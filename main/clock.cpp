/*
 *  Copyright (C) 2018-2019, Thomas Maier-Komor
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

#ifdef CONFIG_CLOCK

#include "actions.h"
#include "globals.h"
#include "log.h"
#include "MAX7219.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/gpio.h>

#include <sys/time.h>
#include <time.h>

static char TAG[] = "clock";
static MAX7219Drv Driver((gpio_num_t)CONFIG_CLOCK_CLK,(gpio_num_t)CONFIG_CLOCK_DOUT,(gpio_num_t)CONFIG_CLOCK_CS,(gpio_num_t)CONFIG_CLOCK_OD);


static void display(uint32_t nv)
{
	static uint32_t cv = 0xffffffff;
	for (unsigned i = 0; i < 8; ++i) {
		uint8_t cd = (cv >> (i<<2)) & 0xf;
		uint8_t nd = (nv >> (i<<2)) & 0xf;
		if (nd != cd)
			Driver.setDigit(i,nd);
	}
	cv = nv;
}


static void clock_task(void *ignored)
{
	log_info(TAG,"started");
	display(0);
	log_info(TAG,"got first time sample");
	uint8_t H=0,M=0,S=0;
	uint8_t spin = 1 << 6;
	for (;;) {
		uint8_t h=0,m=0,s=0;
		get_time_of_day(&h,&m,&s);
		if (h != H) {
			H = h;
			Driver.setDigit(7,h/10);
			Driver.setDigit(6,h%10);
		}
		if (m != M) {
			M = m;
			Driver.setDigit(5,m/10);
			Driver.setDigit(4,m%10);
		}
		if (s != S) {
			S = s;
			Driver.setDigit(3,s/10);
			Driver.setDigit(2,s%10);
		}
		spin >>= 1;
		if (spin == 1)
			spin = (1 << 6);
		Driver.setDigit(0,spin);
		vTaskDelay(125/portTICK_PERIOD_MS);
	}
}


extern "C"
void clock_setup()
{
	// 5V level adjustment necessary
	Driver.attach();
	Driver.displayTest(true);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	Driver.displayTest(false);
	Driver.setDigits(8);
	Driver.powerup();
	Driver.setIntensity(0xf);
	Driver.setDecoding(0xfc);
	for (int d = 0; d < 8; ++d)
		Driver.setDigit(d,0);
	BaseType_t r = xTaskCreate(&clock_task, "clock", 2048, NULL, 4, NULL);
	if (r != pdPASS)
		log_error(TAG,"error starting task: %d",r);
}

#endif
