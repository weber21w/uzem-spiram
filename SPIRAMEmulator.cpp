#include "SPIRAMEmulator.h"
#include <string.h>

SPIRAMEmu SPIRAMemulator;

void SPIRAMEmu::Reset() {
	memset(data, 0, sizeof(data));
	cs_active = false;
	write_enabled = false;
	state = SPIRAM_IDLE;
	cmd = 0;
	byte = 0;
	addr = 0;
}

void SPIRAMEmu::chipSelectChanged(bool selected) {
	cs_active = selected;
	if (!selected) {
		state = SPIRAM_IDLE;
		byte = 0;
	}
}

uint8_t SPIRAMEmu::handleSpiByte(uint8_t v) {
	if (state == SPIRAM_READ) {
		if (byte < 3) {
			if (byte == 0) addr = ((uint32_t)v) << 16;
			else if (byte == 1) addr |= ((uint32_t)v) << 8;
			else addr |= v;
			byte++;
			return 0x00;
		} else {
			uint8_t val = data[addr % SPIRAM_SIZE];
			addr++;
			return val;
		}
	}

	switch (state) {
	case SPIRAM_IDLE:
		cmd = v;
		byte = 0;
		switch (cmd) {
			case 0x03: state = SPIRAM_READ; break;
			case 0x02: state = SPIRAM_WRITE; break;
			case 0x05: state = SPIRAM_RDSR; break;
			case 0x01: state = SPIRAM_WRSR; break;
			default:   state = SPIRAM_IDLE; break;
		}
		return 0x00;

	case SPIRAM_RDSR:
		return write_enabled ? 0x02 : 0x00;

	case SPIRAM_WRSR:
		write_enabled = (v & 0x02);
		state = SPIRAM_IDLE;
		return 0x00;

	case SPIRAM_WRITE:
		if (byte == 0) addr = ((uint32_t)v) << 16;
		else if (byte == 1) addr |= ((uint32_t)v) << 8;
		else if (byte == 2) addr |= v;
		else {
			if (write_enabled) {
				data[addr % SPIRAM_SIZE] = v;
				addr++;
			}
		}
		byte++;
		return 0x00;

	default:
		state = SPIRAM_IDLE;
		return 0x00;
	}
}
