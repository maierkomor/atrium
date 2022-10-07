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

#ifndef APDS9930_H
#define APDS9930_H

#include "event.h"
#include "i2cdrv.h"

#define APDS9930_ADDR	(0x39<<1)

class EnvNumber;


struct APDS9930 : public I2CDevice
{
	explicit APDS9930(uint8_t port);

	const char *drvName() const
	{ return "apds9930"; }

	int init();
	void attach(class EnvObject *);
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:
	static void trigger(void *);
	static void poweroff(void *);
	static unsigned cycle(void *);
	bool status();
	bool sample();
	unsigned read();
	unsigned poweroff();
	void handle_error();

	EnvNumber *m_lux = 0, *m_prox = 0;
	// coefficients: defaults for open air
	float m_ga = 0.49, m_b = 1.862, m_c = 0.746, m_d = 1.291;
	typedef enum { st_idle, st_sample, st_measure, st_read, st_poweroff } state_t;
	state_t m_state = st_idle;
	uint8_t m_retry = 0;
	bool m_close = false;
	event_t m_near = 0, m_far = 0;
};


#endif
