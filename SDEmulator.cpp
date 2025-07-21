/*
(The MIT License)

Copyright (c) 2013 Håkon Nessjøen <haakon.nessjoen@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <stdio.h>
#include <math.h>
#include <sys/stat.h>
#include <dirent.h>
#include "SDEmulator.h"

#ifdef USE_SPI_DEBUG
#define SPI_DEBUG(...) printf(__VA_ARGS__)
#else
#define SPI_DEBUG(...)
#endif

#ifdef SPI_DEBUG
char ascii(unsigned char ch){
    if(ch >= 32 && ch <= 127){
        return ch;
    }
    return '.';
}

#endif

extern void SDSeekToOffset(uint32_t offset);
extern uint8_t SDReadByte();
extern void SDWriteByte(uint8_t byte);
extern char ascii(uint8_t val);

/* bootsector jump instruction */
unsigned char bootjmp[3] = { 0xeb, 0x3c, 0x90 };
unsigned char oem_name[8] = "uzemSDe";

static int posBootsector;
static int posFatSector;
static int posRootDir;
static int posDataSector;
static int clusterSize;

static bool hexDebug=false;

void SDEmu::debug(bool value) {
	hexDebug = value;
}

void SDEmu::chipSelectChanged(bool selected) {
	cs_active = selected;
}

static void long2shortfilename(char *dst, char *src) {
	int i;
	for (i = 0; i < 8; ++i) {
		if (*src == '.') {
			dst+=8-i;
			break;
		}
		*dst++ = toupper(*src++);
	}
	char *dot = strchr(src, '.') + 1;
	if (dot > src) {
		for (i = 0; i < 3; ++i) {
			if (*dot != 0) {
				*dst++ = toupper(*dot++);
			}
		}
	}
}

void SDEmu::SDBuildMBR(SDPartitionEntry* entry){
    // total bytes in the MBR (one sector)
    emulatedMBRLength = entry->sectorOffset * 512;
    emulatedMBR       = (uint8_t*)malloc(emulatedMBRLength);
    memset(emulatedMBR, 0, emulatedMBRLength);

    // copy in the one partition entry
    memcpy(emulatedMBR + 0x1BE, entry, sizeof(SDPartitionEntry));

    // standard MBR signature
    emulatedMBR[0x1FE] = 0x55;
    emulatedMBR[0x1FF] = 0xAA;
}

