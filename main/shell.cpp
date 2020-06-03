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

#include "actions.h"
#include "alive.h"
#include "console.h"
#include "dht.h"
#include "fs.h"
#include "globals.h"
#include "log.h"
#include "ota.h"
#include "mqtt.h"
#include "profiling.h"
#include "romfs.h"
#include "settings.h"
#include "support.h"
#include "terminal.h"
#include "termstream.h"
#include "wifi.h"

#include <string>
#include <sstream>

#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_wifi.h>

#ifdef CONFIG_SMARTCONFIG
#include <esp_smartconfig.h>
#endif

#ifdef CONFIG_SPIFFS
#include <esp_spiffs.h>
#elif defined CONFIG_FATFS
#include <esp_vfs_fat.h>
#endif

#ifdef CONFIG_FATFS
#include <ff.h>
#endif

#ifndef CONFIG_ROMFS
#ifdef ESP32
#include <esp_spi_flash.h>
#else
#include <spi_flash.h>
#endif
#endif

#if defined CONFIG_FATFS || defined CONFIG_SPIFFS
#define HAVE_FS
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/FreeRTOSConfig.h>
#include <freertos/task.h>

#ifdef ESP32
#include <lwip/err.h>
#include <lwip/dns.h>
#elif defined ESP8266
#if IDF_VERSION > 32
#include <driver/rtc.h>	// post v3.2
#endif
#include <driver/hw_timer.h>
extern "C" {
#include <esp_clk.h>
}
#endif

#include <netdb.h>

#ifdef ESP8266
#include <apps/sntp/sntp.h>
#elif IDF_VERSION >= 32
#include <lwip/apps/sntp.h>	// >= v3.2
#else
#include <apps/sntp/sntp.h>	// <= v3.1
#endif
#include <driver/adc.h>
#include <driver/uart.h>

#ifdef ESP32
#if IDF_VERSION >= 40
extern "C" {
#include <esp32/clk.h>
}
#include <soc/rtc.h>
#else
extern "C" {
#include <esp_clk.h>
}
#include <soc/rtc.h>
#endif
#elif defined ESP8266
#include <driver/gpio.h>
#endif

#include <dirent.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#ifdef write
#undef write
#endif

using namespace std;

struct ExeName
{
	char name[15];
	uint8_t plvl;	// 0 = no priviliges required, 1 = user admin required
	int (*function)(Terminal &term, int argc, const char *arg[]);
	const char *descr;
};

static string PWD = "/flash/";
static const char PW[] = "password:", NotSet[] = "<not set>", Denied[] = "\nAccess denied.\n";


extern "C"
const char *getpwd()
{
	return PWD.c_str();
}


#ifdef ESP32
static int shell_cd(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		term.printf("%s: 0-1 argument expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1) {
		term.printf("%s\n",PWD.c_str());
		return 0;
	}
	if (args[1][0] == '/')
		PWD = args[1];
	else
		PWD += args[1];
	if (PWD.c_str()[PWD.size()-1] != '/')
		PWD += '/';
	return 0;
}
#endif


#ifdef HAVE_FS
static int shell_rm(Terminal &term, int argc, const char *args[])
{
	if (argc < 2) {
		term.printf("rm: at least 1 argument expected, got %u\n",argc-1);
		return 1;
	}
	int err = 0;
	int a = 1;
	while (a < argc) {
		string fn;
		if (args[a][0] != '/')
			fn = PWD;
		fn += args[a];
		if (-1 == unlink(fn.c_str())) {
			term.printf("failed to remove '%s': %s\n",args[a],strerror(errno));
			err = 1;
		}
		++a;
	}
	return err;
}
#endif


