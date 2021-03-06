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

#include <sdkconfig.h>

#include "actions.h"
#include "binformats.h"
#include "console.h"
#include "dataflow.h"
#include "dht.h"
#include "event.h"
#include "fs.h"
#include "func.h"
#include "globals.h"
#include "log.h"
#include "ota.h"
#include "memfiles.h"
#include "mqtt.h"
#include "profiling.h"
#include "romfs.h"
#include "settings.h"
#include "shell.h"
#include "status.h"
#include "support.h"
#include "terminal.h"
#include "timefuse.h"
#include "versions.h"
#include "wifi.h"

#include <sstream>

#include <esp_image_format.h>
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
#ifdef CONFIG_IDF_TARGET_ESP32
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

#ifdef CONFIG_IDF_TARGET_ESP32
#include <lwip/err.h>
#include <lwip/dns.h>
#elif defined CONFIG_IDF_TARGET_ESP8266
#if IDF_VERSION > 32
#include <driver/rtc.h>	// post v3.2
#endif
#include <driver/hw_timer.h>
extern "C" {
#include <esp_clk.h>
}
#endif

#include <netdb.h>

#ifdef CONFIG_IDF_TARGET_ESP8266
#include <apps/sntp/sntp.h>
#elif IDF_VERSION >= 32
#include <lwip/apps/sntp.h>	// >= v3.2
#else
#include <apps/sntp/sntp.h>	// <= v3.1
#endif
#include <driver/uart.h>

#ifdef CONFIG_IDF_TARGET_ESP32
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
#elif defined CONFIG_IDF_TARGET_ESP8266
//#include <driver/gpio.h>
#endif

#include <dirent.h>

#include <errno.h>
#include <fcntl.h>
#include <estring.h>
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
	const char *help;
};

static estring PWD = "/flash/";
static const char PW[] = "password:", NotSet[] = "<not set>";

int help_cmd(Terminal &term, const char *arg);

extern "C"
const char *getpwd()
{
	return PWD.c_str();
}


static void print_action(void *p, const Action *a)
{
	if (a == 0)
		return;
	Terminal *t = (Terminal*)p;
	t->printf("\t%-24s  %s\n",a->name,a->text);
}


static int action(Terminal &t, int argc, const char *args[])
{
	if (argc == 1)
		return help_cmd(t,args[0]);
	if (argc != 2)
		return arg_invnum(t);
	if (0 == strcmp(args[1],"-l")) {
		action_iterate(print_action,(void*)&t);
	} else if (!strcmp(args[1],"-F")) {
		if (0 == t.getPrivLevel())
			return arg_priv(t);
		Config.set_actions_enable(Config.actions_enable()|2);
	} else if (!strcmp(args[1],"-f")) {
		if (0 == t.getPrivLevel())
			return arg_priv(t);
		Config.set_actions_enable(Config.actions_enable()^~2);
	} else if (action_activate(args[1])) {
		t.printf("unknown action %s\n",args[1]);
		return 1;
	}
	return 0;
}


static int event(Terminal &t, int argc, const char *args[])
{
	if (argc == 1) {
		return help_cmd(t,args[0]);
	} else if (argc == 2) {
		if (!strcmp(args[1],"-l")) {
			event_t e = 1;
			while (const char *name = event_name(e)) {
				t.printf("%s =>\n",name);
				for (auto a : event_callbacks(e)) 
					t.printf("\t%s\n",a->name);
				++e;
			}
		} else {
			return arg_invalid(t,args[1]);
		}
		return 0;
	} else if (argc == 4) {
		if (0 == t.getPrivLevel())
			return arg_priv(t);
		Action *a = action_get(args[3]);
		if (a == 0)
			return arg_invalid(t,args[3]);
		event_t e = event_id(args[2]);
		if (e == 0) {
			return arg_invalid(t,args[2]);
		} else if (!strcmp(args[1],"-a")) {
			event_callback(e,a);
			Trigger *t = Config.add_triggers();
			t->set_event(args[2]);
			t->set_action(args[3]);
			return 0;
		} else  if (!strcmp(args[1],"-d")) {
			event_detach(e,a);
			auto &t = *Config.mutable_triggers();
			for (auto i = t.begin(), e = t.end(); i != e; ++i) {
				if ((i->event() == args[2]) && (i->action() == args[3])) {
					t.erase(i);
					return 0;
				}
			}
		} else {
			return arg_invalid(t,args[1]);
		} 
	} else {
		return arg_invnum(t);
	}
	return 1;
}


