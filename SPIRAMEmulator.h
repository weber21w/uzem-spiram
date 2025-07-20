#ifndef SPIRAMEMULATOR_H
#define SPIRAMEMULATOR_H

#include <stdint.h>
#include <stdbool.h>

#define SPIRAM_SIZE 0x80000 // 512KB

// SPI RAM command state machine states
#define SPIRAM_IDLE   0
#define SPIRAM_CMD    1
#define SPIRAM_ADDR0  2
#define SPIRAM_ADDR1  3
#define SPIRAM_ADDR2  4
#define SPIRAM_WRITE  5
#define SPIRAM_READ   6
#define SPIRAM_RDSR   7
#define SPIRAM_WRSR   8

struct SPIRAMEmu {
	SPIRAMEmu() {
		Reset();
	}

	void Reset();

	bool cs_active;
	bool write_enabled;
	uint8_t state;
	uint8_t cmd;
	uint8_t byte;
	uint8_t data[SPIRAM_SIZE];
	uint32_t addr;

	void chipSelectChanged(bool selected);
	uint8_t handleSpiByte(uint8_t byte);
};

// Global instance
extern SPIRAMEmu SPIRAMemulator;

#endif // SPIRAMEMULATOR_H
