/*
 *  Copyright (C) 2022, Thomas Maier-Komor
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

#ifndef SX1276_H
#define SX1276_H

#include "spidrv.h"

struct Packet
{
	uint16_t len;
	uint8_t data[];
};


class SX1276 : public SpiDevice
{
	public:
	static SX1276 *create(spi_host_device_t host, spi_device_interface_config_t &cfg, int8_t intr, int8_t reset);
	
	int init() override;
	void attach(class EnvObject *) override;
	int read(char *, int);
	int write(const char *, int);
	int send(const char *, int);
	static SX1276 *getInstance()
	{ return m_inst; }

	void setDio0(uint8_t gpio);
	void setDio1(uint8_t gpio);
	void setDio2(uint8_t gpio);
	void setDio3(uint8_t gpio);
	void setDio4(uint8_t gpio);
	void setDio5(uint8_t gpio);

	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;

	void setPreamble(long length);
	void setSyncWord(int sw);

	typedef enum mode_e {
		mode_sleep = 0, mode_stdb, mode_fstx, mode_tx, mode_fsrx,
		mode_rxcont, mode_rxsingle, mode_cad
	} mode_t;

	bool isLora();
	bool getLora();
	int setLora(bool lora);
	int setCRC(bool);
	int setGain(unsigned);
	int getGain();
	int getOOK();
	int setOOK(bool ook);
	int setMode(mode_t);
	int getOCP();
	int setOCP(bool en);
	int setImax(unsigned imax);
	int getImax();
	int getFreq();
	int setFreq(unsigned f);
	int setPower(float pmax);
	int getPower(float &pmax);
	int setMaxPower(float pmax);
	int getMaxPower(float &pmax);

	// LORA only
	int getBandwidth();
	int setBandwidth(unsigned bw);
	int setHeaderMode(bool implicit);
	int getCodingRate();
	int setCodingRate(uint8_t frac);
	int setSpreadingFactor(unsigned);
	int getSpreadingFactor();

	// FSK/OOK only
	int getBitRate();
	int setBitRate(unsigned br);

	private:
	SX1276(spi_host_device_t host, spi_device_interface_config_t &cfg, int8_t intr, int8_t reset);
	static void postCallback(spi_transaction_t *);
	static void dio0Handler(void *);
	static void dio1Handler(void *);
	static void dio2Handler(void *);
	static void dio3Handler(void *);
	static void dio4Handler(void *);
	static void dio5Handler(void *);
	void readRegsSync(uint8_t reg, uint8_t num);
	int readRegs(uint8_t reg, uint8_t num, uint8_t *);
	int readReg(uint8_t reg, uint8_t *);
	int writeReg(uint8_t r, uint8_t v);
	int writeRegs(uint8_t r, uint8_t num, uint8_t *v);
	static void send_action(void *);
	static void intr_action(void *);	// used internally
	static void intrHandler(void *);
	void processIntr();

	class EnvObject *m_env = 0;
	SemaphoreHandle_t m_sem;
	uint8_t m_opmode = 0;
	int8_t m_reset = -1;
	event_t m_rev = 0;		// receive event
	event_t m_iev[6];
	uint8_t m_rcvbuf[64];
	static SX1276 *m_inst;
};

#endif // SX1276_H