#ifdef CONFIG_IDF_TARGET_ESP32
static int shell_cd(Terminal &term, int argc, const char *args[])
{
	if (argc > 2)
		return arg_invnum(term);
	if (argc == 1) {
		term.println(PWD.c_str());
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
	if (argc != 2)
		return arg_invnum(term);
	int err = 0;
	int a = 1;
	while (a < argc) {
		estring fn;
		if (args[a][0] != '/')
			fn = PWD;
		fn += args[a];
		if (-1 == unlink(fn.c_str())) {
			term.println(strerror(errno));
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
	if (argc != 2)
		return arg_invnum(term);
	estring dn;
	if (args[1][0] != '/')
		dn = PWD;
	dn += args[1];
	if (-1 == mkdir(dn.c_str(),0777)) {
		term.println(strerror(errno));
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
	estring dn;
	if (args[1][0] != '/')
		dn = PWD;
	dn += args[1];
	if (-1 == rmdir(dn.c_str())) {
		term.println(strerror(errno));
		return 1;
	}
	return 0;
}


static int shell_mv(Terminal &term, int argc, const char *args[])
{
	if (argc != 3)
		return arg_invnum(term);
	estring fn0,fn1;
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
		estring pwd = PWD;
		if (pwd.back() == '/')
			pwd.resize(pwd.size()-1);
		DIR *d = opendir(pwd.c_str());
		if (d == 0) {
			term.println(strerror(errno));
			return 1;
		}
		while (struct dirent *e = readdir(d))
			term.println(e->d_name);
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
		estring dir;
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
			estring f = dir;
			f += e->d_name;
			if (nlst) {
				term.println(e->d_name);
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
		size_t s,o;
		romfs_getentry(n,&s,&o);
		term.printf("\t%-12s %6u\n",n,s,o);
	}
	return 0;
#else
	term.printf("not implemented\n");
	return 1;
#endif
}


static int shell_cat1(Terminal &term, const char *arg)
{
#ifdef HAVE_FS
	char *filename;
	if (arg[0] == '/') {
		filename = (char*)arg;
	} else {
		size_t p = PWD.size();
		size_t a = strlen(arg);
		filename = (char *) malloc(a+p+1);
		if (filename == 0)
			return 1;
		memcpy(filename,PWD.data(),p);
		memcpy(filename+p,arg,a+1);
	}
	int fd = open(filename,O_RDONLY);
	if (filename != arg)
		free(filename);
	if (fd < 0)
		return 1;
	struct stat st;
	if (0 != fstat(fd,&st)) {
		close(fd);
		return 1;
	}
	char buf[512];
	int t = 0;
	do {
		int n = read(fd,buf,sizeof(buf));
		if (n == -1) {
			close(fd);
			return 1;
		}
		t += n;
		term.print(buf,n);
	} while (t < st.st_size);
	close(fd);
	return 0;
#elif defined CONFIG_ROMFS
	const char *filename = arg;
	if (filename[0] == '/')
		++filename;
	int r = romfs_open(filename);
	if (r < 0)
		return 1;
	size_t s = romfs_size_fd(r);
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
	return 1;
#endif
}

static int shell_cat(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		return arg_missing(term);
	int r = 0;
	for (int i = 1; i < argc; ++i)
		r |= shell_cat1(term,args[i]);
	return r;
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
		return arg_invnum(term);
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
		return arg_invnum(term);
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
		return arg_invnum(term);
	}
	esp_restart();
	return 0;
}


static int restore(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		return arg_invnum(term);
	}
	esp_wifi_restore();
	return 0;
}


#if defined CONFIG_SPIFFS || defined CONFIG_FATFS
static int shell_df(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		return arg_invnum(term);
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
	DWORD nc = 0;
	FATFS *fs = 0;
	int err = f_getfree(PWD.c_str(),&nc,&fs);
	if ((err == 0) && (fs != 0)) {
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
		return arg_invnum(term);
	}
	esp_partition_iterator_t i = esp_partition_find(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_ANY,0);
	while (i) {
		const esp_partition_t *p = esp_partition_get(i);
#ifdef CONFIG_IDF_TARGET_ESP32
		term.printf("%s %02x %7d@%08x %-8s",p->type ? "data" : "app ", p->subtype, p->size, p->address, p->label);
		esp_app_desc_t desc;
		if (0 == esp_ota_get_partition_description(p,&desc))
			term.printf(" [%s, %s, %s]\n",desc.project_name,desc.version,desc.idf_ver);
		else
			term.printf("\n");
#else
		term.printf("%s %02x %7d@%08x %-8s\n",p->type ? "data" : "app ", p->subtype, p->size, p->address, p->label);
#endif
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


static int mem(Terminal &term, int argc, const char *args[])
{
	term.printf("32bit mem   : %u\n",heap_caps_get_free_size(MALLOC_CAP_32BIT));
	term.printf("8bit mem    : %u\n",heap_caps_get_free_size(MALLOC_CAP_8BIT));
	term.printf("DMA  mem    : %u\n",heap_caps_get_free_size(MALLOC_CAP_DMA));
#ifdef CONFIG_IDF_TARGET_ESP32
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
		return help_cmd(term,args[0]);
	} else if (argc == 2) {
		if (!strcmp("-l",args[1])) {
			if (ESP_OK == esp_wifi_get_mac(WIFI_IF_AP,mac))
				term.printf("softap mac:   %02x:%02x:%02x:%02x:%02x:%02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
			if (ESP_OK == esp_wifi_get_mac(WIFI_IF_STA,mac))
				term.printf("station mac:  %02x:%02x:%02x:%02x:%02x:%02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
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
		if (6 != sscanf(args[2],"%x:%x:%x:%x:%x:%x",inp+0,inp+1,inp+2,inp+3,inp+4,inp+5))
			return arg_invalid(term,args[2]);
		for (int i = 0; i < 6; ++i) {
			if (inp[i] & ~0xff) {
				term.printf("mac element %d out of range",i);
				return 1;
			}
			mac[i] = inp[i];
		}
		wifi_interface_t w;
		if (!strcmp("-s",args[1])) {
			w = WIFI_IF_AP;
			Config.mutable_station()->set_mac(mac,6);
		}
		else if (!strcmp("-a",args[1])) {
			w = WIFI_IF_AP;
			Config.mutable_softap()->set_mac(mac,6);
		} else {
			return arg_invalid(term,args[1]);
		}
		if (esp_err_t e = esp_wifi_set_mac(w,mac)) {
			term.printf("error setting mac: %s",esp_err_to_name(e));
			return 1;
		}
		return 0;
	}
	return 1;
}


static int set(Terminal &t, int argc, const char *args[])
{
	if (argc == 1) {
		return help_cmd(t,args[0]);
	} else if (argc == 2) {
		if (0 == strcmp("-l",args[1])) {
			list_settings(t);
			return 0;
		}
	} else if (argc == 3) {
		if (0 == strcmp(args[1],"-c"))
			return update_setting(t,args[2],0);
		return update_setting(t,args[1],args[2]);
	} else
		return arg_invnum(t);
	return arg_invalid(t,args[1]);
}


static int hostname(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		return arg_invnum(term);
	}
	if (argc == 1) {
		term.printf("nodename: %s\n",Config.has_nodename() ? Config.nodename().c_str() : "<unset>");
		return 0;
	}
	if (0 == term.getPrivLevel()) {
		term.printf("Insufficient privileges - execute 'su 1' to gain access.\n");
		return 1;
	}
	Config.set_nodename(args[1]);
	cfg_set_hostname(args[1]);
	return 0;
}


static int hwconf(Terminal &term, int argc, const char *args[])
{
	static vector<uint8_t> hwcfgbuf;
	if (argc == 1) {
		term.printf(
#ifdef CONFIG_HWCONF_DYNAMIC
				"add <device>\n"
				"set <device>[<id>].<param> <value>\n"
				"add <arrayname>\n"
				"clear\n"
				"reset\n"
				"xxd\n"
				"print\n"
				"json\n"
				"write\n"
#endif
				"writebuf\n"
				"parsexxd\n"
				"clearbuf\n"
				"nvxxd\n"
			   );
		return 0;
	}
#ifdef CONFIG_HWCONF_DYNAMIC
	if (!strcmp("add",args[1])) {
		if (argc < 3)
			return arg_missing(term);
		char arrayname[strlen(args[2])+4];
		strcpy(arrayname,args[2]);
		strcat(arrayname,"[+]");
		return HWConf.setByName(arrayname,0) < 0;
	} else if (!strcmp("set",args[1])) {
		if (argc < 4)
			return arg_missing(term);
		return (0 > HWConf.setByName(args[2],args[3]));
	} else if (!strcmp("reset",args[1])) {
		return cfg_read_hwcfg() >= 0;
	} else if (!strcmp("clear",args[1])) {
		if (argc == 2) {
			HWConf.clear();
			HWConf.set_magic(0xAE54EDCB);
			return 0;
		}
		return HWConf.setByName(args[2],0) < 0;
	} else if (!strcmp("xxd",args[1])) {
		size_t s = HWConf.calcSize();
		uint8_t *buf = (uint8_t *) malloc(s);
		HWConf.toMemory(buf,s);
		print_hex(term,buf,s);
		free(buf);
		return 0;
	} else if (!strcmp("show",args[1])) {
		HWConf.toASCII(term);
		return 0;
	} else if (!strcmp("json",args[1])) {
		HWConf.toJSON(term);
		return 0;
	} else if (!strcmp("write",args[1])) {
		return cfg_store_hwcfg();
#else
	if (!strcmp("clear",args[1])) {
		if (argc == 2) {
			HWConf.clear();
			HWConf.set_magic(0xAE54EDCB);
			return 0;
		}
#endif
	} else if (!strcmp("nvxxd",args[1])) {
		size_t s;
		uint8_t *buf = 0;
		if (int e = readNVconfig("hw.cfg",&buf,&s)) {
			term.println(esp_err_to_name(e));
			return 1;
		}
		print_hex(term,buf,s);
		free(buf);
		return 0;
	} else if (!strcmp("parsexxd",args[1])) {
		uint8_t b = 0, x = 0;
		bool nl = false;
		char c;
		while (term.get_ch(&c) == 1) {
			if ((c == ' ') || (c == '\t') || (c == '\n'))
				continue;
			if (c == '\r') {
				if (nl) {
					break;
				}
				nl = true;
				continue;
			}
			nl = false;
			b <<= 4;
			if ((c >= '0') && (c <= '9')) {
				b |= (c-'0');
			} else if ((c >= 'a') && (c <= 'f')) {
				b |= (c-'a')+10;
			} else if ((c >= 'A') && (c <= 'F')) {
				b |= (c-'A')+10;
			} else {
				term.printf("invalid input 0x%x\n",c);
				return 1;
			}
			if (++x == 2) {
				hwcfgbuf.push_back(b);
				x = 0;
				b = 0;
			}
		}
		term.printf("parsing %u bytes\n",hwcfgbuf.size());
		HardwareConfig nc;
		int e = nc.fromMemory(hwcfgbuf.data(),hwcfgbuf.size());
		if (0 < e) {
			HWConf = nc;
			return 0;
		}
		term.printf("parser error: %d\n",e);
	} else if (!strcmp("writebuf",args[1])) {
		return writeNVM("hw.cfg",hwcfgbuf.data(),hwcfgbuf.size());
	} else if (!strcmp("clearbuf",args[1])) {
		hwcfgbuf.clear();
		return 0;
	} else if (!strcmp("xxdbuf",args[1])) {
		print_hex(term,hwcfgbuf.data(),hwcfgbuf.size());
		return 0;
	} 
	return 1;
}


int arg_invnum(Terminal &t)
{
	t.println("invalid number of arguments");
	return 1;
}


int arg_invalid(Terminal &t, const char *a)
{
	t.printf("invalid argument '%s'\n",a);
	return 1;
}


int arg_missing(Terminal &t)
{
	t.println("missing argument");
	return 1;
}


int arg_priv(Terminal &t)
{
	t.println("Access denied.");
	return 1;
}


static bool parse_bool(int argc, const char *args[], int a, bool d)
{
	if (a >= argc)
		return d;
	if (!strcmp(args[a],"true"))
		return true;
	if (!strcmp(args[a],"false"))
		return false;
	if (!strcmp(args[a],"on"))
		return true;
	if (!strcmp(args[a],"off"))
		return false;
	if (!strcmp(args[a],"yes"))
		return true;
	if (!strcmp(args[a],"no"))
		return false;
	return d;
}


static int timefuse(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		return arg_invnum(term);
	if (argc == 2) {
		if (!strcmp("-l",args[1])) {
			timefuse_t t = timefuse_iterator();
			// active timers cannot be queried for repeat!
			term.printf("%-16s %9s active\n","name","interval");
			while (t) {
				int on = timefuse_active(t);
				term.printf("%-16s %9u %-7s\n"
						,timefuse_name(t)
						,timefuse_interval_get(t)
						,on==0?"false":on==1?"true":"unknown");
				t = timefuse_next(t);
			}
			return 0;
		}
		return arg_invalid(term,args[1]);
	}
	if (0 == term.getPrivLevel())
		return arg_priv(term);
	if (argc == 3) {
		if (!strcmp("-s",args[1]))
			return timefuse_start(args[2]);
		if (!strcmp("-t",args[1]))
			return timefuse_stop(args[2]);
		if (!strcmp("-d",args[1])) {
			/*
			 * Deletes only from config.
			 * dynamic deletion would require deletion of
			 * actions and events associated with the timer!
			 */
			auto t = Config.mutable_timefuses();
			for (auto i = t->begin(), e = t->end(); i != e; ++i) {
				if (i->name() == args[2]) {
					t->erase(i);
					return 0;
				}
			}
			return 1;
			// return timefuse_delete(args[2]);	-- incomplete
		}
		return arg_invalid(term,args[1]);
	}
	// argc >= 4
	if (!strcmp("-r",args[1])) {
		for (EventTimer &t : *Config.mutable_timefuses()) {
			if (t.name() == args[2]) {
				unsigned config = t.config();
				bool r = parse_bool(argc,args,3,config&1);
				config &= ~1;
				config |= r;
				t.set_config(config);
				return 0;
			}
		}
		return 1;
	}
	if (!strcmp("-a",args[1])) {
		for (EventTimer &t : *Config.mutable_timefuses()) {
			if (t.name() == args[2]) {
				unsigned config = t.config();
				bool r = parse_bool(argc,args,3,(config&2) != 0);
				config &= ~2;
				config |= (r << 1);
				t.set_config(config);
				return 0;
			}
		}
		term.println("unknown timer");
		return 1;
	}
	char *e;
	long l = strtol(args[3],&e,0);
	if (*e != 0)
		return arg_invalid(term,args[3]);
	if (!strcmp("-i",args[1]))
		return timefuse_interval_set(args[2],l);
	if (strcmp("-c",args[1]))
		return arg_invalid(term,args[1]);
	EventTimer *t = Config.add_timefuses();
	t->set_name(args[2]);
	t->set_time(l);
	unsigned config = 0;
	if (argc >= 4)
		config |= parse_bool(argc,args,3,false);
	if (argc == 5)
		config |= parse_bool(argc,args,4,false) << 1;
	if (config)
		t->set_config(config);
	timefuse_t r = timefuse_create(t->name().c_str(),l,config&1);
	if ((r != 0) && (config & 2))
		timefuse_start(r);
	return (r == 0);
}


static int wifi(Terminal &term, int argc, const char *args[])
{
	if (argc > 2)
		return arg_invnum(term);
	wifi_mode_t m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_get_mode(&m)) {
		term.printf("get wifi mode: %s\n",esp_err_to_name(e));
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
	} else if (!strcmp(args[1],"ap")) {
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
	} else
		return arg_invalid(term,args[1]);
	if (ESP_OK != esp_wifi_set_mode(m)) {
		term.printf("error changing wifi mode");
	}
	return 0;
}


#ifdef CONFIG_SMARTCONFIG
static int sc(Terminal &term, int argc, const char *args[])
{
	if (argc > 2)
		return arg_invnum(term);
	if (argc == 1)
		term.printf("smartconfig is %srunning",smartconfig_running() ? "" : "not ");
	else if (0 == strcmp(args[1],"start"))
		return smartconfig_start();
	else if (0 == strcmp(args[1],"stop"))
		smartconfig_stop();
	else if (0 == strcmp(args[1],"version"))
		term.println(esp_smartconfig_get_version());
	else
		return arg_invalid(term,args[1]);
	return 0;
}
#endif


#ifdef CONFIG_WPS
static int wps(Terminal &term, int argc, const char *args[])
{
	if (argc > 1)
		return arg_invalid(term,args[1]);
	wifi_wps_start();
	return 0;
}
#endif


static int station(Terminal &term, int argc, const char *args[])
{
	if (argc > 3)
		return arg_invnum(term);
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
		uint8_t x[5];
		uint16_t nm;
		if (5 != sscanf(args[2],"%hhu.%hhu.%hhu.%hhu/%hu",x,x+1,x+2,x+3,&nm)) {
			term.printf("argument must be in the form 192.168.1.1/24\n");
			return 1;
		}
		uint32_t ip = x[0] | (x[1]<<8) | (x[2]<<16) | (x[3]<<24);
		s->set_addr4(ip);
		s->set_netmask4(nm);
	} else if (!strcmp(args[1],"gw")) {
		if (argc != 3)
			return arg_invnum(term);
		uint8_t x[4];
		if (4 != sscanf(args[2],"%hhu.%hhu.%hhu.%hhu",x,x+1,x+2,x+3)) {
			term.printf("argument must be in the form 192.168.1.1\n");
			return 1;
		}
		uint32_t ip = x[0] | (x[1]<<8) | (x[2]<<16) | (x[3]<<24);
		s->set_gateway4(ip);
	} else {
		return arg_invalid(term,args[1]);
	}
	return 0;
}


static int accesspoint(Terminal &term, int argc, const char *args[])
{
	if (argc > 3)
		return arg_invnum(term);
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
			term.printf("ap needs ssid and pass to start\n");
			return 1;
		}
	} else if (!strcmp(args[1],"off")) {
		ap->set_activate(false);
		wifi_stop_softap();
	} else if (!strcmp(args[1],"clear")) {
		Config.clear_softap();
		wifi_stop_softap();
	} else {
		return arg_invalid(term,args[1]);
	}
	return 0;
}


static int debug(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		return arg_invnum(term);
	if (!strcmp("-l",args[1])) {
		log_module_print(term);
		return 0;
	}
	if (0 == term.getPrivLevel())
		return arg_priv(term);
	if (argc != 3) 
		return arg_missing(term);
	if (!strcmp("-d",args[1])) {
		auto *d = Config.mutable_debugs();
		for (auto i = d->begin(), e = d->end(); i != e; ++i) {
			if (*i == args[2]) {
				d->erase(i);
				break;
			}
		}
		return log_module_disable(args[2]);
	}
	if (!strcmp("-e",args[1])) {
		Config.add_debugs(args[2]);
		return log_module_enable(args[2]);
	}
	return arg_invalid(term,args[1]);
}


#ifdef HAVE_FS
static int download(Terminal &term, int argc, const char *args[])
{
	if ((argc < 2) || (argc > 3)) {
		return arg_invnum(term);
	}
	return http_download(term,(char*)args[1], (argc == 3) ? args[2] : 0);
}
#endif


static int nslookup(Terminal &term, int argc, const char *args[])
{
	if (argc != 2) {
		return arg_invnum(term);
	}
	uint32_t a = resolve_hostname(args[1]);
	term.printf("%d.%d.%d.%d\n", a&0xff, (a>>8)&0xff, (a>>16)&0xff, (a>>24)&0xff);
	return (a==0);
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
	if (argc > 3)
		return arg_invnum(term);
	if (argc == 1) {
		if (const char *s = sntp_getservername(0))
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
	if (0 == term.getPrivLevel())
		return arg_priv(term);
	if (!strcmp(args[1],"clear")) {
		Config.clear_sntp_server();
	} else if (!strcmp(args[1],"start")) {
		sntp_init();
	} else if (!strcmp(args[1],"stop")) {
		sntp_stop();
	} else if (!strcmp(args[1],"set")) {
		if (argc != 3)
			return arg_invnum(term);
		sntp_stop();
		Config.set_sntp_server(args[2]);
		sntp_setservername(1,(char*)args[2]);
		sntp_init();
	} else
		return arg_invalid(term,args[1]);
	return 0;
}
#endif


static int timezone(Terminal &term, int argc, const char *args[])
{
	if ((argc != 1) && (argc != 2)) {
		return arg_invnum(term);
	}
	if (argc == 1) {
		if (const char *tz = getenv("TZ"))
			term.printf("timezone: %s\n",tz);
		else
			term.println("timezone not set");
		return 0;
	}
	update_setting(term,"timezone",args[1]);
	return 0;
}


static int xxdSettings(Terminal &t)
{
	size_t s = Config.calcSize();
	uint8_t *buf = (uint8_t *)malloc(s);
	if (buf == 0) {
		t.println("out of memory");
		return 1;
	}
	Config.toMemory(buf,s);
	print_hex(t,buf,s);
	free(buf);
	return 0;
}


static int config(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		return help_cmd(term,args[0]);
	}
	if (0 == strcmp(args[1],"json")) {
		Config.toJSON(term);
	} else if (0 == strcmp(args[1],"print")) {
#ifdef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
		term.println("compiled out");
#else
		Config.toASCII(term);
#endif
	} else if (!strcmp("add",args[1])) {
		if (argc < 3)
			return arg_missing(term);
		char arrayname[strlen(args[2])+4];
		strcpy(arrayname,args[2]);
		strcat(arrayname,"[+]");
		return Config.setByName(arrayname,0) < 0;
	} else if (!strcmp("set",args[1])) {
		if (argc < 4)
			return arg_missing(term);
		if (0 > Config.setByName(args[2],args[3]))
			return 1;
		return 0;
	} else if (!strcmp(args[1],"write")) {
		return cfg_store_nodecfg();
	} else if (!strcmp(args[1],"read")) {
		return cfg_read_nodecfg();
	} else if (!strcmp(args[1],"size")) {
		term << Config.calcSize() << '\n';
		return 0;
	} else if (!strcmp(args[1],"defaults")) {
		cfg_init_defaults();
	} else if (!strcmp(args[1],"activate")) {
		cfg_activate();
	} else if (!strcmp(args[1],"backup")) {
		return cfg_backup_create();
	} else if (!strcmp(args[1],"restore")) {
		return cfg_backup_restore();
	} else if (!strcmp(args[1],"clear")) {
		if (argc != 2)
			return Config.setByName(args[2],0) < 0;
		cfg_clear_nodecfg();
	} else if (!strcmp(args[1],"erase")) {
		if (argc != 2)
			return arg_invnum(term);
		return cfg_erase_nvs();
	} else if (!strcmp(args[1],"xxd")) {
		return xxdSettings(term);
	} else if (!strcmp(args[1],"nvxxd")) {
		size_t s;
		uint8_t *buf = 0;
		if (int e = readNVconfig("node.cfg",&buf,&s))
			return e;
		print_hex(term,buf,s);
		free(buf);
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
		return arg_invnum(term);
	}
	unsigned nt = uxTaskGetNumberOfTasks();
	TaskStatus_t *st = (TaskStatus_t*) malloc(nt*sizeof(TaskStatus_t));
	if (st == 0) {
		term.println("out of memory");
		return 1;
	}
	nt = uxTaskGetSystemState(st,nt,0);
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
	term.println(" ID ST PRIO       TIME STACK CPU NAME");
#else
	term.println(" ID ST PRIO       TIME STACK NAME");
#endif
	for (unsigned i = 0; i < nt; ++i) {
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
		term.printf("%3u  %c %4u %10u %5u %3d %s\n"
#else
		term.printf("%3u  %c %4u %10u %5u %s\n"
#endif
			,st[i].xTaskNumber
			,taskstates[st[i].eCurrentState]
			,st[i].uxCurrentPriority
			,st[i].ulRunTimeCounter
			,st[i].usStackHighWaterMark
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
			,st[i].xCoreID == INT32_MAX ? -1 : st[i].xCoreID
#endif
			,st[i].pcTaskName
			);
	}
	free(st);
	return 0;
}
#endif


static int print_data(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		return arg_invnum(term);
	}
	runtimedata_to_json(term);
	term.write("\n",1);
	return 0;
}


#ifdef CONFIG_OTA
//#ifdef close
//#undef close
//#endif
static int update(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		return help_cmd(term,args[0]);
	if (heap_caps_get_free_size(MALLOC_CAP_32BIT) < (12<<10))
		term.printf("WARNING: memory low! Retry after disabling services.\n");
	UBaseType_t p = uxTaskPriorityGet(0);
	vTaskPrioritySet(0, p > 4 ? p-3 : 1);
	int r = 1;
	if ((argc == 3) && (0 == strcmp(args[1],"-b"))) {
		r = perform_ota(term,(char*)args[2],true);
		term.printf("restarting system...\n");
		term.disconnect();
		vTaskDelay(400);
		esp_restart();
	} else if ((argc == 4) && (0 == strcmp(args[1],"-p"))) {
		r = update_part(term,(char*)args[3],args[2]);
#ifdef CONFIG_ROMFS
		if (r == 0)
			romfs_setup();
#endif
	} else if (argc == 2) {
		r = perform_ota(term,(char*)args[1],false);
	} else {
		return arg_invalid(term,args[1]);
	}
	vTaskPrioritySet(0, p);
	return r;
}
#endif


static int getuptime(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		return arg_invnum(term);
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
		return arg_invnum(term);
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
		esp_chip_info_t ci;
		esp_chip_info(&ci);
		term.printf("%s (rev %d) with %d core%s @ %u MHz"
			, ci.model ? "ESP32" : "ESP8266"
			, ci.revision
			, ci.cores
			, ci.cores > 1 ? "s" : ""
			, mhz
			);
		if (ci.features & CHIP_FEATURE_WIFI_BGN)
			term.print(", 2.4GHz WiFi");
		if (ci.features & CHIP_FEATURE_BT)
			term.print(", Bluetooth");
		if (ci.features & CHIP_FEATURE_BLE) 
			term.print(", Bluetooth LE");
		if (ci.features & CHIP_FEATURE_EMB_FLASH) 
			term.print(", embedded flash");
		term << '\n';
		return 0;
	}
	if (0 == term.getPrivLevel())
		return arg_priv(term);
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
		return arg_invnum(term);
	}
	if (argc == 1) {
		return help_cmd(term,args[0]);
	}
	if (!strcmp(args[1],"-c")) {
		term.printf("clearing password\n");
		Config.clear_pass_hash();
	} else if (!strcmp(args[1],"-r")) {
		term.printf("resetting password to empty estring\n");
		setPassword("");
	} else if (!strcmp(args[1],"-s")) {
		char buf[32];
		term.write(PW,sizeof(PW));
		int n = term.readInput(buf,sizeof(buf),false);
		if (n < 0)
			return 1;
		buf[n] = 0;
		setPassword(buf);
		term.setPrivLevel(0);
		term.println();
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


#ifdef CONFIG_SIGNAL_PROC
static int func(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		return help_cmd(term,args[0]);
	}
	if (!strcmp(args[1],"-l")) {
		Function *f = Function::first();
		while (f) {
			const char *n = f->name();
			const char *t = f->type();
			printf("%-16s %s\n",n?n:"<null>",t?t:"<null>");
			f = f->next();
		}
		return 0;
	}
	if (0 == term.getPrivLevel())
		return arg_priv(term);
	if (!strcmp(args[1],"-a")) {
		FunctionFactory *f = FunctionFactory::first();
		while (f) {
			term.println(f->name());
			f = f->next();
		}
		return 0;
	}
	if (!strcmp(args[1],"-x")) {
		if (argc != 3) 
			return arg_missing(term);
		Function *f = Function::getInstance(args[2]);
		if (f == 0)
			return arg_invalid(term,args[2]);
		f->operator() (0);
		return 0;
	}
	if (!strcmp(args[1],"-c")) {
		if (argc < 4) 
			return arg_missing(term);
		Function *f = FunctionFactory::create(args[3],args[2]);
		if (0 == f)
			return 1;
		FunctionConfig *c = Config.add_functions();
		c->set_name(args[2]);
		c->set_func(args[3]);
		for (int i = 4; i < argc; ++i) {
			c->add_params(args[i]);
			if (DataSignal *s = DataSignal::getSignal(args[i])) {
				if (f->setParam(i-4,s))
					term.printf("error setting parameter %d to %s\n",i,args[i]);
			} else {
				term.printf("unknown signal %s\n",args[i]);
			}
		}
		return 0;
	}
	return arg_invalid(term,args[1]);
}


static int signal(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		return help_cmd(term,args[0]);
	}
	if (!strcmp("-l",args[1])) {
		DataSignal *s = DataSignal::first();
		while (s) {
			term << s->signalName();
			term << ": ";
			s->toStream(term);
			term << '\n';
			s = s->getNext();
		}
		return 0;
	}
	if (0 == term.getPrivLevel())
		return arg_priv(term);
	if (!strcmp("-c",args[1])) {
		if ((argc != 5) && (argc != 4))
			return arg_invnum(term);
		DataSignal *s = 0;
		if (!strcmp(args[2],"int")) {
			s = new IntSignal(strdup(args[3]));
		} else if (!strcmp(args[2],"float")) {
			s = new FloatSignal(strdup(args[3]));
		} else {
			return arg_invalid(term,args[2]);
		}
		if (argc == 4)
			return 0;
		return s->initFrom(args[4]);
	}
	if (!strcmp("-s",args[1])) {
		if (argc != 4)
			return arg_missing(term);
		DataSignal *s = DataSignal::getSignal(args[2]);
		if (0 == s)
			return arg_invalid(term,args[2]);
		return s->initFrom(args[3]);
	}
	if (!strcmp("-f",args[1])) {
		FunctionFactory *f = FunctionFactory::first();
		while (f) {
			term.println(f->name());
			f = f->next();
		}
		return 0;
	}
	if (!strcmp("-a",args[1])) {
		if (argc != 4)
			return arg_invnum(term);
		DataSignal *s = DataSignal::getSignal(args[2]);
		if (s == 0)
			return arg_invalid(term,args[2]);
		Function *f = Function::getInstance(args[3]);
		if (f == 0)
			return arg_invalid(term,args[3]);
		s->addFunction(f);
//		Trigger *t = Config.add_triggers();
//		t->set
		return 0;
	}
	return arg_invalid(term,args[1]);
}
#endif


