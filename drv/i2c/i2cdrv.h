/*
 *  Copyright (C) 2021-2022, Thomas Maier-Komor
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

#ifndef I2CDRV_H
#define I2CDRV_H

#include <sdkconfig.h>
#include <stdint.h>

#ifdef __cplusplus
class EnvObject;

class I2CDevice
{
	public:
	virtual void attach(class EnvObject *);

	virtual const char *drvName() const
	{ return "I2CDevice"; }

	void setName(const char *n);

	static I2CDevice *getFirst()
	{ return m_first; }

	I2CDevice *getNext() const
	{ return m_next; }

	const char *getName() const
	{ return m_name; }

	virtual int init()
	{ return 0; }

	uint8_t getBus() const
	{ return m_bus; }

	uint8_t getAddr() const
	{ return m_addr >> 1; }

#ifdef CONFIG_I2C_XCMD
	virtual const char *exeCmd(struct Terminal &, int argc, const char **argv)
	{ return "Not supported."; }
#endif

	virtual void addIntr(uint8_t gpio);

	static bool hasInstance(const char *);
	static I2CDevice *getByAddr(uint8_t addr);

	protected:
	I2CDevice(uint8_t bus, uint8_t addr, const char *name);
	virtual ~I2CDevice() = default;

	static void updateNames(const char *);
	void updateName();

	I2CDevice *m_next;
	uint8_t m_bus, m_addr;
	char m_name[14] = {0};
	static I2CDevice *m_first;
};


extern "C" {
#endif

int i2c_init(uint8_t bus, uint8_t sda, uint8_t scl, unsigned freq, uint8_t xpullup);
int i2c_read(uint8_t bus, uint8_t addr, uint8_t *d, uint8_t n);
int i2c_read2(uint8_t port, uint8_t addr, uint8_t reg0, uint8_t reg1, uint8_t *d, uint8_t n);
int i2c_write0(uint8_t port, uint8_t addr);
int i2c_write1(uint8_t bus, uint8_t addr, uint8_t r);
int i2c_write2(uint8_t bus, uint8_t addr, uint8_t r, uint8_t v);
int i2c_write4(uint8_t bus, uint8_t addr, uint8_t r0, uint8_t v0, uint8_t r1, uint8_t v1);
int i2c_writen(uint8_t bus, uint8_t addr, uint8_t *d, unsigned n);
int i2c_write(uint8_t bus, uint8_t *d, unsigned n, uint8_t stop, uint8_t start);
int i2c_write_nack(uint8_t port, uint8_t *d, unsigned n, uint8_t stop, uint8_t start);
int i2c_w1rd(uint8_t port, uint8_t addr, uint8_t w, uint8_t *d, uint8_t n);
int i2c_bus_valid(uint8_t bus);

#ifdef __cplusplus
}
#endif

#endif