#ifdef CONFIG_FATFS
static int shell_mkdir(Terminal &term, int argc, const char *args[])
{
	if (argc != 2) {
		term.printf("%s: 2 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	string dn;
	if (args[1][0] != '/')
		dn = PWD;
	dn += args[1];
	if (-1 == mkdir(dn.c_str(),0777)) {
		term.printf("error creating directory %s: %s\n",dn.c_str(),strerror(errno));
		return 1;
	}
	return 0;
}


static int shell_rmdir(Terminal &term, int argc, const char *args[])
{
	if (argc != 2) {
		term.printf("%s: 2 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	string dn;
	if (args[1][0] != '/')
		dn = PWD;
	dn += args[1];
	if (-1 == rmdir(dn.c_str())) {
		term.printf("error removing directory %s: %s\n",dn.c_str(),strerror(errno));
		return 1;
	}
	return 0;
}


static int shell_mv(Terminal &term, int argc, const char *args[])
{
	if (argc != 3) {
		term.printf("mv: 2 arguments expected, got %u\n",argc-1);
		return 1;
	}
	string fn0,fn1;
	if (args[1][0] != '/')
		fn0 = PWD;
	fn1 += args[1];
	if (args[2][0] != '/')
		fn1 = PWD;
	fn1 += args[2];
	int r = rename(fn0.c_str(),fn1.c_str());
	if (r == 0)
		return 0;
	if ((r == -1) && (errno != ENOTSUP)) {
		term.printf("failed to rename '%s' to '%s': %s\n",args[1],args[2],strerror(errno));
		return 1;
	}
	if (-1 == link(args[1],args[2])) {
		term.printf("failed to link '%s' to '%s': %s\n",args[1],args[2],strerror(errno));
		return 1;
	}
	if (-1 == unlink(args[1])) {
		term.printf("failed to unlink '%s': %s\n",args[1],strerror(errno));
		return 1;
	}
	return 0;
}
#endif


static int shell_ls(Terminal &term, int argc, const char *args[])
{
#ifdef HAVE_FS
	if (argc == 1) {
		string pwd = PWD;
		if (pwd.back() == '/')
			pwd.resize(pwd.size()-1);
		DIR *d = opendir(pwd.c_str());
		if (d == 0) {
			term.printf("unable to opendir %s: %s\n",pwd.c_str(),strerror(errno));
			return 1;
		}
		while (struct dirent *e = readdir(d))
			term.printf("\t%s\n",e->d_name);
		closedir(d);
		return 0;
	}
	bool nlst = false;
	int ret = 0;
	int a = 1;
	if (!strcmp(args[1],"-1")) {
		++a;
		nlst = true;
	}
	while (a < argc) {
		string dir;
		if (args[a][0] != '/')
			dir = PWD;
		dir += args[a];
		if (dir.back() == '/')
			dir.resize(dir.size()-1);
		++a;
		DIR *d = opendir(dir.c_str());
		if (d == 0) {
			term.printf("unable to open dir %s: %s\n",dir.c_str(),strerror(errno));
			ret = 1;
			continue;
		}
		if (dir.c_str()[dir.size()-1] != '/')
			dir += '/';
		while (struct dirent *e = readdir(d)) {
			string f = dir;
			f += e->d_name;
			if (nlst) {
				term.printf("%s\n",e->d_name);
				continue;
			}
			struct stat st;
			if (stat(f.c_str(),&st) != 0) {
				term.printf("%s\t<%s>\n",e->d_name,strerror(errno));
				continue;
			}
			if ((st.st_mode & S_IFDIR) == S_IFDIR)
				term.printf("%s\t<DIR>\n",e->d_name);
			else
				term.printf("%s\t%6u\n",e->d_name,st.st_size);
		}
		closedir(d);
	}
	return ret;
#elif defined CONFIG_ROMFS
#ifndef ROMFS_PARTITION
#define ROMFS_PARTITION 0
#endif
	unsigned n = romfs_num_entries();
	for (int i = 0; i < n; ++i) {
		const char *n = romfs_name(i);
		assert(n);
		size_t s = romfs_size(i);
		size_t o = romfs_offset(i);
		term.printf("\t%-12s %6u\n",n,s,o);
	}
	return 0;
#else
	term.printf("not implemented\n");
	return 1;
#endif
}


static int shell_cat(Terminal &term, int argc, const char *args[])
{
	if (argc != 2) {
		term.printf("%s: 1 argument expected, got %u\n",args[0],argc-1);
		return 1;
	}
#ifdef HAVE_FS
	char *filename;
	if (args[1][0] == '/') {
		filename = strdup(args[1]);
	} else {
		size_t p = PWD.size();
		size_t a = strlen(args[1]);
		filename = (char *) malloc(a+p+1);
		if (filename == 0)
			return 1;
		memcpy(filename,PWD.data(),p);
		memcpy(filename+p,args[1],a+1);
	}
	int fd = open(filename,O_RDONLY);
	free(filename);
	if (fd < 0) {
		term.printf("open(%s): %s\n",args[1],strerror(errno));
		return 1;
	}
	struct stat st;
	if (0 != fstat(fd,&st)) {
		close(fd);
		term.printf("stat(%s): %s\n",args[1],strerror(errno));
		return 1;
	}
	char buf[512];
	int t = 0;
	do {
		int n = read(fd,buf,sizeof(buf));
		if (n == -1) {
			close(fd);
			term.printf("read(%s): %s\n",args[1],strerror(errno));
			return 1;
		}
		t += n;
		term.print(buf,n);
	} while (t < st.st_size);
	close(fd);
	return 0;
#elif defined CONFIG_ROMFS
	const char *filename = args[1];
	if (filename[0] == '/')
		++filename;
	int r = romfs_open(filename);
	if (r < 0)
		return 1;
	size_t s = romfs_size(r);
	size_t a = 512 < s ? 512 : s;
	char tmp[a];
	size_t num = 0, off = 0;
	while (num < s) {
		size_t bs = s-off < a ? s-off : a;
		romfs_read_at(r,tmp,bs,off);
		off += bs;
		num += bs;
		term.print(tmp,bs);
	}
	return 0;
#else
	term.printf("not implemented\n");
	return 1;
#endif
}


void print_hex(Terminal &term, uint8_t *b, size_t s, size_t off = 0)
{
	uint8_t *a = b, *e = b + s;
	while (a+16 <= e) {
		term.printf("%04x:  %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x\r\n"
				, a-b+off
				, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]
				, a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
		a += 16;
	}
	if (a == e)
		return;
	char tmp[64], *t = tmp;
	t += sprintf(t,"%04x: ",a-b+off);
	int i = 0;
	while (a < e) {
		t += sprintf(t,"%s%02x",i == 8 ? "  " : " ",*a);
		++i;
		++a;
	}
	*t++ = '\r';
	*t++ = '\n';
	*t = 0;
	term.print(tmp);
}


#ifdef HAVE_FS
static int shell_touch(Terminal &term, int argc, const char *args[])
{
	if (argc != 2) {
		term.printf("%s: 1 argument expected, got %u\n",args[0],argc-1);
		return 1;
	}
	int fd;
	if (args[1][0] == '/') {
		fd = creat(args[1],0666);
	} else {
		char *path = (char *)malloc(PWD.size()+strlen(args[1])+1);
		strcpy(path,PWD.c_str());
		strcat(path,args[1]);
		fd = creat(path,0666);
		free(path);
	}
	if (fd == -1) {
		term.printf("unable to creat file %s: %s\n",args[1],strerror(errno));
		return 1;
	}
	close(fd);
	return 0;
}

	
static int shell_xxd(Terminal &term, int argc, const char *args[])
{
	if (argc != 2) {
		term.printf("%s: 1 argument expected, got %u\n",args[0],argc-1);
		return 1;
	}
	int fd = open(args[1],O_RDONLY);
	if (fd == -1) {
		term.printf("unable to open file %s: %s\n",args[1],strerror(errno));
		return 1;
	}
	struct stat st;
	if (-1 == fstat(fd,&st)) {
		close(fd);
		term.printf("unable to open stat %s: %s\n",args[1],strerror(errno));
		return 1;
	}
	uint8_t buf[64];
	int n = read(fd,buf,sizeof(buf));
	size_t off = 0;
	while (n > 0) {
		print_hex(term,buf,n,off);
		n = read(fd,buf,sizeof(buf));
		off += n;
	}
	close(fd);
	return 0;
}
#endif


static int shell_reboot(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		term.printf("reboot: 0 arguments expected, got %u\n",argc-1);
		return 1;
	}
	esp_restart();
	return 0;
}


#ifdef ESP32
static int restore(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		term.printf("%s: 0 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	esp_wifi_restore();
	return 0;
}
#endif


#if defined CONFIG_SPIFFS || defined CONFIG_FATFS
static int shell_df(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		term.printf("df: 0 arguments expected, got %u\n",argc-1);
		return 1;
	}
#ifdef CONFIG_SPIFFS
	size_t total = 0, used = 0;
	esp_err_t ret = esp_spiffs_info("storage", &total, &used);
	if (ret == ESP_OK) {
		term.printf("SPIFFS: %ukiB of %ukB used\n",used>>10,total>>10);
		return 0;
	}
	term.printf("error getting SPIFFS information: %s\n",esp_err_to_name(ret));
#elif defined CONFIG_FATFS
	unsigned long nc = 0;
	FATFS *fs;
	int err = f_getfree(PWD.c_str(),&nc,&fs);
	if (err == 0) {
		term.printf("%s: %lu kiB free\n",PWD.c_str(),nc*fs->csize*fs->ssize>>10);
		return 0;
	}
	term.printf("error getting information: %d\n",err);
#endif
	return 1;
}
#endif


static int part(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		term.printf("%s: 0 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	esp_partition_iterator_t i = esp_partition_find(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_ANY,0);
	while (i) {
		const esp_partition_t *p = esp_partition_get(i);
		term.printf("%s %02x %7d@%08x %s\n",p->type ? "data" : "app ", p->subtype, p->size, p->address, p->label);
		i = esp_partition_next(i);
	}
	i = esp_partition_find(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,0);
	while (i) {
		const esp_partition_t *p = esp_partition_get(i);
		term.printf("%s %02x %7d@%08x %s\n",p->type ? "data" : "app ", p->subtype, p->size, p->address, p->label);
		i = esp_partition_next(i);
	}
	return 0;
}


static int chipid(Terminal &term, int argc, const char *args[])
{
	esp_chip_info_t ci;
	esp_chip_info(&ci);
	term.printf("chip rev: %d\n",ci.revision);
	if (ci.features & CHIP_FEATURE_EMB_FLASH)
		term.printf("has embedded flash\n");
	if (ci.features & CHIP_FEATURE_WIFI_BGN)
		term.printf("has 2.4GHz WiFi\n");
	if (ci.features & CHIP_FEATURE_BT)
		term.printf("has Bluetooth\n");
	if (ci.features & CHIP_FEATURE_BLE)
		term.printf("has Bluetooth LE\n");
	return 0;
}


static int mem(Terminal &term, int argc, const char *args[])
{
	term.printf("32bit mem   : %u\n",heap_caps_get_free_size(MALLOC_CAP_32BIT));
	term.printf("8bit mem    : %u\n",heap_caps_get_free_size(MALLOC_CAP_8BIT));
	term.printf("DMA  mem    : %u\n",heap_caps_get_free_size(MALLOC_CAP_DMA));
#ifdef ESP32
	term.printf("exec mem    : %u\n",heap_caps_get_free_size(MALLOC_CAP_EXEC));
	term.printf("SPI  mem    : %u\n",heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
	term.printf("internal mem: %u\n",heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif
	return 0;
}


int mac(Terminal &term, int argc, const char *args[])
{
	uint8_t mac[6];
	if (argc == 1) {
		term.printf(	"mac -l      : list mac addresses\n"
				"mac -s <mac>: set station mac\n"
				"mac -a <mac>: set softap mac\n"
				"mac -c      : clear mac settings\n"
			   );
		return 0;
	} else if (argc == 2) {
		if (!strcmp("-l",args[1])) {
			if (ESP_OK == esp_wifi_get_mac(ESP_IF_WIFI_AP,mac))
				term.printf("softap mac:   %02x:%02x:%02x:%02x:%02x:%02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
			if (ESP_OK == esp_wifi_get_mac(ESP_IF_WIFI_STA,mac))
				term.printf("station mac:  %02x:%02x:%02x:%02x:%02x:%02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
#ifdef ESP32
			if (ESP_OK == esp_wifi_get_mac(ESP_IF_ETH,mac))
				term.printf("ethernet mac: %02x:%02x:%02x:%02x:%02x:%02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
#endif
			return 0;
		}
	}
	if (0 == term.getPrivLevel()) {
		term.printf("Insufficient privileges - execute 'su 1' to gain access.\n");
		return 1;
	}
	// priveleged commands
	if (argc == 2) {
		if (!strcmp("-c",args[1])) {
			Config.mutable_station()->clear_mac();
			Config.mutable_softap()->clear_mac();
			return 0;
		}
	} else if (argc == 3) {
		unsigned inp[6];
		if (6 != sscanf(args[2],"%x:%x:%x:%x:%x:%x",inp+0,inp+1,inp+2,inp+3,inp+4,inp+5)) {
			term.printf("invalid mac\n");
			return 1;
		}
		for (int i = 0; i < 6; ++i) {
			if (inp[i] & ~0xff) {
				term.printf("mac element %d out of range",i);
				return 1;
			}
			mac[i] = inp[i];
		}
		wifi_interface_t w;
		if (!strcmp("-s",args[1])) {
			w = ESP_IF_WIFI_AP;
			Config.mutable_station()->set_mac(mac,6);
		}
		else if (!strcmp("-a",args[1])) {
			w = ESP_IF_WIFI_AP;
			Config.mutable_softap()->set_mac(mac,6);
		} else {
			return 1;
		}
		if (esp_err_t e = esp_wifi_set_mac(w,mac)) {
			term.printf("error setting mac: %s",esp_err_to_name(e));
			return 1;
		}
		return 0;
	}
	return 1;
}


static int adc(Terminal &term, int argc, const char *args[])
{
#ifdef ESP32
	if (argc != 2) {
		term.printf("%s: 1 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	long channel = strtol(args[1],0,0);
	if ((channel < 0) || (channel > 7) || ((channel == 0) && (errno != 0))) {
		term.printf("%s: argument %s out of range\n",args[1]);
		return 1;
	}
	adc1_config_width(ADC_WIDTH_BIT_12);
	adc1_config_channel_atten((adc1_channel_t)channel, ADC_ATTEN_DB_0);
	term.printf("adc1[%d] = %d\n",(adc1_channel_t)channel,adc1_get_raw((adc1_channel_t)channel));
	return 0;
#elif defined ESP8266
	if (argc != 1) {
		term.printf("%s: 0 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
#if IDF_VERSION >= 32	// and esp8266/master
	uint16_t adc;
	adc_read(&adc);
	term.printf("adc = %u\n",adc);
	return 0;
#else	// esp8266/v3.1
	term.printf("adc = %u\n",adc_read());
	return 0;
#endif
#else
	term.printf("not implemented\n");
	return 1;
#endif
}


#ifdef CONFIG_OTA
static int boot(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		term.printf("%s: 0-1 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1) {
		const esp_partition_t *b = esp_ota_get_boot_partition();
		const esp_partition_t *r = esp_ota_get_running_partition();
		const esp_partition_t *u = esp_ota_get_next_update_partition(NULL);
		term.printf("booting from: %s\n"
			    "running from: %s\n"
			    "updating to : %s\n"
			    , b ? b->label : "<error>"
			    , r ? r->label : "<error>"
			    , u ? u->label : "<error>"
			    );
		return b && r ? 0 : 1;
	}
	esp_partition_iterator_t i = esp_partition_find(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_ANY,0);
	while (i) {
		const esp_partition_t *p = esp_partition_get(i);
		if (0 == strcmp(p->label,args[1])) {
			esp_ota_set_boot_partition(p);
			esp_partition_iterator_release(i);
			return 0;
		}
		i = esp_partition_next(i);
	}
	printf("error: unable to find and set boot partition to %s",args[1]);
	return 1;
}
#endif


#ifdef ESP32
static int hall(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		term.printf("%s: 0 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	adc1_config_width(ADC_WIDTH_BIT_12);
	term.printf("hall sensor = %d\n",hall_sensor_read());
	return 0;
}
#endif


static int print_setting(Terminal &t, const char *name)
{
	if (0 == strcmp(name,"ap_ssid"))
		t.printf("%s\n",Config.softap().ssid().c_str());
	else if (0 == strcmp(name,"ap_pass"))
		t.printf("%s\n",Config.softap().pass().c_str());
	else if (0 == strcmp(name,"ap_active"))
		t.printf("%s\n",Config.softap().activate() ? "on" : "off");
	//else if (0 == strcmp(name,"debug"))
		//t.printf("%s\n", Config.Debug() ? "on" : "off");
	else if (0 == strcmp(name,"max_on_time"))
		t.printf("%u\n",Config.max_on_time());
#ifdef CONFIG_MQTT
	else if (0 == strcmp(name,"mqtt_uri"))
		t.printf("%s\n",Config.mqtt().uri().c_str());
	else if (0 == strcmp(name,"mqtt_enable"))
		t.printf("%s\n",Config.mqtt().enable() ? "true" : "false");
#endif
	else if (0 == strcmp(name,"station_ssid"))
		t.printf("%s\n",Config.station().ssid().c_str());
	else if (0 == strcmp(name,"station_pass"))
		t.printf("%s\n",Config.station().pass().c_str());
	else if (0 == strcmp(name,"station_active"))
		t.printf("%s\n",Config.station().activate() ? "on" : "off");
	else
		return 1;
	return 0;
}


static int set(Terminal &t, int argc, const char *args[])
{
	if (argc > 4) {
		t.printf("%s: not more than 3 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 2) {
		if (0 == strcmp("-l",args[1])) {
			for (size_t i = 0; SetFns[i].first; ++i)
				t.printf("%s\n",SetFns[i].first);
			return 0;
		}
		return print_setting(t,args[1]);
	} else if (argc == 3) {
		if (0 == strcmp(args[1],"-c")) {
			if (0 == change_setting(args[2],0))
				return 0;
		} else if (0 == strcmp(args[1],"-h")) {
		} else if (0 == change_setting(args[1],args[2])) {
			return 0;
		} else {
			return 1;
		}
	}
	t.printf("use 'set -l' to list available parameters\n"
		"use 'set -c <param> to clear parameter\n"
		"use 'set <param> <value>' to set parameter\n");
	return 1;
}


static int hostname(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		term.printf("%s: 0-1 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1) {
		if (Config.has_nodename())
			term.printf("hostname: %s\n",Config.nodename().c_str());
		else
			term.printf("hostname not set\n");
		return 0;
	}
	if (0 == term.getPrivLevel()) {
		term.printf("Insufficient privileges - execute 'su 1' to gain access.\n");
		return 1;
	}
	if (argc == 2) {
		setHostname(args[1]);
		Config.set_nodename(args[1]);
		RTData.set_node(args[1]);
	} else {
		return 1;
	}
	return 0;
}


static int wifi(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		term.printf("%s: 0-1 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	wifi_mode_t m = WIFI_MODE_NULL;
	if (ESP_OK != esp_wifi_get_mode(&m)) {
		term.printf("unable to determine current wifi mode\n");
		return 1;
	}
	if (argc == 1) {
		term.printf("wifi mode %u\n",m);
		return 0;
	}
	if ((args[1][0] >= '0') && (args[1][0] <= '3')) {
		m = (wifi_mode_t) (args[1][0] - '0');
	} else if (!strcmp(args[1],"station")) {
		m = WIFI_MODE_STA;
	} else if (!strcmp(args[1],"accesspoint")) {
		m = WIFI_MODE_AP;
	} else if (!strcmp(args[1],"both")) {
		m = WIFI_MODE_APSTA;
	} else if (!strcmp(args[1],"off")) {
		m = WIFI_MODE_NULL;
	} else if (!strcmp(args[1],"status")) {
		term.printf("station is %s\n",m & 1 ? "on" : "off");
		term.printf("accesspoint is %s\n",m & 2 ? "on" : "off");
		/*
		if (m & 1) {
			uint8_t st = wifi_station_get_connect_status();
			term.printf("station status %u\n",st);
		}
		*/
	} else if (!strcmp(args[1],"-h")) {
		term.printf("valid arguments: station, accesspoint, both, off, status");
		return 0;
	} else
		return 1;
	if (ESP_OK != esp_wifi_set_mode(m)) {
		term.printf("error changing wifi mode");
	}
	return 0;
}


#ifdef CONFIG_SMARTCONFIG
static int sc(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		term.printf("%s: 0 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1)
		term.printf("smartconfig is %srunning",smartconfig_running() ? "" : "not ");
	else if (0 == strcmp(args[1],"start"))
		return smartconfig_start();
	else if (0 == strcmp(args[1],"stop"))
		smartconfig_stop();
	else if (0 == strcmp(args[1],"version"))
		term.printf("%s\n",esp_smartconfig_get_version());
	else
		return 1;
	return 0;
}
#endif


#ifdef CONFIG_WPS
static int wps(Terminal &term, int argc, const char *args[])
{
	if (argc > 1) {
		term.printf("%s: 0 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	wifi_wps_start();
	return 0;
}
#endif


static int station(Terminal &term, int argc, const char *args[])
{
	if (argc > 3) {
		term.printf("%s: 0-2 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	WifiConfig *s = Config.mutable_station();
	if (argc == 1) {
		term.printf("ssid: %s\npass: %s\nactive: %s\n"
			, s->has_ssid() ? s->ssid().c_str() : NotSet
			, s->has_pass() ? s->pass().c_str() : NotSet
			, s->activate() ? "true" : "false"
			);
		if (s->has_addr4()) {
			uint32_t a = s->addr4();
			term.printf("addr: %d.%d.%d.%d/%d\n",a&0xff,(a>>8)&0xff,(a>>16)&0xff,(a>>24)&0xff,s->netmask4());
		}
		if (s->has_gateway4()) {
			uint32_t a = s->gateway4();
			term.printf("gw: %d.%d.%d.%d\n",a&0xff,(a>>8)&0xff,(a>>16)&0xff,(a>>24)&0xff);
		}
	} else if (!strcmp(args[1],"ssid")) {
		if (argc == 3)
			s->set_ssid(args[2]);
		else if (argc == 2)
			s->clear_ssid();
		else
			term.printf("ssid: '%s'\n",s->ssid().c_str());
	} else if (!strcmp(args[1],"pass")) {
		s->set_pass(args[2]);
	} else if ((!strcmp(args[1],"on")) || (!strcmp(args[1],"up"))) {
		if (s->has_ssid() && s->has_pass()) {
			wifi_start_station(s->ssid().c_str(),s->pass().c_str());
			s->set_activate(true);
		} else {
			term.printf("station needs ssid and pass to start\n");
			return 1;
		}
	} else if (!strcmp(args[1],"off")) {
		s->set_activate(false);
		wifi_stop_station();
	} else if (!strcmp(args[1],"clear")) {
		Config.clear_station();
		wifi_stop_station();
	} else if (!strcmp(args[1],"ip")) {
		if (argc != 3) {
			term.printf("invalid number of arguments\n");
			return 1;
		}
		if (!strcmp(args[2],"-c")) {
			s->clear_addr4();
			s->clear_netmask4();
			s->clear_gateway4();
			return 0;
		}
		unsigned x[5];
		if (5 != sscanf(args[2],"%u.%u.%u.%u/%u",x,x+1,x+2,x+3,x+4)) {
			term.printf("argument must be in the form 192.168.1.1/24\n");
			return 1;
		}
		if ((x[0] > 255) || (x[1] > 255) || (x[2] > 255) || (x[3] > 255) || (x[4] > 32)) {
			term.printf("argument must be in the form 192.168.1.1/24\n");
			return 1;
		}
		uint32_t ip = x[0] | (x[1]<<8) | (x[2]<<16) | (x[3]<<24);
		s->set_addr4(ip);
		s->set_netmask4(x[4]);
	} else if (!strcmp(args[1],"gw")) {
		if (argc != 3) {
			term.printf("invalid number of arguments\n");
			return 1;
		}
		unsigned x[4];
		if (4 != sscanf(args[2],"%u.%u.%u.%u",x,x+1,x+2,x+3)) {
			term.printf("argument must be in the form 192.168.1.1\n");
			return 1;
		}
		if ((x[0] > 255) || (x[1] > 255) || (x[2] > 255) || (x[3] > 255)) {
			term.printf("argument must be in the form 192.168.1.1\n");
			return 1;
		}
		uint32_t ip = x[0] | (x[1]<<8) | (x[2]<<16) | (x[3]<<24);
		s->set_gateway4(ip);
	} else if (!strcmp(args[1],"-h")) {
		term.printf("valid arguments: ssid, pass, ip, gw, on, off, clear\n");
	} else {
		return 1;
	}
	return 0;
}


static int accesspoint(Terminal &term, int argc, const char *args[])
{
	if (argc > 3) {
		term.printf("%s: 0-2 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	WifiConfig *ap = Config.mutable_softap();
	if (argc == 1) {
		term.printf("ssid: %s\npass: %s\nactive: %s\n"
			, ap->has_ssid() ? ap->ssid().c_str() : NotSet
			, ap->has_pass() ? ap->pass().c_str() : NotSet
			, ap->activate() ? "true" : "false"
			);
	} else if (!strcmp(args[1],"ssid")) {
		if (argc == 3)
			ap->set_ssid(args[2]);
		else 
			term.printf("ssid: '%s'",ap->ssid().c_str());
	} else if (!strcmp(args[1],"pass")) {
		ap->set_pass(args[2]);
		if (!wifi_stop_softap())
			return 1;
	} else if ((!strcmp(args[1],"on")) || (!strcmp(args[1],"up"))) {
		if (ap->has_ssid()) {
			ap->set_activate(true);
			if (!wifi_start_softap(ap->ssid().c_str(),ap->has_pass() ? ap->pass().c_str() : ""))
				return 1;
		} else {
			term.printf("softap needs ssid and pass to start\n");
			return 1;
		}
	} else if (!strcmp(args[1],"off")) {
		ap->set_activate(false);
		wifi_stop_softap();
	} else if (!strcmp(args[1],"clear")) {
		Config.clear_softap();
		wifi_stop_softap();
	} else if (!strcmp(args[1],"-h")) {
		term.printf("valid arguments: ssid, pass, on, off, clear\n");
	} else {
		return 1;
	}
	return 0;
}


#ifdef HAVE_FS
static int download(Terminal &term, int argc, const char *args[])
{
	if ((argc < 2) || (argc > 3)) {
		term.printf("%s: 1-2 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	return http_download(term,(char*)args[1], (argc == 3) ? args[2] : 0) ? 0 : 1;
}
#endif


static int nslookup(Terminal &term, int argc, const char *args[])
{
	if (argc != 2) {
		term.printf("%s: 1 argument expected, got %u\n",args[0],argc-1);
		return 1;
	}
	uint32_t a = resolve_hostname(args[1]);
	term.printf("%d.%d.%d.%d\n", a&0xff, (a>>8)&0xff, (a>>16)&0xff, (a>>24)&0xff);
	return 0;
}


#ifdef ESP32
static const char *GpioIntrTypeStr[] = {
	"disabled",
	"enabled",
	"NMI",
	"NMI and regular",
};


static const char *GpioIntrTriggerStr[] = {
	"disabled",
	"falling edge",
	"raising edge",
	"any edge",
	"low level",
	"high level",
};


static uint32_t read_iomux_conf(unsigned io)
{
	switch (io) {
	case 0: return *(uint32_t *)GPIO_PIN_REG_0;
	case 1: return *(uint32_t *)GPIO_PIN_REG_1;
	case 2: return *(uint32_t *)GPIO_PIN_REG_2;
	case 3: return *(uint32_t *)GPIO_PIN_REG_3;
	case 4: return *(uint32_t *)GPIO_PIN_REG_4;
	case 5: return *(uint32_t *)GPIO_PIN_REG_5;
	case 6: return *(uint32_t *)GPIO_PIN_REG_6;
	case 7: return *(uint32_t *)GPIO_PIN_REG_7;
	case 8: return *(uint32_t *)GPIO_PIN_REG_8;
	case 9: return *(uint32_t *)GPIO_PIN_REG_9;
	case 10: return *(uint32_t *)GPIO_PIN_REG_10;
	case 11: return *(uint32_t *)GPIO_PIN_REG_11;
	case 12: return *(uint32_t *)GPIO_PIN_REG_12;
	case 13: return *(uint32_t *)GPIO_PIN_REG_13;
	case 14: return *(uint32_t *)GPIO_PIN_REG_14;
	case 15: return *(uint32_t *)GPIO_PIN_REG_15;
	case 16: return *(uint32_t *)GPIO_PIN_REG_16;
	case 17: return *(uint32_t *)GPIO_PIN_REG_17;
	case 18: return *(uint32_t *)GPIO_PIN_REG_18;
	case 19: return *(uint32_t *)GPIO_PIN_REG_19;
//	case 20: return *(uint32_t *)GPIO_PIN_REG_20;
	case 21: return *(uint32_t *)GPIO_PIN_REG_21;
	case 22: return *(uint32_t *)GPIO_PIN_REG_22;
	case 23: return *(uint32_t *)GPIO_PIN_REG_23;
//	case 24: return *(uint32_t *)GPIO_PIN_REG_24;
	case 25: return *(uint32_t *)GPIO_PIN_REG_25;
	case 26: return *(uint32_t *)GPIO_PIN_REG_26;
	case 27: return *(uint32_t *)GPIO_PIN_REG_27;
//	case 28: return *(uint32_t *)GPIO_PIN_REG_28;
//	case 29: return *(uint32_t *)GPIO_PIN_REG_29;
//	case 30: return *(uint32_t *)GPIO_PIN_REG_30;
//	case 31: return *(uint32_t *)GPIO_PIN_REG_31;
	case 32: return *(uint32_t *)GPIO_PIN_REG_32;
	case 33: return *(uint32_t *)GPIO_PIN_REG_33;
	case 34: return *(uint32_t *)GPIO_PIN_REG_34;
	case 35: return *(uint32_t *)GPIO_PIN_REG_35;
	case 36: return *(uint32_t *)GPIO_PIN_REG_36;
	case 37: return *(uint32_t *)GPIO_PIN_REG_37;
	case 38: return *(uint32_t *)GPIO_PIN_REG_38;
	case 39: return *(uint32_t *)GPIO_PIN_REG_39;
	default:
		return 0xffffffff;
	}
}
#endif


static int gpio(Terminal &term, int argc, const char *args[])
{
	if (argc > 3) {
		term.printf("%s: 0-2 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
#ifdef ESP32
	if (argc == 1) {
		term.printf("gpio [pins|<pin>]\n");
	} else if (0 == strcmp(args[1],"pins")) {
		uint64_t enabled = GPIO_ENABLE_REG | ((uint64_t)GPIO_ENABLE1_REG << 32);
		for (unsigned pin = 0; pin < 32; ++pin)
			term.printf("pin %2u: gpio 0x%08x=%08x, %s\n"
				,pin
				,GPIO_REG(pin)
				,*(uint32_t*)GPIO_REG(pin)
				,((enabled>>pin)&1) ? "enabled" : "disabled"
				);
	} else if ((args[1][0] >= '0') && (args[1][0] <= '9')) {
		long pin = strtol(args[1],0,0);
		if (!GPIO_IS_VALID_GPIO(pin)) {
			term.printf("gpio %ld out of range\n",pin);
			return 1;
		}
		uint32_t iomux = read_iomux_conf(pin);
		uint32_t gpiopc = *(uint32_t*)GPIO_REG(pin);	// pin configuration @0x88+4*n
		bool level;
		if (pin < 32) 
			level = (((iomux>>9)&1 ? GPIO_IN_REG : GPIO_OUT_REG) >> pin ) & 1;
		else
			level = (((iomux>>9)&1 ? GPIO_IN1_REG : GPIO_OUT1_REG) >> (pin-32) ) & 1;
		term.printf(
			"pin %2u: iomux 0x%08x, gpiopc 0x%08x\n"
			"\tfunction   %d\n"
			"\tpad driver %d\n"
			"\tinput      %s\n"
			"\tpull-up    %s\n"
			"\tpull-down  %s\n"
			"\tlevel      %s\n"
			"\tAPP intr   %s\n"
			"\tPRO intr   %s\n"
			"\twakeup     %s\n"
			"\tinterrupts %s\n"
			,pin,iomux,gpiopc
			,((iomux>>12)&7)
			,((iomux>>10)&3)
			,((iomux>>9)&1) ? "enabled" : "disabled"
			,((iomux>>8)&1) ? "enabled" : "disabled"
			,((iomux>>7)&1) ? "enabled" : "disabled"
			,level ? "high" : "low"
			,GpioIntrTypeStr[(gpiopc>>13)&3]
			,GpioIntrTypeStr[(gpiopc>>16)&3]
			,((gpiopc>>10)&1) ? "enabled" : "disabled"
			,GpioIntrTriggerStr[(gpiopc>>7)&0x7]
			);
	}
#elif defined ESP8266
	if (argc == 1) {
		term.printf("gpio [all|<pin>]\n");
	} else if (0 == strcmp(args[1],"all")) {
		uint32_t dir = GPIO_REG_READ(GPIO_ENABLE_ADDRESS);
		for (int p = 0; p <= 16; ++p ) {
			term.printf("pin %2d: %s, %s\n",p
				, gpio_get_level((gpio_num_t)p) == 0 ? "low" : "high"
				, (dir & (1<<p)) ? "output" : "input"
				);
		}
	} else if ((args[1][0] >= '0') && (args[1][0] <= '9')) {
		long l = strtol(args[1],0,0);
		if ((l > 16) || (l < 0)) {
			term.printf("gpio value out of range");
			return 1;
		}
		if (argc == 2) {
			term.printf("%s\n",gpio_get_level((gpio_num_t)l) == 0 ? "low" : "high");
		} else if (argc == 3) {
			if (0 == strcmp(args[2],"out"))
				gpio_set_direction((gpio_num_t)l,GPIO_MODE_OUTPUT);
			else if (0 == strcmp(args[2],"in"))
				gpio_set_direction((gpio_num_t)l,GPIO_MODE_INPUT);
			else if (0 == strcmp(args[2],"0"))
				gpio_set_level((gpio_num_t)l,0);
			else if (0 == strcmp(args[2],"1"))
				gpio_set_level((gpio_num_t)l,1);
			else {
				term.printf("unexpected argument\n");
				return 1;
			}
		}
	}
#else
#error unknwon target
#endif
	else {
		term.printf("unexpected arguments\n");
		return 1;
	}
	return 0;
}


#if 0
static int segv(Terminal &term, int argc, const char *args[])
{
	term.printf("triggering segment violoation\n");
	*(char*)0 = 1;
	return 0;
}
#endif


#ifdef CONFIG_SNTP
static int sntp(Terminal &term, int argc, const char *args[])
{
	if (argc > 3) {
		term.printf("%s: 0-2 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1) {
		if (char *s = sntp_getservername(0))
			term.printf("sntp server: %s\n",s);
		if (const char *tz = getenv("TZ"))
			term.printf("timezone %s\n",tz);
		time_t now;
		time(&now);
		char buf[64];
		term.printf("current time: %s\n",asctime_r(localtime(&now),buf));
		uint8_t h,m;
		get_time_of_day(&h,&m);
		term.printf("get_time_of_day(): %u:%02u",h,m);
		return 0;
	}
	if (!strcmp(args[1],"-h"))
		term.printf("synopsis:\n%s [start|stop|set|clear]\n",args[0],args[0]);
	else if (!strcmp(args[1],"clear")) {
		Config.clear_sntp_server();
	} else if (!strcmp(args[1],"start")) {
		sntp_init();
	} else if (!strcmp(args[1],"stop")) {
		sntp_stop();
	} else if (!strcmp(args[1],"set")) {
		if (argc != 3) {
			term.printf("invalid numer of arguments");
			return 1;
		}
		sntp_stop();
		Config.set_sntp_server(args[2]);
		sntp_setservername(1,(char*)args[2]);
		sntp_init();
	} else
		return 1;
	return 0;
}
#endif


static int timezone(Terminal &term, int argc, const char *args[])
{
	if ((argc != 1) && (argc != 2)) {
		term.printf("%s: 0-1 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1) {
		if (const char *tz = getenv("TZ"))
			term.printf("timezone: %s\n",tz);
		else
			term.printf("timezone not set\n");
		return 0;
	}
	set_timezone(args[1]);
	return 0;
}


/*
static int threshold(Terminal &term, int argc, const char *args[])
{
	if ((argc != 1) && (argc != 3)) {
		term.printf("%s: 1 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1) {
		term.printf("threshold on %u, off %u, acive %s\n",ThresholdOn,ThresholdOff,ThresholdActive?"true":"false");
		return 0;
	}
	if (!strcmp(args[1],"on")) {
		long l = strtol(args[2],0,0);
		if ((l < 0) || (l > 1023)) {
			term.printf("invalid argument\n");
			return 1;
		}
		ThresholdOn = l;
		Config.set_threshold_on(l);
	} else if (!strcmp(args[1],"off")) {
		long l = strtol(args[2],0,0);
		if ((l < 0) || (l > 1023)) {
			term.printf("invalid argument\n");
			return 1;
		}
		ThresholdOff = l;
		Config.set_threshold_off(l);
	} else if (!strcmp(args[1],"active")) {
		if (!strcmp(args[2],"true"))
			ThresholdActive = true;
		else if (!strcmp(args[2],"1"))
			ThresholdActive = true;
		else if (!strcmp(args[2],"false"))
			ThresholdActive = false;
		else if (!strcmp(args[2],"0"))
			ThresholdActive = false;
		else {
			term.printf("invalid argument\n");
			return 1;
		}
		Config.set_threshold_active(ThresholdActive);
	} else {
		return 1;
	}
	return 0;
}
*/


static int xxdSettings(Terminal &t)
{
	size_t s = Config.calcSize();
	char *buf = (char*)malloc(s);
	if (buf == 0) {
		t.print("out of memory");
		return 1;
	}
	Config.toMemory((uint8_t*)buf,s);
	print_hex(t,(uint8_t*)buf,s);
	free(buf);
	return 0;
}


static int config(Terminal &term, int argc, const char *args[])
{
	if (argc != 2) {
		term.printf("%s: 1 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (!strcmp(args[1],"print")) {
		term.printf("current config:\n");
		TermStream ts(term);
		Config.toJSON(ts);
		term.printf("\n");
	} else if (!strcmp(args[1],"write")) {
		storeSettings();
	} else if (!strcmp(args[1],"read")) {
		readSettings();
	} else if (!strcmp(args[1],"defaults")) {
		setupDefaults();
	} else if (!strcmp(args[1],"activate")) {
		activateSettings();
	} else if (!strcmp(args[1],"clear")) {
		clearSettings();
	} else if (!strcmp(args[1],"erase")) {
		eraseSettings();
	} else if (!strcmp(args[1],"xxd")) {
		return xxdSettings(term);
	} else if (!strcmp(args[1],"nvxxd")) {
		size_t s;
		uint8_t *buf = readNVconfig(&s);
		if (buf == 0)
			return 1;
		print_hex(term,buf,s);
		free(buf);
	} else if (!strcmp(args[1],"-h")) {
		term.printf("valid arguments: print, read, write, defaults, activate, clear, erase, nvxxd, xxd\n");
	} else {
		return 1;
	}
	return 0;
}


// requires configUSE_TRACE_FACILITY in FreeRTOSConfig.h
#if configUSE_TRACE_FACILITY == 1
const char taskstates[] = "XRBSDI";
int ps(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		term.printf("%s: 0 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	unsigned nt = uxTaskGetNumberOfTasks();
	TaskStatus_t *st = (TaskStatus_t*) malloc(nt*sizeof(TaskStatus_t));
	if (st == 0) {
		term.printf("%s: unable to allocate enough memory\n",args[0]);
		return 1;
	}
	nt = uxTaskGetSystemState(st,nt,0);
	term.printf(" ID ST PRIO       TIME STACK NAME\n");
	for (unsigned i = 0; i < nt; ++i) {
		term.printf("%3u  %c %4u %10u %5u %s\n"
			,st[i].xTaskNumber
			,taskstates[st[i].eCurrentState]
			,st[i].uxCurrentPriority
			,st[i].ulRunTimeCounter
			,st[i].usStackHighWaterMark
			,st[i].pcTaskName);
	}
	free(st);
	return 0;
}
#endif


static int print_data(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		term.printf("%s: 0 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	TermStream jsonstr(term);
	runtimedata_to_json(jsonstr);
	term.write("\n",1);
	return 0;
}


#ifdef CONFIG_OTA
#ifdef close
#undef close
#endif
static int update(Terminal &term, int argc, const char *args[])
{
	if (argc > 3) {
		term.printf("%s: 1-2 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if ((argc == 1) || ((argc == 2) && (0 == strcmp(args[1],"-h")))) {
		term.printf("synopsys: %s [-b|-r] <source>\n"
			"-b : boot after update\n"
#ifdef CONFIG_ROMFS
			"-r : update romfs partition\n"
#endif
			,args[0]);
		return 0;
	}
	UBaseType_t p = uxTaskPriorityGet(0);
	vTaskPrioritySet(0, p > 4 ? p-3 : 1);
	int r = 1;
	if ((argc == 3) && (0 == strcmp(args[1],"-b"))) {
		r = perform_ota(term,(char*)args[2],true);
		term.printf("restarting system...\n");
		term.disconnect();
		vTaskDelay(400);
		esp_restart();
#ifdef CONFIG_ROMFS
	} else if ((argc == 3) && (0 == strcmp(args[1],"-r"))) {
		r = update_romfs(term,(char*)args[2]);
#endif
	} else if (argc == 2) {
		r = perform_ota(term,(char*)args[1],false);
	} else {
		term.printf("synopsys: %s [-b|-r] <source>\n",args[0]);
	}
	vTaskPrioritySet(0, p);
	return r;
}
#endif


static int getuptime(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		term.printf("%s: 0 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
#ifdef _POSIX_MONOTONIC_CLOCK
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC,&ts);
	uint32_t u = ts.tv_sec;
#else
	uint64_t u = clock() / CLOCKS_PER_SEC;
#endif
	unsigned d = u/(60*60*24);
	u -= d*(60*60*24);
	unsigned h = u/(60*60);
	u -= h*60*60;
	unsigned m = u/60;
	u -= m*60;
	unsigned s = u;
	term.printf("%u days, %d:%02u:%02u\n",d,h,m,s);
	return 0;
}


static int cpu(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		term.printf("%s: 0-1 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1) {
		int f = esp_clk_cpu_freq();
		unsigned mhz;
		switch (f) {
		case RTC_CPU_FREQ_80M:
			mhz = 80;
			break;
		case RTC_CPU_FREQ_160M:
			mhz = 160;
			break;

#ifdef ESP32
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
		term.printf("%u MHz\n",mhz);
		return 0;
	}
	if (0 == term.getPrivLevel()) {
		term.printf(Denied);
		return 1;
	}
	errno = 0;
	long l = strtol(args[1],0,0);
	if ((l == 0) || (errno != 0))
		return 1;
	if (0 != set_cpu_freq(l)) {
		term.printf("unsupported frequency\n");
		return 1;
	}
	Config.set_cpu_freq(l);
	return 0;
}


static int password(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		term.printf("%s: 1 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1) {
		term.printf("-c: clear\n-r: reset\n-s: set\n-v: verify\n");
		return 0;
	}
	if (!strcmp(args[1],"-c")) {
		term.printf("clearing password\n");
		Config.clear_pass_hash();
	} else if (!strcmp(args[1],"-r")) {
		term.printf("resetting password to empty string\n");
		setPassword("");
	} else if (!strcmp(args[1],"-s")) {
		char buf[32];
		term.write(PW,sizeof(PW));
		int n = term.readInput(buf,sizeof(buf),false);
		if (n < 0)
			return 1;
		buf[n] = 0;
		setPassword(buf);
	} else if (!strcmp(args[1],"-v")) {
		char buf[32];
		term.write(PW,sizeof(PW));
		int n = term.readInput(buf,sizeof(buf),false);
		if (n < 0)
			return 1;
		buf[n] = 0;
		term.printf("%smatch\n", verifyPassword(buf) ? "" : "mis");
	} else {
		setPassword(args[1]);
	}
	return 0;
}


static int su(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		term.printf("privileg level %u\n",term.getPrivLevel());
		return 0;
	}
	if (argc > 2) {
		term.printf("synopsis: su [<0|1>]\nlevel 1 for admin rights\n");
		return 1;
	}
	if (!strcmp(args[1],"0")) {
		term.setPrivLevel(0);
		term.printf("new privileg level %u\n",term.getPrivLevel());
		return 0;
	}
	if (strcmp(args[1],"1")) {
		term.printf("invalid argument\n");
		return 1;
	}
	if (!Config.pass_hash().empty()) {
		char buf[32];
		term.write(PW,sizeof(PW));
		int n = term.readInput(buf,sizeof(buf),false);
		if (n < 0)
			return 1;
		buf[n] = 0;
		if (!verifyPassword(buf))
			return 1;
	}
	term.setPrivLevel(1);
	term.printf("new privileg level %u\n",term.getPrivLevel());
	return 0;
}


/*
static int flashtest(Terminal &term, int argc, const char *args[])
{
	if ((argc != 2) && (argc != 3)) {
		term.printf("%s: 1 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	int r;
	long page = strtol(args[1],0,0);
	uint8_t buf[32];
	if (argc == 2) {
		r = spi_flash_read(page*4096,(uint32_t*)buf,sizeof(buf));
		printf("read@0x%lx = %d\n",page,r);
		for (int i = 0; i < sizeof(buf); ++i)
			printf(" %02x",buf[i]);
		printf("\n");
		return 0;
	}
	if (!strcmp(args[2],"erase")) {
		r = spi_flash_erase_sector(page);
		printf("erase = %d\n",r);
		return 0;
	}
	long l = strtol(args[2],0,0);
	if ((l < 0) || (l > sizeof(buf))) {
		printf("argument '%s' out of range\n",args[1]);
		return 1;
	}
	r = spi_flash_read(page*4096,(uint32_t*)buf,sizeof(buf));
	printf("read = %d\n",r);
	for (int i = 0; i < sizeof(buf); ++i)
		printf(" %02x",buf[i]);
	printf("\n");
	
	for (int i = 0; i < l; i += 2) 
		buf[i] = i;
	r = spi_flash_write(page*4096,(uint32_t*)buf,sizeof(buf));
	printf("write = %d\n",r);

	r = spi_flash_read(page*4096,(uint32_t*)buf,sizeof(buf));
	printf("read = %d\n",r);
	for (int i = 0; i < sizeof(buf); ++i)
		printf(" %02x",buf[i]);
	printf("\n");
	return 0;
}
*/


static int ifconfig(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		term.printf("%s: 0 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}

	tcpip_adapter_ip_info_t ipconfig;
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipconfig))
		term.printf("if0/station: %d.%d.%d.%d mask %d.%d.%d.%d gw %d.%d.%d.%d %s\n",
			ipconfig.ip.addr & 0xff,
			ipconfig.ip.addr>>8 & 0xff,
			ipconfig.ip.addr>>16 & 0xff,
			ipconfig.ip.addr>>24 & 0xff,
			ipconfig.netmask.addr & 0xff,
			ipconfig.netmask.addr>>8 & 0xff,
			ipconfig.netmask.addr>>16 & 0xff,
			ipconfig.netmask.addr>>24 & 0xff,
			ipconfig.gw.addr & 0xff,
			ipconfig.gw.addr>>8 & 0xff,
			ipconfig.gw.addr>>16 & 0xff,
			ipconfig.gw.addr>>24 & 0xff,
			wifi_station_isup() ? "up" : "down"
			);
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ipconfig))
		term.printf("if1/soft_ap: %d.%d.%d.%d mask %d.%d.%d.%d gw %d.%d.%d.%d %s\n",
			ipconfig.ip.addr & 0xff,
			ipconfig.ip.addr>>8 & 0xff,
			ipconfig.ip.addr>>16 & 0xff,
			ipconfig.ip.addr>>24 & 0xff,
			ipconfig.netmask.addr & 0xff,
			ipconfig.netmask.addr>>8 & 0xff,
			ipconfig.netmask.addr>>16 & 0xff,
			ipconfig.netmask.addr>>24 & 0xff,
			ipconfig.gw.addr & 0xff,
			ipconfig.gw.addr>>8 & 0xff,
			ipconfig.gw.addr>>16 & 0xff,
			ipconfig.gw.addr>>24 & 0xff,
			wifi_softap_isup() ? "up" : "down"
			);
#ifdef ESP32
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_ETH, &ipconfig))
		term.printf("if2/eth    : %d.%d.%d.%d mask %d.%d.%d.%d gw %d.%d.%d.%d %s\n",
			ipconfig.ip.addr & 0xff,
			ipconfig.ip.addr>>8 & 0xff,
			ipconfig.ip.addr>>16 & 0xff,
			ipconfig.ip.addr>>24 & 0xff,
			ipconfig.netmask.addr & 0xff,
			ipconfig.netmask.addr>>8 & 0xff,
			ipconfig.netmask.addr>>16 & 0xff,
			ipconfig.netmask.addr>>24 & 0xff,
			ipconfig.gw.addr & 0xff,
			ipconfig.gw.addr>>8 & 0xff,
			ipconfig.gw.addr>>16 & 0xff,
			ipconfig.gw.addr>>24 & 0xff,
			eth_isup() ? "up" : "down"
			);
#endif
	return 0;
}


static int version(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		term.printf("%s: 0 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	term.printf("Version %s\n"
#ifdef HG_REV
		"hg rev " HG_REV "\n"
#endif
#ifdef HG_ID
		"hg id " HG_ID "\n"
#endif
		"build on " __DATE__ ", " __TIME__ "\n"
		, Version
		);
	term.printf("IDF version: %s\n",esp_get_idf_version());
	return 0;
}


static int help(Terminal &term, int argc, const char *args[]);
extern int action(Terminal &term, int argc, const char *args[]);
extern int alive(Terminal &term, int argc, const char *args[]);
extern int at(Terminal &term, int argc, const char *args[]);
extern int bme(Terminal &term, int argc, const char *args[]);
extern int dht(Terminal &term, int argc, const char *args[]);
extern int dim(Terminal &term, int argc, const char *args[]);
extern int distance(Terminal &term, int argc, const char *args[]);
extern int dmesg(Terminal &term, int argc, const char *args[]);
extern int holiday(Terminal &term, int argc, const char *args[]);
extern int inetadm(Terminal &term, int argc, const char *args[]);
extern int influx(Terminal &term, int argc, const char *args[]);
extern int lightctrl(Terminal &term, int argc, const char *args[]);
extern int shell_format(Terminal &term, int argc, const char *args[]);
extern int status_led(Terminal &term, int argc, const char *args[]);
extern int subtasks(Terminal &term, int argc, const char *args[]);
extern int udp_stats(Terminal &term, int argc, const char *args[]);
extern int uart_termcon(Terminal &term, int argc, const char *args[]);

ExeName ExeNames[] = {
	{"?",0,help,"help"},
#ifdef CONFIG_ALIVELED
	{"alive",1,alive,"alive LED"},
#endif
	{"ap",1,accesspoint,"accesspoint settings"},
	{"adc",0,adc,"A/D converter"},
	{"action",0,action,"actions"},
#ifdef CONFIG_AT_ACTIONS
	{"at",0,at,"time triggered actions"},
#endif
#ifdef CONFIG_BME280
	{"bme",0,bme,"BME280 sensor"},
#endif
#ifdef CONFIG_OTA
	{"boot",1,boot,"get/set boot partition"},
#ifdef CONFIG_INFOCAST
	{"caststats",0,udp_stats,"UDP cast statistics"},
#endif
#endif
	{"cat",0,shell_cat,"cat file"},
#ifdef ESP32
	{"cd",0,shell_cd,"change directory"},
#endif
#ifdef CONFIG_TERMSERV
	{"con",0,uart_termcon,"switch to UART console"},
#endif
	{"chipid",0,chipid,"system chip information"},
	{"config",1,config,"system configuration"},
	{"cpu",0,cpu,"CPU speed"},
#if defined CONFIG_SPIFFS || defined CONFIG_FATFS
	{"df",0,shell_df,"available storage"},
#endif
#if CONFIG_DMESG_SIZE > 0
	{"dmesg",0,dmesg,"system log"},
#endif
#ifdef CONFIG_DHT
	{"dht",0,dht,"DHTxx family of sensors"},
#endif
#ifdef CONFIG_DIMMER_GPIO
	{"dim",0,dim,"dimmer driver"},
#endif
#ifdef CONFIG_DIST
	{"dist",0,distance,"hc_sr04 distance measurement"},
#endif
#ifdef HAVE_FS
	{"download",1,download,"http file download"},
#endif
#if defined CONFIG_SPIFFS || defined CONFIG_FATFS
	{"format",1,shell_format,"format storage"},
#endif
	//{"flashtest",flashtest},
	{"gpio",1,gpio,"GPIO access"},
#ifdef ESP32
	{"hall",0,hall,"hall sensor"},
#endif
	{"help",0,help,"help"},
#ifdef CONFIG_HOLIDAYS
	{"holiday",0,holiday,"holiday settings"},
#endif
	{"hostname",0,hostname,"get or set hostname"},
	{"ifconfig",0,ifconfig,"network interface settings"},
#ifdef CONFIG_INETD
	{"inetadm",1,inetadm,"manage network services"},
#endif
#ifdef CONFIG_INFLUX
	{"influx",1,influx,"configure influx UDP data gathering"},
#endif
#ifdef CONFIG_LIGHTCTRL
	{"lightctrl",0,lightctrl,"light control APP settings"},
#endif
	{"ls",0,shell_ls,"list storage"},
	{"mac",0,mac,"MAC addresses"},
	{"mem",0,mem,"RAM statistics"},
#ifdef CONFIG_FATFS
	{"mkdir",1,shell_mkdir,"make directory"},
#endif
#ifdef CONFIG_MQTT
	{"mqtt",1,mqtt,"MQTT settings"},
#endif
#ifdef CONFIG_FATFS
	{"mv",1,shell_mv,"move/rename file"},
#endif
	{"nslookup",0,nslookup,"lookup hostname in DNS"},
	{"part",0,part,"partition table"},
	{"passwd",1,password,"set password"},
#if configUSE_TRACE_FACILITY == 1
	{"ps",0,ps,"task statistics"},
#endif
	{"reboot",1,shell_reboot,"reboot system"},
#ifdef ESP32
	{"restore",1,restore,"restore system defaults"},
#endif
#ifdef HAVE_FS
	{"rm",1,shell_rm,"remove file"},
#endif
#ifdef CONFIG_FATFS
	{"rmdir",1,shell_rmdir,"remove directory"},
#endif
#ifdef CONFIG_SMARTCONFIG
	{"sc",0,sc,"SmargConfig actions"},
#endif
#if 0
	{"segv",1,segv,"trigger a segmentation violation"},
#endif
	{"set",1,set,"variable settings"},
#ifdef CONFIG_SNTP
	{"sntp",1,sntp,"simple NTP client settings"},
#endif
	{"station",1,station,"WiFi station settings"},
	{"su",0,su,"set user privilege level"},
#ifdef CONFIG_SUBTASKS
	{"subtasks",0,subtasks,"cyclic tasks procedures"},
#endif
	{"timezone",1,timezone,"set time zone for NTP"},
#ifdef HAVE_FS
	{"touch",1,shell_touch,"create file, update time-stamp"},
#endif
#ifdef CONFIG_OTA
	{"update",1,update,"OTA download procedure"},
#endif
	{"uptime",0,getuptime,"time system is up and running"},
	{"version",0,version,"version description"},
	{"webdata",0,print_data,"data provided to webpages"},
	{"wifi",1,wifi,"wifi station/AP mode"},
#ifdef CONFIG_WPS
	{"wps",1,wps,"wifi configuration with WPS"},
#endif
#ifdef HAVE_FS
	{"xxd",1,shell_xxd,"hex output of file"},
#endif
};


static int help(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		term.printf("%s: 0 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	for (int i = 2; i < sizeof(ExeNames)/sizeof(ExeNames[0]); ++i) 
		term.printf("%-14s %-5s  %s\n",ExeNames[i].name,ExeNames[i].plvl?"admin":"user",ExeNames[i].descr);
	return 0;
}


int shellexe(Terminal &term, const char *cmd)
{
	//TimeDelta td(__FUNCTION__);
	log_info("shell","execute '%s'",cmd);	// log_*() must not be used due to associated Mutexes
	char *args[8];
	size_t l = strlen(cmd);
	char buf[l+1];
	memcpy(buf,cmd,l+1);
	char *at = buf;
	size_t n = 0;
	do {
		args[n] = at;
		at = strchr(at,' ');
		while (at && (*at == ' ')) {
			*at = 0;
			++at;
		}
		++n;
		if (n == sizeof(args)/sizeof(args[0])) {
			term.printf("too many arguments\n");
			return 1;
		}
	} while (at && *at);
	for (int i = 0; i < sizeof(ExeNames)/sizeof(ExeNames[0]); ++i) {
		if (0 == strcmp(args[0],ExeNames[i].name)) {
			if (!Config.pass_hash().empty() && (ExeNames[i].plvl > term.getPrivLevel())) {
				term.printf(Denied);
				return 1;
			}
			//term.printf("calling shell function '%s'\n",ExeNames[i].name);
			if (0 == ExeNames[i].function(term,n,(const char **)args)) {
				term.printf("\nOK.\n");
				return 0;
			} else {
				term.printf("\nError.\n");
				return 1;
			}
		}
	}
	term.printf("'%s': command not found\n",args[0]);
	return 1;
}