static int su(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		term.printf("privileg level %u\n",term.getPrivLevel());
		return 0;
	}
	if (argc > 2) {
		return help_cmd(term,args[0]);
	}
	if (!strcmp(args[1],"0")) {
		term.setPrivLevel(0);
		term.printf("new privileg level %u\n",term.getPrivLevel());
		return 0;
	}
	if (strcmp(args[1],"1"))
		return arg_invalid(term,args[1]);
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


static void print_ipinfo(Terminal &t, const char *itf, tcpip_adapter_ip_info_t *i, bool up)
{
	uint8_t m = 0;
	uint32_t nm = ntohl(i->netmask.addr);
	while (nm & (1<<(31-m)))
		++m;
	t.printf("%s: %d.%d.%d.%d/%d gw %d.%d.%d.%d %s\n"
		, itf
		, i->ip.addr & 0xff
		, i->ip.addr>>8 & 0xff
		, i->ip.addr>>16 & 0xff
		, i->ip.addr>>24 & 0xff
		, m
		, i->gw.addr & 0xff
		, i->gw.addr>>8 & 0xff
		, i->gw.addr>>16 & 0xff
		, i->gw.addr>>24 & 0xff
		, up ? "up" : "down"
		);
}


static int ifconfig(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		return arg_invnum(term);
	}
	tcpip_adapter_ip_info_t ipconfig;
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipconfig))
		print_ipinfo(term, "if0/station ", &ipconfig, wifi_station_isup());
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ipconfig))
		print_ipinfo(term, "if1/soft_ap ", &ipconfig, wifi_softap_isup());
