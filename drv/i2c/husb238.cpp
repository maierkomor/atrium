/*
 *  Copyright (C) 2026, Thomas Maier-Komor
 *  Atrium Firmware Package for ESP
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sdkconfig.h>

#ifdef CONFIG_HUSB238

#include "husb238.h"
#include "cyclic.h"
#include "log.h"
#include "terminal.h"

#include <esp_err.h>

#define TAG MODULE_HUSB238

#define PD_STATUS0	0x0
#define PD_STATUS1	0x1
#define SRC_PDO_5V	0x2
#define SRC_PDO_9V	0x3
#define SRC_PDO_12V	0x4
#define SRC_PDO_15V	0x5
#define SRC_PDO_18V	0x6
#define SRC_PDO_20V	0x7
#define SRC_PDO		0x8
#define GO_CMD		0x9

#define PD_NONE	0x0
#define PD_5V	0x1
#define PD_9V	0x2
#define PD_12V	0x3
#define PD_15V	0x4
#define PD_18V	0x5
#define PD_20V	0x6
#define PD_28V	0x7	// unspecified
#define PD_36V	0x8	// unspecified
#define PD_48V	0x9	// unspecified

#define BIT_CC_DIR	0x80
#define BIT_ATTACH	0x40
#define BITS_PD_RESP	0x38
#define BIT_5V		0x04
#define BITS_CURRENT	0x03

#define ST1_CC2		0x80
#define ST1_ATTACHED	0x40
#define ST1_SUCCESS	0x08
#define ST1_INV_CMD	0x18
#define ST1_NOT_SUP	0x20
#define ST1_FAILED	0x28
#define ST1_5V_1A5	0x05
#define ST1_5V_2A4	0x06
#define ST1_5V_3A0	0x07

// SRC_PDO_xxV
#define SRC_DETECTED	0x80
#define SRC_CURRENT	0x0f

#define GO_REQ_PDO	0x01
#define GO_SEND_CAP	0x04
#define GO_RESET	0x10

uint8_t SrcPdo[] = { 0, 5, 9, 12, 15, 18, 20 };

uint8_t PdVoltLvl[] = { 5, 9, 12, 15, 18, 20, 28, 36, 48 };

uint16_t PdSrcCurrent[] = {
	500, 700, 1000, 1250, 1500, 1750,
	2000, 2250, 2500, 2750, 3000,
	3250, 3500, 4000, 4500, 5000
};


HUSB238::HUSB238(uint8_t bus, uint8_t addr)
: I2CDevice(bus,addr,"husb238a")
, m_volt("voltage","V","%3.0f")
, m_curr("current","mA","%4.3f")
{

}

void HUSB238::attach(EnvObject *root)
{
	root->add(&m_volt);
	root->add(&m_curr);
	cyclic_add_task(m_name,cyclic,(HUSB238*)this,0);
}


HUSB238 *HUSB238::create(uint8_t bus, uint8_t addr)
{
	uint8_t val;
	if (i2c_w1rd(bus,addr,0,&val,sizeof(val))) {
		log_warn(TAG,"no HUSB238 at %u:0x%x",bus,addr);
		return 0;
	}
	log_info(TAG,"HUSB238 at %u:0x%x",bus,addr);
	return new HUSB238(bus, addr);
}


unsigned HUSB238::cyclic(void *arg)
{
	HUSB238 *dev = (HUSB238 *)arg;
	return dev->pcyclic();
}


unsigned HUSB238::pcyclic()
{
	uint8_t v;
	if (0 == readReg(PD_STATUS0,v)) {
		uint8_t volt = v >> 4;
		uint8_t curr = v & 0xf;
		if (volt) {
			m_volt.set(SrcPdo[volt]);
			m_curr.set(PdSrcCurrent[curr]);
		} else {
			m_volt.set(NAN);
			m_curr.set(NAN);
		}
	}
	return 1000;
}


const char *HUSB238::readReg(uint8_t reg, uint8_t &val)
{
	if (esp_err_t err = i2c_w1rd(m_bus,m_addr,reg,&val,sizeof(val)))
		return esp_err_to_name(err);
	return 0;
}


#ifdef CONFIG_I2C_XCMD
static void printStatus(Terminal &term, uint8_t v)
{
	const char *err = 0;
	uint8_t volt = v >> 4;
	uint8_t curr = v & 0xf;
	if (0 == volt) {
		err = "unattached";
	} else if (volt < sizeof(SrcPdo)/sizeof(SrcPdo[0])) {
		term.printf("PD %uV, %umA\n",SrcPdo[volt],PdSrcCurrent[curr]);
	} else {
		err = "reserver values";
	}
	if (err)
		term.println(err);

}


void HUSB238::printCapabilities(Terminal &term)
{
	uint8_t v = 0;
	if (readReg(SRC_PDO_5V,v)) {
		term.println("No 5V SRC PDO register.");
	} else if (v & SRC_DETECTED) {
		term.printf("5V, %umA\n",PdSrcCurrent[v&SRC_CURRENT]);
	}
	if (readReg(SRC_PDO_9V,v)) {
		term.println("No 9V SRC PDO register.");
	} else if (v & SRC_DETECTED) {
		term.printf("9V, %umA\n",PdSrcCurrent[v&SRC_CURRENT]);
	}
	if (readReg(SRC_PDO_12V,v)) {
		term.println("No 12V SRC PDO register.");
	} else if (v & SRC_DETECTED) {
		term.printf("12V, %umA\n",PdSrcCurrent[v&SRC_CURRENT]);
	}
	if (readReg(SRC_PDO_15V,v)) {
		term.println("No 15V SRC PDO register.");
	} else if (v & SRC_DETECTED) {
		term.printf("15V, %umA\n",PdSrcCurrent[v&SRC_CURRENT]);
	}
	if (readReg(SRC_PDO_18V,v)) {
		term.println("No 18V SRC PDO register.");
	} else if (v & SRC_DETECTED) {
		term.printf("18V, %umA\n",PdSrcCurrent[v&SRC_CURRENT]);
	}
	if (readReg(SRC_PDO_20V,v)) {
		term.println("No 20V SRC PDO register.");
	} else if (v & SRC_DETECTED) {
		term.printf("20V, %umA\n",PdSrcCurrent[v&SRC_CURRENT]);
	}
}


const char *HUSB238::exeCmd(Terminal &term, int argc, const char **args)
{
	if ((argc == 0) || (0 == strcmp(args[0],"-h"))) {
		term.println(
			"status  : print status\n"
			"cap     : print capabilities\n"
			"req <v> : request voltage\n"
			);
		return 0;
	}
	if (argc == 1) {
		if (0 == strcmp(args[0],"status")) {
			uint8_t v = 0;
			if (const char *err = readReg(PD_STATUS0,v))
				return err;
			printStatus(term,v);
		} else if (0 == strcmp(args[0],"cap")) {
			printCapabilities(term);
		} else {
			return "Invalid argument #1.";
		}
	} else if (argc == 2) {
		//char *e;
		//long l = strtol(args[1],&e,0);
		if (0 == strcmp(args[0],"req")) {
		} else {
			return "Invalid argument #1.";
		}
	} else {
		return "Invalid number of arguments.";
	}
	return 0;
}
#endif // CONFIG_I2C_XCMD
#endif
