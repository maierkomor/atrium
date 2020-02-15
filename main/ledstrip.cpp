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

#ifdef CONFIG_LEDSTRIP

#include "log.h"
#include "ws2812b.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/*
#define BLACK	0X000000
#define WHITE	0xffffff
#define RED	0xff0000
#define GREEN	0x00ff00
#define BLUE	0x0000ff
#define MAGENTA	0xff00ff
#define YELLOW	0xffff00
#define CYAN	0x00ffff
*/

#define BLACK	0X000000
#define WHITE	0x202020
#define RED	0x200000
#define GREEN	0x002000
#define BLUE	0x000020
#define MAGENTA	0x200020
#define YELLOW	0x202000
#define CYAN	0x002020
#define PURPLE	0x100010

static const char TAG[] = "ledstrip";

static uint32_t ColorMap[] = {
	BLACK, WHITE, RED, GREEN, BLUE, MAGENTA, YELLOW, CYAN, PURPLE
};

static WS2812BDrv LED_Strip((rmt_channel_t)CONFIG_LEDSTRIP_CHANNEL,(gpio_num_t)CONFIG_LEDSTRIP_GPIO,CONFIG_LEDSTRIP_NUMLEDS);

extern "C"
void ledstrip_task(void *ignored)
{
	uint8_t off = 0;
	LED_Strip.reset();
	log_info(TAG,"0");
	vTaskDelay(1000/portTICK_PERIOD_MS);
	LED_Strip.set_leds(WHITE);
	log_info(TAG,"1");
	LED_Strip.update();
	vTaskDelay(1000/portTICK_PERIOD_MS);
	LED_Strip.set_leds(BLACK);
	log_info(TAG,"2");
	LED_Strip.update();
	vTaskDelay(1000/portTICK_PERIOD_MS);
	for (unsigned x = 0; x < 256; ++x) {
		log_info(TAG,"3");
		LED_Strip.set_leds(x << 16 | x << 8 | x);
		LED_Strip.update();
		vTaskDelay(50/portTICK_PERIOD_MS);
	}
	for (;;) {
		for (int i = 0; i < CONFIG_LEDSTRIP_NUMLEDS; ++i)
			LED_Strip.set_led(i,ColorMap[(i+off)%(sizeof(ColorMap)/sizeof(ColorMap[0]))]);
		log_info(TAG,"4");
		LED_Strip.update();
		if (++off == sizeof(ColorMap)/sizeof(ColorMap[0]))
			off = 0;
		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}


extern "C"
void ledstrip_setup()
{
	log_info(TAG,"setup");
	LED_Strip.init();
	BaseType_t r = xTaskCreatePinnedToCore(&ledstrip_task, TAG, 2048, NULL, 4, NULL, APP_CPU_NUM);
	if (r != pdPASS)
		log_error(TAG,"task creation failed: %s",esp_err_to_name(r));
	else
		log_info(TAG,"started");
}
#endif
