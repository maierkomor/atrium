/*
 *  Copyright (C) 2018-2023, Thomas Maier-Komor
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

#ifdef ESP8266
#ifdef ESP32
#error conflicting defines: ESP32 and ESP8266
#endif
#elif defined ESP32
#else
#error ESP familiy not set
#endif

extern "C" {
#ifndef ESP32
#include <esp_clk.h>
#endif
}
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <esp_err.h>

#ifdef ESP32
#include <esp_core_dump.h>
#endif

#include "cyclic.h"
#include "event.h"
#include "fs.h"
#include "globals.h"
#include "leds.h"
#include "log.h"
#include "mqtt.h"
#include "netsvc.h"
#include "nvm.h"
#include "ota.h"
#include "settings.h"
#include "support.h"
#include "syslog.h"
#include "uart_terminal.h"
#include "uarts.h"
#include "udns.h"
#include "env.h"
#include "wifi.h"

#if IDF_VERSION >= 50
#include <rom/ets_sys.h>
#include <esp_chip_info.h>
#endif

#ifdef CONFIG_IDF_TARGET_ESP8266
#include <spi_flash.h>
#include <driver/rtc.h>
#elif defined CONFIG_IDF_TARGET_ESP32
#include <soc/rtc.h>
#elif defined CONFIG_IDF_TARGET_ESP32S3
#include <driver/usb_serial_jtag.h>
#endif

#ifdef CONFIG_VERIFY_HEAP
#define verify_heap() if (!heap_caps_check_integrity_all(true)){printf("corrupt heap\n");heap_caps_dump_all();abort();}
#else
#define verify_heap()
#endif

#define TAG MODULE_INIT

#ifdef CONFIG_FREERTOS_UNICORE
#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S3
//#error multi-core supported required on multi-core micro-controllers
#endif
#endif

using namespace std;

void action_setup();
void adc_setup();
void alarms_setup();
void button_setup();
void buzzer_setup();
void console_setup();
void cyclic_setup();
void dht_setup();
void dimmer_setup();
void display_setup();
void distance_setup();
void ftpd_setup();
void gpio_setup();
void hall_setup();
void httpd_setup();
void i2c_setup();
void inetd_setup();
void influx_setup();
void rgbleds_setup();
int lightctrl_setup();
void lora_setup();
void ow_setup();
void relay_setup();
void screen_setup();
void sm_setup();
void sm_start();
void spi_setup();
void telnet_setup();
void timefuse_setup();
void tlc5947_setup();
int touchpads_setup();
void udns_setup();
void usb_setup();
void xlua_setup();
void udpctrl_setup();
int webcam_setup();
void xio_setup();

void settings_setup();


static void system_info()
{
#if IDF_VERSION >= 50
	uint32_t mhz = ets_get_cpu_frequency();
#else
	int f = esp_clk_cpu_freq();
	unsigned mhz;
	switch (f) {
#ifdef RTC_CPU_FREQ_80M
	case RTC_CPU_FREQ_80M:
		mhz = 80;
		break;
#endif
#ifdef RTC_CPU_FREQ_160M
	case RTC_CPU_FREQ_160M:
		mhz = 160;
		break;
#endif

#ifdef CONFIG_IDF_TARGET_ESP32
	case RTC_CPU_FREQ_XTAL:
		mhz = rtc_clk_xtal_freq_get();
		break;
	case RTC_CPU_FREQ_240M:
		mhz = 240;
		break;
	case RTC_CPU_FREQ_2M:
		mhz = 2;
		break;
#endif
	default:
		mhz = f/1000000;
	}
#endif
	esp_chip_info_t ci;
	esp_chip_info(&ci);
	log_info(TAG,"ESP%u (rev %d) with %d core%s @ %uMHz"
		, ci.model ? 32 : 8266
		, ci.revision
		, ci.cores
		, ci.cores > 1 ? "s" : ""
		, mhz
		);
#ifdef CONFIG_SOC_SPIRAM_SUPPORTED
	if (uint32_t spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM))
		log_info(TAG,"%ukB SPI RAM",spiram>>10);
#endif
	log_info(TAG,"%s reset",ResetReasons[(int)esp_reset_reason()]);

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


static int count_name(EnvObject *o, const char *name)
{
	unsigned n = 0;
	for (EnvElement *e : o->getChilds()) {
		assert(e);
		if (EnvObject *c = e->toObject()) {
			n += count_name(c,name);
		} else if (0 == strcmp(e->name(),name)) {
			++n;
		}
	}
	return n;
}


static int has_dups(EnvObject *o)
{
	for (EnvElement *e : o->getChilds()) {
		if (EnvObject *c = e->toObject()) {
			if (has_dups(c))
				return 1;
		} else if (1 < count_name(RTData,e->name())) {
			return 1;
		}
	}
	return 0;
}


static void env_setup()
{
	if (has_dups(RTData))
		return;
	unsigned i = 0;
	while (EnvElement *x = RTData->getChild(i)) {
		++i;
		if (0 == strcmp(x->name(),"mqtt"))
			continue;
		if (EnvObject *c = x->toObject()) {
			for (EnvElement *m : c->getChilds())
				RTData->add(m);
		}
	}
}


#ifdef CONFIG_VERIFY_HEAP
unsigned cyclic_check_heap(void *)
{
	assert(heap_caps_check_integrity_all(true));
	return 5000;
}
#endif

extern "C"
void app_main()
{
	verify_heap();
	log_setup();		// init CONFIG_CONSOLE_UART_NUM, logging mutex, etc
	event_init();		// event mutex, etc
#ifdef CONFIG_SYSLOG
	dmesg_setup();		// syslog buffer, using syslog`msg event
#endif
	globals_setup();	// init HWConf, Config, RTData with defaults
	if (0 == nvm_setup())		// read HWConf
		cfg_read_hwcfg();
	verify_heap();

	log_info(TAG,"Atrium Firmware for ESP based systems");
	log_info(TAG,"%s, %s",Copyright,License);
	log_info(TAG,"Version %s, IDF %s",Version,esp_get_idf_version());
	system_info();
#ifdef CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
	esp_core_dump_init();
#endif

	action_setup();
	timefuse_setup();
	settings_setup();
	verify_heap();
	uart_setup();		// init configured uarts, set diag uart
	cyclic_setup();
	verify_heap();
#ifdef CONFIG_VERIFY_HEAP
	cyclic_add_task("check_heap",cyclic_check_heap,0);
#endif

	gpio_setup();	// init core GPIOs
#ifdef CONFIG_I2C
	i2c_setup();	// must be before xio_setup to provide io-cluster
#endif
#ifdef CONFIG_ESP_PHY_ENABLE_USB
	usb_setup();
#endif
#ifdef CONFIG_SPI
	spi_setup();	// must be before xio_setup to provide io-cluster
#endif
	xio_setup();
#ifdef CONFIG_BUZZER
	buzzer_setup();
#endif
	verify_heap();
	adc_setup();
#ifdef CONFIG_IDF_TARGET_ESP32
	hall_setup();
#endif

	verify_heap();
#ifdef CONFIG_AT_ACTIONS
	alarms_setup();	// must be bevore status_setup, as status reads alarm info
#endif
#ifdef CONFIG_LEDS
	leds_setup();
#endif
	fs_init();

#ifdef CONFIG_UART_CONSOLE
	console_setup();
#endif
	verify_heap();

	event_start();
#ifdef CONFIG_RELAY
	relay_setup();
#endif
#ifdef CONFIG_BUTTON
	button_setup();
#endif
#ifdef CONFIG_DIMMER
	dimmer_setup();
#endif
#ifdef CONFIG_DHT
	dht_setup();
#endif
#ifdef CONFIG_TOUCHPAD
	touchpads_setup();
#endif
#ifdef CONFIG_HCSR04
	distance_setup();
#endif
#ifdef CONFIG_ONEWIRE
	ow_setup();
#endif
	verify_heap();

#ifdef CONFIG_UDNS
	udns_setup();
#endif
	verify_heap();
	wifi_setup();
	verify_heap();
	cfg_activate();
	verify_heap();
#ifdef CONFIG_UDNS
	mdns_setup();
#endif
	verify_heap();
#ifdef CONFIG_MQTT
	mqtt_setup();	// in case of no subscriptions
#endif
#ifdef CONFIG_INFLUX
	influx_setup();
#endif
	verify_heap();
	env_setup();
	sntp_setup();

#ifdef CONFIG_SYSLOG
	syslog_setup();
#endif

#ifdef CONFIG_TLC5947
	tlc5947_setup();
#endif
#ifdef CONFIG_RGBLEDS
	rgbleds_setup();
#endif
#ifdef CONFIG_LIGHTCTRL
	lightctrl_setup();
#endif
#ifdef CONFIG_CAMERA
	webcam_setup();
#endif
#ifdef CONFIG_LORA
	lora_setup();
#endif
	cfg_init_timers();


#ifdef CONFIG_LUA
	xlua_setup();
#endif
#ifdef CONFIG_DISPLAY
	display_setup();
	screen_setup();
#endif // CONFIG_DISPLAY

#ifdef CONFIG_STATEMACHINES
	sm_setup();
#endif

	// activate actions after all events and actions are setup
	// otherwise this step will fail
	//cfg_activate_actions();	-- removed
	// hardware init finished, no reset
	nvm_store_u8("hwconf",0);
	// here all actions and events must be initialized
	cfg_activate_triggers();

#ifdef CONFIG_STATEMACHINES
	sm_start();
#endif

	// network listening services
#ifdef CONFIG_UDPCTRL
	udpctrl_setup();
#endif
#ifdef CONFIG_TELNET
	telnet_setup();
#endif
#ifdef CONFIG_HTTP
	httpd_setup();
#endif
#ifdef CONFIG_FTP
	ftpd_setup();
#endif

#ifdef CONFIG_SOCKET_API
	inetd_setup();
#endif
#ifdef CONFIG_IDF_TARGET_ESP32
	esp_ota_mark_app_valid_cancel_rollback();
#endif
	log_info(TAG,"Atrium Version %s",Version);
	event_trigger(event_id("init`done"));
}