#if defined TCPIP_ADAPTER_IF_ETH
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_ETH, &ipconfig))
		print_ipinfo(term, "if2/ethernet", &ipconfig, eth_isup());
#endif
	return 0;
}


static int version(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		return arg_invnum(term);
	}
	term.printf("Atrium Version %s\n"
		"build on " __DATE__ ", " __TIME__ "\n"
		"IDF version: %s\n"
		, Version
		, esp_get_idf_version()
		);
	return 0;
}


extern int adc(Terminal &term, int argc, const char *args[]);
extern int at(Terminal &term, int argc, const char *args[]);
extern int bme(Terminal &term, int argc, const char *args[]);
extern int dht(Terminal &term, int argc, const char *args[]);
extern int dim(Terminal &term, int argc, const char *args[]);
extern int dim(Terminal &t, int argc, const char *argv[]);
extern int distance(Terminal &term, int argc, const char *args[]);
extern int dmesg(Terminal &term, int argc, const char *args[]);
extern int gpio(Terminal &term, int argc, const char *args[]);
extern int hall(Terminal &term, int argc, const char *args[]);
extern int holiday(Terminal &term, int argc, const char *args[]);
extern int inetadm(Terminal &term, int argc, const char *args[]);
extern int influx(Terminal &term, int argc, const char *args[]);
extern int lightctrl(Terminal &term, int argc, const char *args[]);
extern int nightsky(Terminal &term, int argc, const char *args[]);
extern int process(Terminal &term, int argc, const char *args[]);
extern int relay(Terminal &term, int argc, const char *args[]);
extern int shell_format(Terminal &term, int argc, const char *args[]);
extern int status_led(Terminal &term, int argc, const char *args[]);
extern int status(Terminal &term, int argc, const char *args[]);
extern int subtasks(Terminal &term, int argc, const char *args[]);
extern int touchpad(Terminal &term, int argc, const char *args[]);
extern int uart_termcon(Terminal &term, int argc, const char *args[]);
extern int udpc_stats(Terminal &term, int argc, const char *args[]);
extern int udp_stats(Terminal &term, int argc, const char *args[]);
extern int onewire(Terminal &term, int argc, const char *args[]);