uint8_t SDEmu::handleSpiByte(uint8_t byte) {
    uint8_t response = 0xFF;
    switch(spiState){
    case SD_IDLE_STATE:
        if(byte == 0xff){
        	//SPI_DEBUG("Idle->0xff\n");
            response = 0xff; // echo back that we're ready
            break;
        }
        spiCommand = byte;
        response = 0x00;
        spiState = SD_ARG_X_HI;
        break;
    case SD_ARG_X_HI:
        SPI_DEBUG("x hi: %02X\n",byte);
        spiArgXhi = byte;
        response = 0x00;
        spiState = SD_ARG_X_LO;
        break;
    case SD_ARG_X_LO:
        SPI_DEBUG("x lo: %02X\n",byte);
        spiArgXlo = byte;
        response = 0x00;
        spiState = SD_ARG_Y_HI;
        break;
    case SD_ARG_Y_HI:
        SPI_DEBUG("y hi: %02X\n",byte);
        spiArgYhi = byte;
        response = 0x00;
        spiState = SD_ARG_Y_LO;
        break;
    case SD_ARG_Y_LO:
        SPI_DEBUG("y lo: %02X\n",byte);
        spiArgYlo = byte;
        response = 0x00;
        spiState = SD_ARG_CRC;
        break;
    case SD_ARG_CRC:
        // assemble the 32bit LBA from the four bytes we just shifted in
        spiArg = ((uint32_t)spiArgXhi << 24)
               | ((uint32_t)spiArgXlo << 16)
               | ((uint32_t)spiArgYhi <<  8)
               |  (uint32_t)spiArgYlo;
            SPI_DEBUG("SPI - CMD%d (%02X) spiArg=0x%08X CRC:%02X\n", spiCommand^0x40, spiCommand, spiArg, byte);
        // ignore CRC and process commands
        switch(spiCommand){
        case 0x40: //CMD0 =  RESET / GO_IDLE_STATE
            response = 0x00;
            spiState = SD_RESPOND_SINGLE;
            spiResponseBuffer[0] = 0xff; // 8 clock wait
            spiResponseBuffer[1] = 0x01; // send command response R1->idle flag
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+2;
            spiByteCount = 0;
            break;
        case 0x41: //CMD1 =  INIT / SEND_OP_COND
            response = 0x00;
            spiState = SD_RESPOND_SINGLE;
            spiResponseBuffer[0] = 0x00; // 8-clock wait
            spiResponseBuffer[1] = 0x00; // no error
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+2;
            spiByteCount = 0;
            break;

        case 0x48: //CMD8 =  INIT / SEND_IF_COND
            response = 0x00;
            spiState = SD_RESPOND_SINGLE;
            spiResponseBuffer[0] = 0xff; // 8-clock wait
            spiResponseBuffer[1] = 0x01; // return command response R7
            spiResponseBuffer[2] = 0x00; // return command response R7
            spiResponseBuffer[3] = 0x00; // return command response R7
            spiResponseBuffer[4] = 0x01; // return command response R7 voltage accepted
            spiResponseBuffer[5] = spiArgYlo; // return command response R7 check pattern
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+6;
            spiByteCount = 0;
            break;

        case 0x4c: //CMD12 =  STOP_TRANSMISSION
            response = 0x00;
            spiState = SD_RESPOND_SINGLE;
            spiResponseBuffer[0] = 0xff; //stuff byte
            spiResponseBuffer[1] = 0xff; //stuff byte
            spiResponseBuffer[2] = 0x00; // card is ready //in "trans" state
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+3;
            spiByteCount = 0;
            break;


        case 0x51: //CMD17 =  READ_BLOCK
            response = 0x00;
            spiState = SD_RESPOND_SINGLE;
            spiResponseBuffer[0] = 0xFF; // 8-clock wait
            spiResponseBuffer[1] = 0x00; // no error
            spiResponseBuffer[2] = 0xFE; // start block
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+3;
            SDSeekToOffset(spiArg);
            spiByteCount = 512;
            break;
        case 0x52: //CMD18 =  MULTI_READ_BLOCK
            response = 0x00;
            spiState = SD_RESPOND_MULTI;
            spiResponseBuffer[0] = 0xFF; // 8-clock wait
            spiResponseBuffer[1] = 0x00; // no error
            spiResponseBuffer[2] = 0xFE; // start block
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+3;
            spiCommandDelay=0;
            SDSeekToOffset(spiArg);
            spiByteCount = 0;
            break;   
        case 0x58: //CMD24 =  WRITE_BLOCK
            response = 0x00;
            spiState = SD_WRITE_SINGLE;
            spiResponseBuffer[0] = 0x00; // 8-clock wait
            spiResponseBuffer[1] = 0x00; // no error
            spiResponseBuffer[2] = 0xFE; // start block
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+3;
            SDSeekToOffset(spiArg);
            spiByteCount = 512;
            break;

        case 0x69: //ACMD41 =  SD_SEND_OP_COND  (ACMD<n> is the command sequence of CMD55-CMD<n>)
            response = 0x00;
            spiState = SD_RESPOND_SINGLE;
            spiResponseBuffer[0] = 0xff; // 8 clock wait
           	spiResponseBuffer[1] = 0x00; // send command response R1->OK
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+2;
            spiByteCount = 0;
            break;

        case 0x77: //CMD55 =  APP_CMD  (ACMD<n> is the command sequence of CMD55-CMD<n>)
            response = 0x00;
            spiState = SD_RESPOND_SINGLE;
            spiResponseBuffer[0] = 0xff; // 8 clock wait
            spiResponseBuffer[1] = 0x01; // send command response R1->idle flag
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+2;
            spiByteCount = 0;
            break;


        case 0x7A: //CMD58 =  READ_OCR
            response = 0x00;
            spiState = SD_RESPOND_SINGLE;
            spiResponseBuffer[0] = 0xff; // 8 clock wait
            spiResponseBuffer[1] = 0x00; // send command response R1->ok
            spiResponseBuffer[2] = 0x80; // return command response R3
            spiResponseBuffer[3] = 0xff; // return command response R3
            spiResponseBuffer[4] = 0x80; // return command response R3
            spiResponseBuffer[5] = 0x00; // return command response R3
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+6;
            spiByteCount = 0;
            break;

        default:
			printf("Unknown SPI command: %d\n", spiCommand);
            response = 0x00;
            spiState = SD_RESPOND_SINGLE;
            spiResponseBuffer[0] = 0x02; // data accepted
            spiResponseBuffer[1] = 0x05;  //i illegal command
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+2;
            break;
        }
        break;

   case SD_RESPOND_SINGLE:
        response = *spiResponsePtr;
        SPI_DEBUG("SPI - Respond: %02X\n",response);
        spiResponsePtr++;
        if(spiResponsePtr == spiResponseEnd){
            if(spiByteCount != 0){
                spiState = SD_READ_SINGLE_BLOCK;
            }
            else{
                spiState = SD_IDLE_STATE;
            }
        }
        break;

    case SD_RESPOND_MULTI:
    	if(spiCommandDelay!=0){
    		spiCommandDelay--;
    		response=0xff;
    		break;
    	}

        response = *spiResponsePtr;
        SPI_DEBUG("SPI - Respond: %02X\n",response);
        spiResponsePtr++;

        if(response==0 && spiByteCount==0){
        	spiCommandDelay=250; //average delay based on a sample of cards
        }

        if(spiResponsePtr == spiResponseEnd){
            spiState = SD_READ_MULTIPLE_BLOCK;
            spiByteCount = 512;
        }
        break;

    case SD_READ_SINGLE_BLOCK:
        response = SDReadByte();
        #ifdef USE_SPI_DEBUG
	{
            // output a nice display to see sector data
            int i = 512-spiByteCount;
            int ofs = i&0x000F;
            static unsigned char buf[16];
            if(i > 0 && (ofs == 0)){
                printf("%04X: ",i-16);
                for(int j=0; j<16; j++) printf("%02X ",buf[j]);
                printf("| ");
                for(int j=0; j<16; j++) printf("%c",ascii(buf[j]));
                SPI_DEBUG("\n");
            }
            buf[ofs] = response;
	}
        #endif
        spiByteCount--;
        if(spiByteCount == 0){
            spiResponseBuffer[0] = 0x00; //CRC
            spiResponseBuffer[1] = 0x00; //CRC
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+2;
            spiState = SD_RESPOND_SINGLE;
        }
        break;

    case SD_READ_MULTIPLE_BLOCK:
        if(response == 0x4C){ //CMD12 - stop multiple read transmission
            spiState = SD_RESPOND_SINGLE;

        	spiCommand = 0x4C;
        	response = 0x00;
        	spiState = SD_ARG_X_HI;
            spiByteCount = 0;
            break;
        }
        else{
            response = SDReadByte();
        }
        SPI_DEBUG("SPI - Data[%d]: %02X\n",512-spiByteCount,response);
        spiByteCount--;
        //inter-sector
        //NOTE: Current MoviePlayer.hex does not work with two delay bytes after the CRC. It has
        //been coded to work with a MicroSD card. These cards usually have only 1 delay byte after the CRC.
        //Uzem uses two delay bytes after the CRC since it is what regular SD cards does
        //and we want to emulate the "worst" case.
        if(spiByteCount == 0){
            spiResponseBuffer[0] = 0x00; //CRC
            spiResponseBuffer[1] = 0x00; //CRC
            spiResponseBuffer[2] = 0xff; //delay
            spiResponseBuffer[3] = 0xff; //delay
            spiResponseBuffer[4] = 0xFE; // start block
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+5;
            spiArg+=512; // automatically move to next block
            SDSeekToOffset(spiArg);
            spiByteCount = 512;
            spiState = SD_RESPOND_MULTI;
        }
        break;

    case SD_WRITE_SINGLE:
        response = *spiResponsePtr;
        SPI_DEBUG("SPI - Respond: %02X\n",response);
        spiResponsePtr++;
        if(spiResponsePtr == spiResponseEnd){
            if(spiByteCount != 0){
                spiState = SD_WRITE_SINGLE_BLOCK;
            }
            else{
                spiState = SD_IDLE_STATE;
            }
        }
        break;    
    case SD_WRITE_SINGLE_BLOCK:
        SDWriteByte(response);
        SPI_DEBUG("SPI - Data[%d]: %02X\n",spiByteCount,response);
        response = 0xFF;
        spiByteCount--;
        if(spiByteCount == 0){
            spiResponseBuffer[0] = 0x00; //CRC
            spiResponseBuffer[1] = 0x00; //CRC
            spiResponsePtr = spiResponseBuffer;
            spiResponseEnd = spiResponsePtr+2;
            spiState = SD_RESPOND_SINGLE;
            //SDCommit();
        }
        break;    
    }
	return response;
}

