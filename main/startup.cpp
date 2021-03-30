/*
 *  Copyright (C) 2018-2021, Thomas Maier-Komor
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

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <esp_err.h>

#include <driver/uart.h>
#include <driver/gpio.h>
#include <nvs.h>
#include <nvs_flash.h>

#ifdef CONFIG_IDF_TARGET_ESP8266
#include <spi_flash.h>
#endif

#include <sdkconfig.h>

#include "cyclic.h"
#include "fs.h"
#include "globals.h"
#include "log.h"
#include "ota.h"
#include "settings.h"
#include "status.h"
#include "uart_terminal.h"
#include "uarts.h"
#include "versions.h"
#include "wifi.h"

#ifndef CHIP_FEATURE_EMB_FLASH
#define CHIP_FEATURE_EMB_FLASH 0
#endif

#ifndef CHIP_FEATURE_BLE
#define CHIP_FEATURE_BLE 0
#endif

#ifndef CHIP_FEATURE_BT
#define CHIP_FEATURE_BT 0
#endif

#if IDF_VERSION > 32
#define DRIVER_ARG 0,0
#else
#define DRIVER_ARG 0
#endif

static const char TAG[] = "init";
static const char BootStr[] = "Starting Atrium Firmware...\r\n";

/*
static void init_detached(int (*fn)(),const char *name,void *arg)
{
	BaseType_t r = xTaskCreatePinnedToCore(fn,name,4096,arg,8,NULL,APP_CPU_NUM);
	if (r != pdPASS) {
		log_error(TAG,"task creation for module '%s' failed: %s",i->name,esp_err_to_name(r));
	}
}
*/


int adc_setup();
int alarms_setup();
int bme_setup();
int button_setup();
int clockapp_setup();
int console_setup();
int cyclic_setup();
int dht_setup();
int dimmer_setup();
int distance_setup();
int event_setup();
int ftpd_setup();
int gpio_setup();
int hall_setup();
int httpd_setup();
int inetd_setup();
int influx_setup();
int ledstrip_setup();
int lightctrl_setup();
int mqtt_setup();
int nightsky_setup();
int ow_setup();
int relay_setup();
int status_setup();
int syslog_setup();
int telnet_setup();
int touchpads_setup();
int udpctrl_setup();
int webcam_setup();

void settings_setup();


static void system_info()
{
	esp_chip_info_t ci;
	esp_chip_info(&ci);
	log_info(TAG,"%s with %d CPU core%s, revision %d%s%s%s%s"
#if IDF_VERSION >= 32
		", %dkB %s flash"
#endif
		, ci.model ? "ESP32" : "ESP8266"
		, ci.cores
		, ci.cores > 1 ? "s" : ""
		, ci.revision
		, (ci.features & CHIP_FEATURE_EMB_FLASH) ? ", embedded flash" : ""
		, (ci.features & CHIP_FEATURE_WIFI_BGN) ? ", WiFi" : ""
		, (ci.features & CHIP_FEATURE_BT) ? ", BT" : ""
		, (ci.features & CHIP_FEATURE_BLE) ? ", BLE" : ""
#if IDF_VERSION >= 32
		, spi_flash_get_chip_size() >> 10
		, (ci.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external"
#endif
		);
	log_info(TAG,"reset reason: %s",ResetReasons[(int)esp_reset_reason()]);

#ifdef ESP_IF_ETH
	uint8_t eth[6];
	if (ESP_OK == esp_wifi_get_eth(ESP_IF_ETH,eth)) {
		log_info(TAG,"ethernet MAC: %02x:%02x:%02x:%02x:%02x:%02x",eth[0],eth[1],eth[2],eth[3],eth[4],eth[5]);
	} else {
		log_info(TAG,"no ethernet MAC");
	}
#endif
#ifdef ESP_IF_BT
	uint8_t btmac[6];
	if (ESP_OK == esp_wifi_get_btmac(ESP_IF_BT,btmac)) {
		log_info(TAG,"bluetooth MAC: %02x:%02x:%02x:%02x:%02x:%02x",btmac[0],btmac[1],btmac[2],btmac[3],btmac[4],btmac[5]);
	} else {
		log_info(TAG,"no bluetooth MAC");
	}
#endif
}


extern "C"
void app_main()
{
	log_setup();
#ifdef CONFIG_SYSLOG
	dmesg_setup();
#endif
	log_info(TAG,"Atrium Firmware for ESP based systems");
	log_info(TAG,"Copyright 2019-2021, Thomas Maier-Komor, License: GPLv3");
	log_info(TAG,"Version: " VERSION);
	log_info(TAG,"IDF: %s",esp_get_idf_version());
	globals_setup();
	nvs_setup();
	diag_setup();

	system_info();

#ifdef CONFIG_IDF_TARGET_ESP8266
	gpio_install_isr_service(0);
#else
	gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM);
#endif
	settings_setup();
	event_setup();	// needed for most drivers including gpio
	gpio_setup();
	adc_setup();
#ifdef CONFIG_IDF_TARGET_ESP32
	hall_setup();
#endif

#ifdef CONFIG_STATUSLEDS
	status_setup();
#endif

#ifdef CONFIG_SUBTASKS
	cyclic_setup();
#endif
	init_fs();

#ifdef CONFIG_UART_CONSOLE
	console_setup();
#endif
#ifdef CONFIG_MQTT
	mqtt_setup();	// before any subscription occurs
#endif

#ifdef CONFIG_AT_ACTIONS
	alarms_setup();
#endif
#ifdef CONFIG_RELAY
	relay_setup();
#endif
#ifdef CONFIG_BUTTON
	button_setup();
#endif
#ifdef CONFIG_DIMMER
	dimmer_setup();
#endif
#ifdef CONFIG_BME280
	bme_setup();
#endif
#ifdef CONFIG_DHT
	dht_setup();
#endif
#ifdef CONFIG_TOUCHPAD
	touchpads_setup();
#endif
#ifdef CONFIG_DIST
	distance_setup();
#endif
#ifdef CONFIG_ONEWIRE
	ow_setup();
#endif

	wifi_setup();
	cfg_activate();
	uart_setup();
#ifdef CONFIG_SYSLOG
	syslog_setup();
#endif
	//BaseType_t r = xTaskCreate(settings_setup,"cfginit",4096,(void*)0,8,0);

#ifdef CONFIG_NIGHTSKY
	nightsky_setup();
#endif
#ifdef CONFIG_CLOCK
	clockapp_setup();
#endif
#ifdef CONFIG_LEDSTRIP
	ledstrip_setup();
#endif
#ifdef CONFIG_LIGHTCTRL
	lightctrl_setup();
#endif
#ifdef CONFIG_CAMERA
	webcam_setup();
#endif

	// client network services
#ifdef CONFIG_INFLUX
	influx_setup();
#endif
	
	// activate actions after all events and actions are setup
	// otherwise this step will fail
	cfg_activate_actions();

#ifdef CONFIG_SIGNAL_PROC
	cfg_init_functions();
#endif
	cfg_activate_triggers();

#ifdef CONFIG_UDPCTRL
	udpctrl_setup();
#endif

	// inetd bound services must be setup before inetd itself
#ifdef CONFIG_TELNET
	telnet_setup();
#endif
#ifdef CONFIG_HTTP
	httpd_setup();
#endif
#ifdef CONFIG_FTP
	ftpd_setup();
#endif
	inetd_setup();

#ifdef CONFIG_IDF_TARGET_ESP32
	esp_ota_mark_app_valid_cancel_rollback();
#endif
	log_info(TAG,"Atrium Version " VERSION);
}