static int help(Terminal &term, int argc, const char *args[]);


ExeName ExeNames[] = {
	{"?",0,help,"help",0},
	{"ap",1,accesspoint,"accesspoint settings",ap_man},
	{"adc",0,adc,"A/D converter",adc_man},
	{"action",0,action,"actions",action_man},
#ifdef CONFIG_AT_ACTIONS
	{"at",0,at,"time triggered actions",at_man},
#endif
#ifdef CONFIG_BME280
	{"bme",0,bme,"BME280 sensor",bme_man},
#endif
#ifdef CONFIG_OTA
	{"boot",0,boot,"get/set boot partition",boot_man},
#ifdef CONFIG_INFOCAST
	{"caststats",0,udp_stats,"UDP cast statistics",0},
#endif
#endif
	{"cat",0,shell_cat,"cat file",0},
#ifdef CONFIG_IDF_TARGET_ESP32
	{"cd",0,shell_cd,"change directory",0},
#endif
#ifdef CONFIG_TERMSERV
	{"con",0,uart_termcon,"switch to UART console",0},
#endif
	{"config",1,config,"system configuration",config_man},
	{"cpu",1,cpu,"CPU speed",0},
	{"debug",0,debug,"enable debug logging (* for all)",debug_man},
#ifdef CONFIG_DIMMER
	{"dim",0,dim,"operate dimmer",0},
#endif
#if defined CONFIG_SPIFFS || defined CONFIG_FATFS
	{"df",0,shell_df,"available storage",0},
#endif
#if CONFIG_DMESG
	{"dmesg",0,dmesg,"system log",0},
#endif
#ifdef CONFIG_DHT
	{"dht",0,dht,"DHTxx family of sensors",0},
#endif
#ifdef CONFIG_DIMMER_GPIO
	{"dim",0,dim,"dimmer driver",0},
#endif
#ifdef CONFIG_DIST
	{"dist",0,distance,"hc_sr04 distance measurement",0},
#endif
#ifdef HAVE_FS
	{"download",1,download,"http file download",0},
#endif
	{"event",0,event,"handle trigger events as actions",event_man},
#if defined CONFIG_SPIFFS || defined CONFIG_FATFS
	{"format",1,shell_format,"format storage",0},
#endif
#ifdef CONFIG_SIGNAL_PROC
	{"func",1,func,"function related operations",func_man},
#endif
	//{"flashtest",flashtest,0},
	{"gpio",1,gpio,"GPIO access",0},
#ifdef CONFIG_IDF_TARGET_ESP32
	{"hall",0,hall,"hall sensor",0},
#endif
	{"help",0,help,"help",0},
#ifdef CONFIG_HOLIDAYS
	{"holiday",0,holiday,"holiday settings",holiday_man},
#endif
	{"hwconf",1,hwconf,"hardware configuration",hwconf_man},
	{"ifconfig",0,ifconfig,"network interface settings",0},
#ifdef CONFIG_INETD
	{"inetadm",1,inetadm,"manage network services",inetadm_man},
#endif
#ifdef CONFIG_INFLUX
	{"influx",1,influx,"configure influx UDP data gathering",influx_man},
#endif
#ifdef CONFIG_LIGHTCTRL
	{"lightctrl",0,lightctrl,"light control APP settings",0},
#endif
	{"ls",0,shell_ls,"list storage",0},
	{"mac",0,mac,"MAC addresses",mac_man},
	{"mem",0,mem,"RAM statistics",0},
#ifdef CONFIG_FATFS
	{"mkdir",1,shell_mkdir,"make directory",0},
#endif
#ifdef CONFIG_MQTT
	{"mqtt",1,mqtt,"MQTT settings",mqtt_man},
#endif
#ifdef CONFIG_FATFS
	{"mv",1,shell_mv,"move/rename file",0},
#endif
	{"nodename",0,hostname,"get or set hostname of this device/node",0},
#ifdef CONFIG_NIGHTSKY
	{"ns",0,nightsky,"nightsky operation",0},
#endif
	{"nslookup",0,nslookup,"lookup hostname in DNS",0},
#ifdef CONFIG_ONEWIRE
	{"ow",0,onewire,"one-wire driver access",0},
#endif
	{"part",0,part,"partition table",0},
	{"passwd",1,password,"set password",passwd_man},
#if configUSE_TRACE_FACILITY == 1
	{"ps",0,ps,"task statistics",0},
#endif
	//{"process",1,process,"define and modify processing objects",0},
	{"reboot",1,shell_reboot,"reboot system",0},
#ifdef CONFIG_RELAY
	{"relay",0,relay,"relay status and operation",0},
#endif
	{"restore",1,restore,"restore system defaults",0},
#ifdef HAVE_FS
	{"rm",1,shell_rm,"remove file",0},
#endif
#ifdef CONFIG_FATFS
	{"rmdir",1,shell_rmdir,"remove directory",0},
#endif
#ifdef CONFIG_SMARTCONFIG
	{"sc",1,sc,"SmargConfig actions",0},
#endif
#if 0
	{"segv",1,segv,"trigger a segmentation violation",0},
#endif
	{"set",1,set,"variable settings",set_man},
#ifdef CONFIG_SIGNAL_PROC
	{"signal",0,signal,"view/modify/connect signals",signal_man},
#endif
#ifdef CONFIG_SNTP
	{"sntp",0,sntp,"simple NTP client settings",sntp_man},
#endif
	{"station",1,station,"WiFi station settings",station_man},
#ifdef CONFIG_STATUSLEDS
	{"status",1,status,"status LED",status_man},
#endif
	{"su",0,su,"set user privilege level",su_man},
#ifdef CONFIG_SUBTASKS
	{"subtasks",0,subtasks,"cyclic tasks procedures",0},
#endif
	{"timer",1,timefuse,"create timer",timer_man},
	{"timezone",1,timezone,"set time zone for NTP",0},
#ifdef HAVE_FS
	{"touch",1,shell_touch,"create file, update time-stamp",0},
#endif
#ifdef CONFIG_TOUCHPAD
	{"tp",1,touchpad,"touchpad output",0},
#endif
#ifdef CONFIG_UDPCTRL
	{"udpcstat",0,udpc_stats,"display UDP statistics",0},
#endif
#ifdef CONFIG_OTA
	{"update",1,update,"OTA download procedure",update_man},
#endif
	{"uptime",0,getuptime,"time system is up and running",0},
	{"version",0,version,"version description",0},
	{"webdata",0,print_data,"data provided to webpages",0},
	{"wifi",1,wifi,"wifi station/AP mode",wifi_man},
#ifdef CONFIG_WPS
	{"wps",1,wps,"wifi configuration with WPS",0},
#endif
#ifdef HAVE_FS
	{"xxd",0,shell_xxd,"hex output of file",0},
#endif
};