uint8_t SDEmu::SDReadByte(){
	uint8_t result;

	if (emulatedMBR && emulatedReadPos != 0xFFFFFFFF) {
		result = emulatedMBR[emulatedReadPos++];
	} else {
		read(&result);
	}

	return result;
}

void SDEmu::SDWriteByte(uint8_t value){
	(void)value;
	fprintf(stderr, "No write support in SD emulation\n");
}

void SDEmu::SDSeekToOffset(uint32_t pos){
	if (emulatedMBR) {
		if (pos < emulatedMBRLength) {
			emulatedReadPos = pos;
		} else {
			seek(pos);
			emulatedReadPos = 0xFFFFFFFF;
		}
	} else {
		seek(pos);
	}
}

int SDEmu::init_with_directory(const char *path) {
	int i;
	struct stat st;

	bool cs_active = false;
	//2G SD card, 32k per cluster FAT16
	memcpy(&bootsector.bootjmp, bootjmp, 3);
	memcpy(&bootsector.oem_name, oem_name, 8);
	bootsector.bytes_per_sector = 512;
	bootsector.sectors_per_cluster =64; //32K cluster size
	bootsector.reserved_sector_count = 1;
	bootsector.table_count = 2;
	bootsector.root_entry_count = 512;
	bootsector.total_sectors_16 = 0;
	bootsector.media_type = 0xF8;
	bootsector.sectors_per_fat = 0x76; //1024; /* Update ? */
	bootsector.sectors_per_track = 32;
	bootsector.head_side_count = 32;
	bootsector.hidden_sector_count = 0;
	bootsector.total_sectors_32 = 3854201; /* remember: file system id in mbr must be 06 */
	bootsector.drive_no = 4;
	bootsector.extended_fields = 0x29;
	bootsector.serial_number = 1234567;
	memcpy(&bootsector.volume_label, "UZEBOX     ", 11);
	memcpy(&bootsector.filesystem_type, "FAT16", 5);
	bootsector.signature[0] = 0x55;
	bootsector.signature[1] = 0xAA;

	posBootsector = 0;
	posFatSector  = bootsector.bytes_per_sector + (bootsector.reserved_sector_count * bootsector.bytes_per_sector);
	posRootDir    = posFatSector + (bootsector.table_count * (bootsector.sectors_per_fat * bootsector.bytes_per_sector));
	posDataSector = posRootDir + (((bootsector.root_entry_count * 32) / bootsector.bytes_per_sector) * bootsector.bytes_per_sector);
	clusterSize =  bootsector.bytes_per_sector*bootsector.sectors_per_cluster;

	DIR *dir = opendir(path);
	if (dir == NULL) {
		return -1;
	}

	memcpy(toc[0].name,"UZEBOX  ",8);
	memcpy(toc[0].ext,"   ",3);
	toc[0].attrib = SDEFA_ARCHIVE | SDEFA_VOLUME_ID;

	i = 1;
	struct dirent *entry;
	int freecluster = 2;
	printf("SD Emulation of following files:\n");
	while ((entry = readdir(dir))) {
		if (entry && entry->d_name[0] != '.') {
			char *statpath = (char *)malloc(strlen(path) + strlen(entry->d_name) + 2);
			strcpy(statpath, path);
			strcat(statpath, "/");
			strcat(statpath, entry->d_name);
			stat(statpath, &st);
			paths[i] = statpath;

			memset(toc[i].name, 32, 11);
			long2shortfilename((char *)toc[i].name, (char *)entry->d_name);

			toc[i].attrib = SDEFA_ARCHIVE;
			toc[i].cluster_no = freecluster;

			//Fill the FAT with the file's cluster chain
			int fileClustersCount=ceil(st.st_size / (bootsector.sectors_per_cluster * 512.0f));
			for(int j=freecluster;j<(freecluster+fileClustersCount-1);j++){
				clusters[j]=j+1;
			}
			clusters[freecluster+fileClustersCount-1]=0xffff; //Last cluster in file marker (EOC)

			toc[i].filesize = st.st_size;
			printf("\t%d: %s:%ld\n", i, entry->d_name, st.st_size);
			freecluster += fileClustersCount;
			if (++i == MAX_FILES) {
				break;
			}
		}
	}
	
	//build MBR
	SDPartitionEntry pe;
	memset(&pe,0,sizeof(pe));
	pe.state       = 0x00;                          // non-bootable
	pe.type        = 0x06;                          // FAT16 partition
	pe.sectorOffset= 1;                             // partition starts at LBA 1
	pe.sectorCount = bootsector.total_sectors_32;   // size in sectors
	SDBuildMBR(&pe);
	return 0;
}

