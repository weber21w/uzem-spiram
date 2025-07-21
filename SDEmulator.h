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

#ifndef _SDEMULATOR_H_
#define _SDEMULATOR_H_

#include <cstring> // for memset()
#include <stdint.h>

#define SD_IDLE_STATE             0
#define SD_ARG_X_LO               1
#define SD_ARG_X_HI               2
#define SD_ARG_Y_LO               3
#define SD_ARG_Y_HI               4
#define SD_ARG_CRC                5
#define SD_RESPOND_SINGLE         6
#define SD_RESPOND_MULTI          7
#define SD_READ_SINGLE_BLOCK      8
#define SD_READ_MULTIPLE_BLOCK    9
#define SD_WRITE_SINGLE          10
#define SD_WRITE_SINGLE_BLOCK    11
#define SD_RESPOND_R1            12
#define SD_RESPOND_R1B           13
#define SD_RESPOND_R2            14
#define SD_RESPOND_R3            15
#define SD_RESPOND_R7            16

// File attribute bits (needed!)
#define SDEFA_READ_ONLY      0x01
#define SDEFA_HIDDEN         0x02
#define SDEFA_SYSTEM         0x04
#define SDEFA_VOLUME_ID      0x08
#define SDEFA_DIRECTORY      0x10
#define SDEFA_ARCHIVE        0x20
#define SDEFA_LONGFILENAME   0x0F

#define MAX_FILES 1024

struct SDPartitionEntry {
	uint8_t state;
	uint8_t startHead;
	uint16_t startCylinder;
	uint8_t type;
	uint8_t endHead;
	uint16_t endCylinder;
	uint32_t sectorOffset;
	uint32_t sectorCount;
};

union SDEmu_date {
	uint16_t raw;
	struct {
		unsigned year  : 7 __attribute__((packed));
		unsigned month : 4 __attribute__((packed));
		unsigned day   : 5 __attribute__((packed));
	} get;
};

union SDEmu_time {
	uint16_t raw;
	struct {
		unsigned ct_hour    : 5 __attribute__((packed));
		unsigned ct_minutes : 6 __attribute__((packed));
		unsigned ct_seconds : 5 __attribute__((packed));
	};
};

typedef struct fat_BS {
	uint8_t  bootjmp[3];
	uint8_t  oem_name[8];
	uint16_t bytes_per_sector;
	uint8_t  sectors_per_cluster;
	uint16_t reserved_sector_count;
	uint8_t  table_count;
	uint16_t root_entry_count;
	uint16_t total_sectors_16;
	uint8_t  media_type;
	uint16_t sectors_per_fat;
	uint16_t sectors_per_track;
	uint16_t head_side_count;
	uint32_t hidden_sector_count;
	uint32_t total_sectors_32;
	uint16_t drive_no;
	uint8_t  extended_fields;
	uint32_t serial_number;
	uint8_t  volume_label[11];
	uint8_t  filesystem_type[8];
	uint8_t  extended_section[448];
	uint8_t  signature[2];
} __attribute__((packed)) fat_BS_t;

struct SDEmu_file {
	uint8_t name[8];
	uint8_t ext[3];
	uint8_t attrib;
	uint8_t nt;
	uint8_t creation_time_tenth;
	union SDEmu_time creation_time;
	union SDEmu_date creation_date;
	union SDEmu_date accessed_date;
	uint16_t zero;
	union SDEmu_time last_modification_time;
	union SDEmu_date last_modification_date;
	uint16_t cluster_no;
	uint32_t filesize;
} __attribute__((packed));

struct SDEmu {
	SDEmu() {
		cs_active = false;
		position = 0;
		memset(&toc, 0, sizeof(toc));
		memset(&bootsector, 0, sizeof(bootsector));
		spiState = SD_IDLE_STATE;
		emulatedMBR = nullptr;
		emulatedMBRLength = 0;
		emulatedReadPos = 0xFFFFFFFF;
	}

	struct fat_BS bootsector;
	struct SDEmu_file toc[MAX_FILES];
	uint16_t clusters[1024 * 512];
	char* paths[MAX_FILES];
	int position;
	bool cs_active;

	uint8_t spiState;
	uint8_t spiCommand;
	uint8_t spiArgXhi, spiArgXlo, spiArgYhi, spiArgYlo;
	uint32_t spiArg;
	uint8_t spiResponseBuffer[8];
	uint8_t* spiResponsePtr, * spiResponseEnd;
	int spiByteCount;
	int spiCommandDelay;
	uint8_t* emulatedMBR;
	uint32_t emulatedMBRLength;
	uint32_t emulatedReadPos;

	void chipSelectChanged(bool selected);
	void SDBuildMBR(SDPartitionEntry* entry);
	int init_with_directory(const char* path);
	void read(unsigned char* ptr);
	void seek(int pos);
	void debug(bool value);
	uint8_t handleSpiByte(uint8_t byte);
	uint8_t SDReadByte();
	void SDWriteByte(uint8_t value);
	void SDSeekToOffset(uint32_t pos);
};

#endif
