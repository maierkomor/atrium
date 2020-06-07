/*
 *  Copyright (C) 2018-2020, Thomas Maier-Komor
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

#ifdef CONFIG_BUTTON

#include "actions.h"
#include "button.h"
#include "cyclic.h"
#include "globals.h"
#include "log.h"
#include "relay.h"
#include "settings.h"
#include "wifi.h"

#include <driver/gpio.h>
#include <esp_system.h>
#include <esp_wifi.h>

#if CONFIG_BUTTON_GPIO < 0 || CONFIG_BUTTON_GPIO >= GPIO_PIN_COUNT
#error gpio value for button out of range
#endif


static char TAG[] = "button";
static uptime_t PressedAt;
static uint32_t DownTime = 0;


static void button_intr(void *)
{
	// no ESP_LOGx from ISRs!
	uptime_t now = uptime();
	if (gpio_get_level((gpio_num_t)CONFIG_BUTTON_GPIO) == CONFIG_BUTTON_PRESSED) {
		PressedAt = now;
	} else {
		DownTime = now - PressedAt;
		PressedAt = 0;
	}
}


static unsigned button_loop()
{
	if (DownTime == 0)
		return 50;
	if (DownTime < 50) {
		log_dbug(TAG,"button event %ums debounced",DownTime);
		DownTime = 0;
		return 20;
	}
	log_info(TAG,"button event %ums",DownTime);
	if (DownTime < 400) {
#ifdef CONFIG_RELAY
		relay_toggle();
#endif
	} else if ((DownTime > 1000) && (DownTime < 2000)) {
		bool en = !RTData.timers_enabled();
		log_info(TAG,"%sabling timed actions",en?"en":"dis");
		Config.set_actions_enable(en);
		RTData.set_timers_enabled(en);
	} else
	if (DownTime > 3000) {
#ifdef CONFIG_WPS
		wifi_wps_start();
#elif defined CONFIG_SMARTCONFIG
		smartconfig_start();
#endif
		DownTime = 0;
		return 100;
	} else
	if (DownTime > 10000)
		factoryReset();
	DownTime = 0;
	return 20;
}


#ifndef ESP_INTR_FLAG_EDGE
#define ESP_INTR_FLAG_EDGE 0	// for esp8266 to esp32 compatibility
#endif
extern "C"
void button_setup()
{
	log_info(TAG,"configuring button at gpio %u",CONFIG_BUTTON_GPIO);
	if (esp_err_t e = gpio_set_direction((gpio_num_t)CONFIG_BUTTON_GPIO,GPIO_MODE_INPUT)) {
		log_error(TAG,"unable to set GPIO %d as input: %s",CONFIG_BUTTON_GPIO,esp_err_to_name(e));
		return;
	}
	if (esp_err_t e = gpio_pullup_en((gpio_num_t)CONFIG_BUTTON_GPIO)) {
		log_error(TAG,"unable to enable pull-up on GPIO %d: %s",CONFIG_BUTTON_GPIO,esp_err_to_name(e));
		return;
	}
	if (esp_err_t e = gpio_set_intr_type((gpio_num_t)CONFIG_BUTTON_GPIO,GPIO_INTR_ANYEDGE)) {
		log_error(TAG,"error setting interrupt type to anyadge on gpio %d: %s",CONFIG_BUTTON_GPIO,esp_err_to_name(e));
		return;
	}
	// - this is done during startup for all handlers
	//if (esp_err_t e = gpio_install_isr_service(ESP_INTR_FLAG_EDGE))
		//log_warn(TAG,"installing isr service returned %s",esp_err_to_name(e));
	if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)CONFIG_BUTTON_GPIO,button_intr,0)) {
		log_error(TAG,"registering isr handler returned %s",esp_err_to_name(e));
		return;
	}
	add_cyclic_task(TAG,button_loop,0);
}

#endif
