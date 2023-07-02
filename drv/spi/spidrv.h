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

#ifndef SPIDEV_H
#define SPIDEV_H

#include <sdkconfig.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#ifdef CONFIG_IDF_TARGET_ESP8266
#include <driver/spi.h>
#else
#include <driver/spi_master.h>
#endif


#ifdef __cplusplus
class EnvObject;


class SpiDevice
{
	public:
	virtual void attach(class EnvObject *);

	virtual const char *drvName() const
	{ return "I2CDevice"; }

	void setName(const char *n);

	static SpiDevice *getFirst()
	{ return m_first; }

	SpiDevice *getNext() const
	{ return m_next; }

	const char *getName() const
	{ return m_name; }

	virtual int init()
	{ return 0; }

	uint8_t getCS() const
	{ return m_cs; }

	bool hasInstance(const char *d);

	virtual const char *exeCmd(struct Terminal &, int argc, const char **argv)
	{ return "Not supported."; }

//	virtual void initSpiCfg(spi_device_interface_config_t &);

	protected:
	SpiDevice(const char *name, uint8_t cs);
	virtual ~SpiDevice() = default;

	static void updateNames(const char *);
	void updateName();

	SpiDevice *m_next = 0;
	char m_name[16] = {0};
#ifndef CONFIG_IDF_TARGET_ESP8266
	spi_device_handle_t m_hdl;
#endif
	uint8_t m_cs;
	static SpiDevice *m_first;
};


extern "C" {
#endif // __cplusplus

#ifdef ESP32
void spidrv_post_cb_relsem(spi_transaction_t *t);
int spidrv_read_regs(spi_device_handle_t hdl, uint8_t reg, uint8_t num, uint8_t *data, SemaphoreHandle_t);
#endif

#ifdef __cplusplus
}	// extern "C"
#endif // __cplusplus
#endif
