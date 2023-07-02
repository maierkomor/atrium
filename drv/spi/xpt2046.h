/*
 *  Copyright (C) 2023, Thomas Maier-Komor
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

#ifndef XPT2046_H
#define XPT2046_H

#include "spidrv.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>


class XPT2046 : public SpiDevice
{
	public:
	static XPT2046 *create(spi_host_device_t host, spi_device_interface_config_t &cfg, int8_t intr);

	const char *drvName() const override
	{ return "xpt2046"; }

	void attach(class EnvObject *) override;
	int init() override;
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
	void sleep();
	void persist();

	private:
	XPT2046(uint8_t cs, int8_t intr, spi_device_handle_t hdl);
	void readRegs();
	static unsigned cyclic(void *);
	static void intrHandler(void *);
	static void sleep(void *);
	static void wake(void *);

	typedef enum power_state_e { pw_inv = 0, pw_off = 1, pw_sleep = 2, pw_on = 3 } pwst_t;
	EnvNumber m_rx, m_ry, m_rz, m_a0, m_a1;
       	EnvBool m_p;
	event_t m_evp, m_evr;
	uint16_t m_lx = 0, m_ux = 0, m_ly = 0, m_uy = 0;
	uint16_t m_lz = 90, m_uz = 110;
	spi_device_handle_t m_hdl;
	SemaphoreHandle_t m_sem;
	pwst_t m_pwreq = pw_inv, m_pwst = pw_on;
	bool m_pressed = false, m_wake = false;
	static XPT2046 *Instance;
};


#endif

