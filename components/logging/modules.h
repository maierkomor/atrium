/*
 *  Copyright (C) 2021-2023, Thomas Maier-Komor
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

#ifndef MODULES_H
#define MODULES_H

#include <stdint.h>
#include <string.h>

extern const char ModNames[];
extern const uint16_t ModNameOff[];


// enum definitions
typedef enum logmod_e {
	logmod_invalid = 0,
	logmod_action,
	logmod_adc,
	logmod_alarms,
	logmod_apds,
	logmod_bh1750,
	logmod_bmx,
	logmod_button,
	logmod_cam,
	logmod_ccs811b,
	logmod_cfg,
	logmod_con,
	logmod_cyclic,
	logmod_dht,
	logmod_dim,
	logmod_disp,
	logmod_ds18b20,
	logmod_event,
	logmod_fs,
	logmod_ftpd,
	logmod_gpio,
	logmod_hcsr04,
	logmod_hd44780u,
	logmod_hdc1000,
	logmod_hlw8012,
	logmod_ht16k33,
	logmod_http,
	logmod_i2c,
	logmod_ili9341,
	logmod_ina219,
	logmod_influx,
	logmod_init,
	logmod_led,
	logmod_ledc,
	logmod_log,
	logmod_lua,
	logmod_lwtcp,
	logmod_max7219,
	logmod_mcp230xx,
	logmod_mqtt,
	logmod_nightsky,
	logmod_ns,
	logmod_nvm,
	logmod_ota,
	logmod_owb,
	logmod_pca9685,
	logmod_pcf8574,
	logmod_relay,
	logmod_rgbleds,
	logmod_romfs,
	logmod_screen,
	logmod_sgp30,
	logmod_shell,
	logmod_si7021,
	logmod_sm,
	logmod_sntp,
	logmod_spi,
	logmod_ssd130x,
	logmod_sx1276,
	logmod_tca9555,
	logmod_telnet,
	logmod_ti,
	logmod_timefuse,
	logmod_tlc5916,
	logmod_tlc5947,
	logmod_tp,
	logmod_uart,
	logmod_udns,
	logmod_udpctrl,
	logmod_usb,
	logmod_wlan,
	logmod_ws2812,
	logmod_www,
	logmod_xio,
	logmod_xpt2046,
} logmod_t;

// module defines
#define MODULE_ACTION          logmod_action
#define MODULE_ADC             logmod_adc
#define MODULE_ALARMS          logmod_alarms
#define MODULE_APDS            logmod_apds
#define MODULE_BH1750          logmod_bh1750
#define MODULE_BMX             logmod_bmx
#define MODULE_BUTTON          logmod_button
#define MODULE_CAM             logmod_cam
#define MODULE_CCS811B         logmod_ccs811b
#define MODULE_CFG             logmod_cfg
#define MODULE_CON             logmod_con
#define MODULE_CYCLIC          logmod_cyclic
#define MODULE_DHT             logmod_dht
#define MODULE_DIM             logmod_dim
#define MODULE_DISP            logmod_disp
#define MODULE_DS18B20         logmod_ds18b20
#define MODULE_EVENT           logmod_event
#define MODULE_FS              logmod_fs
#define MODULE_FTPD            logmod_ftpd
#define MODULE_GPIO            logmod_gpio
#define MODULE_HCSR04          logmod_hcsr04
#define MODULE_HD44780U        logmod_hd44780u
#define MODULE_HDC1000         logmod_hdc1000
#define MODULE_HLW8012         logmod_hlw8012
#define MODULE_HT16K33         logmod_ht16k33
#define MODULE_HTTP            logmod_http
#define MODULE_I2C             logmod_i2c
#define MODULE_ILI9341         logmod_ili9341
#define MODULE_INA219          logmod_ina219
#define MODULE_INFLUX          logmod_influx
#define MODULE_INIT            logmod_init
#define MODULE_LED             logmod_led
#define MODULE_LEDC            logmod_ledc
#define MODULE_LOG             logmod_log
#define MODULE_LUA             logmod_lua
#define MODULE_LWTCP           logmod_lwtcp
#define MODULE_MAX7219         logmod_max7219
#define MODULE_MCP230XX        logmod_mcp230xx
#define MODULE_MQTT            logmod_mqtt
#define MODULE_NIGHTSKY        logmod_nightsky
#define MODULE_NS              logmod_ns
#define MODULE_NVM             logmod_nvm
#define MODULE_OTA             logmod_ota
#define MODULE_OWB             logmod_owb
#define MODULE_PCA9685         logmod_pca9685
#define MODULE_PCF8574         logmod_pcf8574
#define MODULE_RELAY           logmod_relay
#define MODULE_RGBLEDS         logmod_rgbleds
#define MODULE_ROMFS           logmod_romfs
#define MODULE_SCREEN          logmod_screen
#define MODULE_SGP30           logmod_sgp30
#define MODULE_SHELL           logmod_shell
#define MODULE_SI7021          logmod_si7021
#define MODULE_SM              logmod_sm
#define MODULE_SNTP            logmod_sntp
#define MODULE_SPI             logmod_spi
#define MODULE_SSD130X         logmod_ssd130x
#define MODULE_SX1276          logmod_sx1276
#define MODULE_TCA9555         logmod_tca9555
#define MODULE_TELNET          logmod_telnet
#define MODULE_TI              logmod_ti
#define MODULE_TIMEFUSE        logmod_timefuse
#define MODULE_TLC5916         logmod_tlc5916
#define MODULE_TLC5947         logmod_tlc5947
#define MODULE_TP              logmod_tp
#define MODULE_UART            logmod_uart
#define MODULE_UDNS            logmod_udns
#define MODULE_UDPCTRL         logmod_udpctrl
#define MODULE_USB             logmod_usb
#define MODULE_WLAN            logmod_wlan
#define MODULE_WS2812          logmod_ws2812
#define MODULE_WWW             logmod_www
#define MODULE_XIO             logmod_xio
#define MODULE_XPT2046         logmod_xpt2046
#define MAX_MODULE_ID           74
#define NUM_MODULES             75

#ifdef USE_MODULE
#define TAG USE_MODULE
#endif

#define MODULE_NAME (ModNames+ModNameOff[TAG])

#endif