int help_cmd(Terminal &term, const char *arg)
{
#ifdef CONFIG_INTEGRATED_HELP
	for (int i = 0; i < sizeof(ExeNames)/sizeof(ExeName); ++i) {
		if (!strcmp(ExeNames[i].name,arg)) {
			if (ExeNames[i].help) {
				term.print(ExeNames[i].help);
				return 0;
			}
			break;
		}
	}
#endif
	char tmp[strlen(arg)+6];
#ifdef CONFIG_ROMFS
	strcpy(tmp,arg);
	strcat(tmp,".man");
	if (0 == shell_cat1(term,tmp))
		return 0;
#endif
	strcpy(tmp,"/man/");
	strcat(tmp,arg);
	if (0 == shell_cat1(term,tmp))
		return 0;
	term.printf("no help page for %s\n",arg);
	return 1;
}


static int help(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		term.printf("help -l: list available commands\n"
			"help <cmd>: print help for command <cmd>\n"
			);
	} else if (!strcmp("-l",args[1])) {
		for (int i = 2; i < sizeof(ExeNames)/sizeof(ExeNames[0]); ++i) 
			term.printf("%-14s %-5s  %s\n",ExeNames[i].name,ExeNames[i].plvl?"admin":"user",ExeNames[i].descr);
	} else {
		return help_cmd(term,args[1]);
	}
	return 0;
}


int shellexe(Terminal &term, const char *cmd)
{
	TimeDelta td(__FUNCTION__);
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
				return arg_priv(term);
			}
			if ((n == 2) && (0 == strcmp("-h",args[1])))
				return help_cmd(term,args[0]);
			//term.printf("calling shell function '%s'\n",ExeNames[i].name);
			int r = ExeNames[i].function(term,n,(const char **)args);
			return r;
		}
	}
	term.printf("'%s': command not found\n",args[0]);
	return 1;
}


void shell(Terminal &term)
{
	char com[128];
	term.write("> ",2);
	int r = term.readInput(com,sizeof(com)-1,true);
	while (r > 0) {
		term.print("\n");
		com[r] = 0;
		if ((r == 4) && !memcmp(com,"exit",4))
			break;
		if (shellexe(term,com))
			term.printf("\nError.\n> ");
		else
			term.printf("\nOK.\n> ");
		r = term.readInput(com,sizeof(com)-1,true);
	}
}
