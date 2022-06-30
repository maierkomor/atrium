/*
 *  Copyright (C) 2021, Thomas Maier-Komor
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
	logmod_action         =   1,
	logmod_adc            =   2,
	logmod_alarms         =   3,
	logmod_apds           =   4,
	logmod_bh1750         =   5,
	logmod_bmx            =   6,
	logmod_button         =   7,
	logmod_cam            =   8,
	logmod_ccs811b        =   9,
	logmod_cfg            =  10,
	logmod_con            =  11,
	logmod_cyclic         =  12,
	logmod_dht            =  13,
	logmod_dim            =  14,
	logmod_disp           =  15,
	logmod_ds18b20        =  16,
	logmod_event          =  17,
	logmod_fs             =  18,
	logmod_ftpd           =  19,
	logmod_func           =  20,
	logmod_gpio           =  21,
	logmod_hcsr04         =  22,
	logmod_hd44780u       =  23,
	logmod_hdc1000        =  24,
	logmod_ht16k33        =  25,
	logmod_http           =  26,
	logmod_i2c            =  27,
	logmod_inetd          =  28,
	logmod_influx         =  29,
	logmod_init           =  30,
	logmod_led            =  31,
	logmod_ledc           =  32,
	logmod_ledstrip       =  33,
	logmod_log            =  34,
	logmod_lwtcp          =  35,
	logmod_max7219        =  36,
	logmod_mcp230xx       =  37,
	logmod_mqtt           =  38,
	logmod_nightsky       =  39,
	logmod_ns             =  40,
	logmod_ota            =  41,
	logmod_owb            =  42,
	logmod_pca9685        =  43,
	logmod_pcf8574        =  44,
	logmod_relay          =  45,
	logmod_romfs          =  46,
	logmod_screen         =  47,
	logmod_sgp30          =  48,
	logmod_shell          =  49,
	logmod_signal         =  50,
	logmod_sm             =  51,
	logmod_sntp           =  52,
	logmod_ssd1306        =  53,
	logmod_telnet         =  54,
	logmod_ti             =  55,
	logmod_timefuse       =  56,
	logmod_tlc5916        =  57,
	logmod_tlc5947        =  58,
	logmod_tp             =  59,
	logmod_uart           =  60,
	logmod_udns           =  61,
	logmod_udpctrl        =  62,
	logmod_wlan           =  63,
	logmod_ws2812         =  64,
	logmod_www            =  65,
	logmod_xio            =  66,
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
#define MODULE_FUNC            logmod_func
#define MODULE_GPIO            logmod_gpio
#define MODULE_HCSR04          logmod_hcsr04
#define MODULE_HD44780U        logmod_hd44780u
#define MODULE_HDC1000         logmod_hdc1000
#define MODULE_HT16K33         logmod_ht16k33
#define MODULE_HTTP            logmod_http
#define MODULE_I2C             logmod_i2c
#define MODULE_INETD           logmod_inetd
#define MODULE_INFLUX          logmod_influx
#define MODULE_INIT            logmod_init
#define MODULE_LED             logmod_led
#define MODULE_LEDC            logmod_ledc
#define MODULE_LEDSTRIP        logmod_ledstrip
#define MODULE_LOG             logmod_log
#define MODULE_LWTCP           logmod_lwtcp
#define MODULE_MAX7219         logmod_max7219
#define MODULE_MCP230XX        logmod_mcp230xx
#define MODULE_MQTT            logmod_mqtt
#define MODULE_NIGHTSKY        logmod_nightsky
#define MODULE_NS              logmod_ns
#define MODULE_OTA             logmod_ota
#define MODULE_OWB             logmod_owb
#define MODULE_PCA9685         logmod_pca9685
#define MODULE_PCF8574         logmod_pcf8574
#define MODULE_RELAY           logmod_relay
#define MODULE_ROMFS           logmod_romfs
#define MODULE_SCREEN          logmod_screen
#define MODULE_SGP30           logmod_sgp30
#define MODULE_SHELL           logmod_shell
#define MODULE_SIGNAL          logmod_signal
#define MODULE_SM              logmod_sm
#define MODULE_SNTP            logmod_sntp
#define MODULE_SSD1306         logmod_ssd1306
#define MODULE_TELNET          logmod_telnet
#define MODULE_TI              logmod_ti
#define MODULE_TIMEFUSE        logmod_timefuse
#define MODULE_TLC5916         logmod_tlc5916
#define MODULE_TLC5947         logmod_tlc5947
#define MODULE_TP              logmod_tp
#define MODULE_UART            logmod_uart
#define MODULE_UDNS            logmod_udns
#define MODULE_UDPCTRL         logmod_udpctrl
#define MODULE_WLAN            logmod_wlan
#define MODULE_WS2812          logmod_ws2812
#define MODULE_WWW             logmod_www
#define MODULE_XIO             logmod_xio
#define MAX_MODULE_ID           66
#define NUM_MODULES             67

#ifdef USE_MODULE
#define TAG USE_MODULE
#endif

#define MODULE_NAME (ModNames+ModNameOff[TAG])

#endif
