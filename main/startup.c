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

#ifdef ESP8266
#include <spi_flash.h>
#endif

#include <sdkconfig.h>

#include "actions.h"
#include "alive.h"
#include "bme.h"
#include "button.h"
#include "console.h"
#include "cyclic.h"
#include "dht.h"
#include "dimmer.h"
#include "distance.h"
#include "fs.h"
#include "ftpd.h"
#include "httpd.h"
#include "inetd.h"
#include "influx.h"
#include "clock.h"
#include "ledstrip.h"
#include "lightctrl.h"
#include "log.h"
#include "mqtt.h"
#include "nightsky.h"
#include "ota.h"
#include "relay.h"
#include "settings.h"
#include "telnet.h"
#include "uart_terminal.h"
#include "udpctrl.h"
#include "version.h"
#include "webcam.h"
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

static const char ResetReasons[][12] = {
	"unknown",
	"powerup",
	"external",
	"software",
	"panic",
	"internal_wd",
	"task_wd",
	"watchdog",
	"deepsleep",
	"brownout",
	"sdio",
};


void app_main()
{
#if CONFIG_CONSOLE_UART_NONE == 0
	uart_driver_install((uart_port_t)CONFIG_CONSOLE_UART_NUM,UART_FIFO_LEN*8,512,0,DRIVER_ARG);
	uart_write_bytes((uart_port_t)CONFIG_CONSOLE_UART_NUM,BootStr,sizeof(BootStr));
#endif
	log_setup();
	log_info(TAG,"Atrium Firmware for ESP based systems");
	log_info(TAG,"Copyright 2019-2020, Thomas Maier-Komor, License: GPLv3");
	log_info(TAG,"Version: " VERSION
#ifdef HG_REV
		", HgRev: " HG_REV
#endif
#ifdef HG_ID
		", HgId: " HG_ID
#endif
		);
	log_info(TAG,"reset reason: %s",ResetReasons[(int)esp_reset_reason()]);
	esp_chip_info_t ci;
	esp_chip_info(&ci);
	log_info(TAG,"%s with %d CPU %s, revision %d%s%s%s%s"
		, ci.model ? "ESP32" : "ESP8266"
		, ci.cores
		, ci.cores > 1 ? "cores" : "core"
		, ci.revision
		, (ci.features & CHIP_FEATURE_EMB_FLASH) ? ", embedded flash" : ""
		, (ci.features & CHIP_FEATURE_WIFI_BGN) ? ", WiFi" : ""
		, (ci.features & CHIP_FEATURE_BT) ? ", BT" : ""
		, (ci.features & CHIP_FEATURE_BLE) ? ", BLE" : ""
		);
#if IDF_VERSION >= 32
	log_info(TAG,"%dMB %s flash"
		, spi_flash_get_chip_size() / (1024 * 1024)
		, (ci.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
#endif
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
#ifdef ESP8266
	gpio_install_isr_service(0);
#else
	gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM);
#endif
	
	console_setup();
	nvs_setup();
	wifi_setup();
	init_fs();

#ifdef CONFIG_ALIVELED
	set_aliveled(1000);
	BaseType_t r = xTaskCreatePinnedToCore(&alive_task, "alive", 2048, NULL, 1, NULL, APP_CPU_NUM);
	assert(r == pdPASS);
#endif
	settings_setup();

#ifdef CONFIG_TERMSERV
	termserv_setup();
#endif
#ifdef CONFIG_SYSLOG
	syslog_setup();
#endif
#ifdef CONFIG_SUBTASKS
	subtasks_setup();
#endif
#ifdef CONFIG_AT_ACTIONS
	actions_setup();
#endif
#ifdef CONFIG_TELNET
	telnet_setup();
#endif
#ifdef CONFIG_FTP
	ftpd_setup();
#endif
#ifdef CONFIG_HTTP
	httpd_setup();
#endif
#ifdef CONFIG_INETD
	inetd_setup();
#endif
#ifdef CONFIG_UDPCTRL
	udpctrl_setup();
#endif
#ifdef CONFIG_MQTT
	mqtt_setup();
#endif
#ifdef CONFIG_DIST
	distance_setup();
#endif
#ifdef CONFIG_NIGHTSKY
	nightsky_setup();
#endif
#ifdef CONFIG_CLOCK
	clock_setup();
#endif
#ifdef CONFIG_LEDSTRIP
	ledstrip_setup();
#endif
#ifdef CONFIG_RELAY
	relay_setup();
#endif
#ifdef CONFIG_DHT
	dht_setup();
#endif
#ifdef CONFIG_BUTTON
	button_setup();
#endif
#ifdef CONFIG_CAMERA
	webcam_setup();
#endif
#ifdef CONFIG_DIMMER
	dimmer_setup();
#endif
#ifdef CONFIG_LIGHTCTRL
	lightctrl_setup();
#endif
#ifdef CONFIG_INFLUX
	influx_setup();
#endif
#ifdef CONFIG_BME280
	bme_setup();
#endif
	//esp_ota_mark_app_valid_cancel_rollback();
	log_info(TAG,"done");
}