void SDEmu::read(unsigned char *ptr) {
	static int lastfile=-1;
	static int lastfileStart=0;
	static int lastfileEnd=0;
	static FILE *fp=NULL;
	static int lastPos=0;

	int pos;
	unsigned char c;

	// < 512 Bootsector
	if (position < posFatSector)	{
		pos = position - bootsector.bytes_per_sector;
		unsigned char *boot = (unsigned char *)&bootsector;
		if (pos < sizeof(bootsector)) {
			*ptr++ = *(boot + (pos));
		} else {
			*ptr++ = 0;
		}
	} else
	// Fat table
	if (position < posRootDir) {
		pos = position - posFatSector;
		//printf("sdemu: reading fat: %d\n", pos);
		unsigned char *table = (unsigned char *)&clusters;
		*ptr++ = *(table + pos);
	}
	else if (position < posDataSector) {
		pos = position - posRootDir;
		unsigned char *table = (unsigned char *)&toc;
		*ptr++ = *(table + pos);
	} else {
		int cluster=-1;
		pos = position - posDataSector;
		if (lastfile == -1 || pos < lastfileStart || pos > lastfileEnd) {
			int i;
			lastfile = -1;
			for (i = 0; i < MAX_FILES; ++i) {
				cluster = (pos/512/bootsector.sectors_per_cluster) + 2;
				if (toc[i].name[0] != 0 && cluster >= toc[i].cluster_no && cluster <= toc[i].cluster_no + (toc[i].filesize/512/bootsector.sectors_per_cluster)) {
					lastfile = i;
					lastfileStart = (toc[i].cluster_no-2)*clusterSize;
					lastfileEnd = lastfileStart + (((toc[i].filesize/clusterSize)+1)*clusterSize)-1; //account for cluster size padding

					if (fp != NULL) {
						fclose(fp);
					}
					//printf("Opening file: %s, start=%d, end=%d, cluster=%d\n", paths[i],lastfileStart,lastfileEnd, cluster);
					fp = fopen(paths[i], "rb");
					lastPos=-1;
					break;
				}
			}
		}
		if (lastfile == -1 || fp == NULL) { *ptr++ = 0; }
		else {
			if(pos!=(lastPos+1)){
				fseek(fp, pos - ((toc[lastfile].cluster_no-2)*clusterSize), SEEK_SET);
			}

			if(pos<(lastfileStart+toc[lastfile].filesize)){
				c= fgetc(fp);
			}else{
				c=0;
			}

			lastPos=pos;
			*ptr++ =c;
		}
	}
	position++;
}

void SDEmu::seek(int pos) {
	position = pos;
}
