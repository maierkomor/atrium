/*
 *  Copyright (C) 2018-2022, Thomas Maier-Komor
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
#include "console.h"
#include "cyclic.h"
#include "dataflow.h"
#include "event.h"
#include "fs.h"
#include "func.h"
#include "globals.h"
#include "hwcfg.h"
#include "leds.h"
#include "log.h"
#include "nvm.h"
#include "ota.h"
#include "memfiles.h"
#include "netsvc.h"
#include "profiling.h"
#include "romfs.h"
#include "settings.h"
#include "shell.h"
#include "sntp.h"
#include "support.h"
#include "swcfg.h"
#include "terminal.h"
#include "timefuse.h"
#include "env.h"
#include "wifi.h"

#include <float.h>
#include <sstream>

#include <esp_image_format.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_wifi.h>
#include <tcpip_adapter.h>

#define TAG MODULE_SHELL

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

extern "C" {
#include <dirent.h>
}

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
	uint8_t flags;	// 0 = no priviliges required, 1 = user admin required
	const char *(*function)(Terminal &term, int argc, const char *arg[]);
	const char *descr;
	const char *help;
};

static estring PWD;
static const char PW[] = "password:", NotSet[] = "<not set>";

extern "C"
const char *getpwd()
{
	return PWD.c_str();
}


static void action_print(void *p, const Action *a)
{
	if (a == 0)
		return;
	Terminal *t = (Terminal*)p;
	if (a->text)
		t->printf("\t%-24s  %s\n",a->name,a->text);
}


static void action_perf(void *p, const Action *a)
{
	if (a == 0)
		return;
	Terminal *t = (Terminal*)p;
	if (a->num)
		t->printf("\t%-20s %5u %6u (%6u/%6u/%6u)\n",a->name,a->num,a->sum,a->min,a->sum/a->num,a->max);
}


static const char *action(Terminal &t, int argc, const char *args[])
{
	if (argc == 1)
		return help_cmd(t,args[0]);
	if (argc == 3) {
		char *arg = strdup(args[2]);
		if (action_activate_arg(args[1],arg))
			return "Invalid argument #1.";
	} else if (argc != 2) {
		return "Invalid number of arguments.";
	} else if (0 == strcmp(args[1],"-l")) {
		action_iterate(action_print,(void*)&t);
	} else if (0 == strcmp(args[1],"-p")) {
		t.printf("\t%-20s %5s %6s (%6s/%6s/%6s)\n","name","count","total","min.","avg.","max.");
		action_iterate(action_perf,(void*)&t);
	} else if (!strcmp(args[1],"-F")) {
		if (0 == t.getPrivLevel())
			return "Access denied.";
		Config.set_actions_enable(Config.actions_enable()|2);
	} else if (!strcmp(args[1],"-f")) {
		if (0 == t.getPrivLevel())
			return "Access denied.";
		Config.set_actions_enable(Config.actions_enable()&~2);
	} else if (action_activate(args[1])) {
		return "Invalid argument #1.";
	}
	return 0;
}


static void print_mac(Terminal &t, const char *i, uint8_t mac[])
{
	t.printf("%-8s: %02x:%02x:%02x:%02x:%02x:%02x\n",i,mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}


static int get_ip(const char *str, uint8_t *ip)
{
	unsigned a[5];
	// %hhu does not work on ESP8266 nano-formating library!
	int n = sscanf(str,"%u.%u.%u.%u/%u",&a[0],&a[1],&a[2],&a[3],&a[4]);
//	con_printf("%u.%u.%u.%u/%u",a[0],a[1],a[2],a[3],a[4]);
	int x = 0;
	while (x < n) {
		if (a[x] > 0xff)
			return 0;
		ip[x] = a[x];
		++x;
	}
	return n;
}


static void print_obj(Terminal &t, EnvObject *o, int indent)
{
	unsigned c = 0;
	while (EnvElement *e = o->getChild(c++)) {
		const char *n = e->name();
		if (0 == strcmp(n,"mqtt"))
			continue;
		for (int i = 0; i < indent; ++i)
			t << "    ";
		t << n;
		if (EnvObject *c = e->toObject()) {
			t << ":\n";
			print_obj(t,c,indent+1);
		} else {
			t << ": ";
			e->writeValue(t);
			if (const char *d = e->getDimension()) {
				t << ' ';
				t.print(d);
			}
			t << '\n';
		}
	}
}


static const char *env(Terminal &t, int argc, const char *args[])
{
	rtd_lock();
	print_obj(t,RTData,0); 
	rtd_unlock();
	return 0;
}


static const char *event(Terminal &t, int argc, const char *args[])
{
	if (argc == 1) {
		return help_cmd(t,args[0]);
	} else if (argc == 2) {
		if (!strcmp(args[1],"-l")) {
			event_t e = 0;
			while (const char *name = event_name(++e)) {
				if (name[0] == '*')
					continue;
				uint64_t time = event_time(e);
				const char *s = "num kM";
				while (time > 30000) {
					time /= 1000;
					++s;
				}
				t.printf("%s (%ux, %lu%cs) =>\n",name,event_occur(e),(unsigned long)time,*s);
				const EventHandler *h = event_handler(e);
				for (const auto &c : h->callbacks)
					t.printf("\t%s (%s)%s\n", c.action->name, c.arg ? c.arg : "", c.enabled ? "" : " [disabled]");
			}
		} else {
			return "Invalid argument #1.";
		}
		return 0;
	} else if (argc == 3) {
		if (!strcmp(args[1],"-t")) {
			if (event_t e = event_id(args[2])) {
				event_trigger(e);
				return 0;
			}
		}
	} else if (argc == 4) {
		if (0 == t.getPrivLevel())
			return "Access denied.";
		Action *a = action_get(args[3]);
		if (a == 0)
			return "Invalid argument #3.";
		event_t e = event_id(args[2]);
		if (e == 0) {
			return "Invalid argument #2.";
		} else if (!strcmp(args[1],"-a")) {
			event_callback(e,a);
			for (auto &t : *Config.mutable_triggers()) {
				if (t.event() == args[2]) {
					t.add_action(args[3]);
					return 0;
				}
			}
			Trigger *t = Config.add_triggers();
			t->set_event(args[2]);
			t->add_action(args[3]);
			return 0;
		} else  if (!strcmp(args[1],"-d")) {
			event_detach(e,a);
			int r = 1;
			auto &t = *Config.mutable_triggers();
			for (auto i = t.begin(), e = t.end(); i != e; ++i) {
				if (i->event() == args[2]) {
					auto &ma = *i->mutable_action();
					auto j = ma.begin();
					while (j != ma.end()) {
						if (*j == args[3]) {
							j = ma.erase(j);
							r = 0;
						} else
							++j;
					}
				}
			}
			return r ? "Failed." : 0;
		} else {
			return "Invalid argument #1.";
		} 
	} else if (argc == 5) {
		if (0 == t.getPrivLevel())
			return "Access denied.";
		Action *a = action_get(args[3]);
		if (a == 0)
			return "Invalid argument #3.";
		event_t e = event_id(args[2]);
		if (e == 0) {
			return "Invalid argument #2.";
		} else if (!strcmp(args[1],"-a")) {
			size_t cl = strlen(args[3]);
			char *arg = (char*) malloc(cl+strlen(args[4])+2);
			memcpy(arg,args[3],cl);
			arg[cl] = ' ';
			strcpy(arg+cl+1,args[4]);
			event_callback_arg(e,a,strdup(args[4]));
			for (auto &t : *Config.mutable_triggers()) {
				if (t.event() == args[2]) {
					t.add_action(arg);
					return 0;
				}
			}
			Trigger *t = Config.add_triggers();
			t->set_event(args[2]);
			t->add_action(arg);
			return 0;
		} else {
			return "Invalid argument #1.";
		} 
	} else {
		return "Invalid number of arguments.";
	}
	return "Invalid argument.";
}


#ifdef CONFIG_IDF_TARGET_ESP32
static const char *shell_cd(Terminal &term, int argc, const char *args[])
{
	if (argc > 2)
		return "Invalid number of arguments.";
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
static const char *shell_rm(Terminal &term, int argc, const char *args[])
{
	if (argc != 2)
		return "Invalid number of arguments.";
	const char *err = 0;
	int a = 1;
	while (a < argc) {
		estring fn;
		if (args[a][0] != '/')
			fn = PWD;
		fn += args[a];
		if (-1 == unlink(fn.c_str())) {
			err = strerror(errno);
		}
		++a;
	}
	return err;
}
#endif


#ifdef CONFIG_FATFS
static const char *shell_mkdir(Terminal &term, int argc, const char *args[])
{
	if (argc != 2)
		return "Invalid number of arguments.";
	estring dn;
	if (args[1][0] != '/')
		dn = PWD;
	dn += args[1];
	if (-1 == mkdir(dn.c_str(),0777)) {
		return strerror(errno);
	}
	return 0;
}


static const char *shell_rmdir(Terminal &term, int argc, const char *args[])
{
	if (argc != 2)
		return "Invalid number of arguments.";
	estring dn;
	if (args[1][0] != '/')
		dn = PWD;
	dn += args[1];
	if (-1 == rmdir(dn.c_str())) {
		return strerror(errno);
	}
	return 0;
}


static const char *shell_mv(Terminal &term, int argc, const char *args[])
{
	if (argc != 3)
		return "Invalid number of arguments.";
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
		return strerror(errno);
	}
	if (-1 == link(args[1],args[2])) {
		return strerror(errno);
	}
	if (-1 == unlink(args[1])) {
		return strerror(errno);
	}
	return 0;
}
#endif


static const char *shell_ls(Terminal &term, int argc, const char *args[])
{
#if defined CONFIG_ROMFS
	if (unsigned n = romfs_num_entries()) {
		for (int i = 0; i < n; ++i) {
			const char *n = romfs_name(i);
			assert(n);
			size_t s,o;
			romfs_getentry(n,&s,&o);
			term.printf("\t%5u %-12s\n",s,n);
		}
		return 0;
	}
#endif
#ifdef HAVE_FS
	if (argc == 1) {
		estring pwd = PWD;
		if (pwd.back() == '/')
			pwd.resize(pwd.size()-1);
		DIR *d = opendir(pwd.c_str());
		if (d == 0) {
			return strerror(errno);
		}
		while (struct dirent *e = readdir(d))
			term.println(e->d_name);
		closedir(d);
		return 0;
	}
	bool nlst = false;
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
	return 0;
#else
	return "no filesystem";
#endif
}


static const char *shell_cat1(Terminal &term, const char *arg)
{
#ifdef CONFIG_ROMFS
	const char *fn = arg;
	if (fn[0] == '/')
		++fn;
	int r = romfs_open(fn);
	if (r >= 0) {
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
	}
#endif
#ifdef HAVE_FS
	char *filename;
	if (arg[0] == '/') {
		filename = (char*)arg;
	} else {
		size_t p = PWD.size();
		size_t a = strlen(arg);
		filename = (char *) malloc(a+p+1);
		if (filename == 0)
			return "Out of memory.";
		memcpy(filename,PWD.data(),p);
		memcpy(filename+p,arg,a+1);
	}
	int fd = open(filename,O_RDONLY);
	if (filename != arg)
		free(filename);
	if (fd < 0)
		return strerror(errno);
	struct stat st;
	if (0 != fstat(fd,&st)) {
		int e = errno;
		close(fd);
		return strerror(e);
	}
	char buf[512];
	int t = 0;
	do {
		int n = read(fd,buf,sizeof(buf));
		if (n == -1) {
			int e = errno;
			close(fd);
			return strerror(e);
		}
		t += n;
		term.print(buf,n);
	} while (t < st.st_size);
	close(fd);
	return 0;
#else
	return "No filesystem.";
#endif
}

static const char *shell_cat(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		return "Missing argument.";
	int r = 0;
	for (int i = 1; i < argc; ++i)
		r |= shell_cat1(term,args[i]) != 0;
	return r ? "Failed." : 0;
}


void print_hex(Terminal &term, uint8_t *b, size_t s, size_t off = 0)
{
	uint8_t *a = b, *e = b + s;
	while (a != e) {
		char tmp[64], *t = tmp;
		t += sprintf(t,"%04x: ",a-b+off);
		int i = 0;
		while ((a < e) && (i < 16)) {
			*t++ = ' ';
			if (i == 8)
				*t++ = ' ';
			t += sprintf(t,"%02x",*a);
			++i;
			++a;
		}
		*t = 0;
		term.println(tmp);
	}
}


#ifdef HAVE_FS
static const char *shell_touch(Terminal &term, int argc, const char *args[])
{
	if (argc != 2) {
		return "Invalid number of arguments.";
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
		return strerror(errno);
	}
	close(fd);
	return 0;
}


static const char *shell_xxd(Terminal &term, int argc, const char *args[])
{
	if (argc != 2) {
		return "Invalid number of arguments.";
	}
	int fd = open(args[1],O_RDONLY);
	if (fd == -1) {
		return strerror(errno);
	}
	struct stat st;
	if (-1 == fstat(fd,&st)) {
		close(fd);
		return strerror(errno);
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


static const char *shell_reboot(Terminal &term, int argc, const char *args[])
{
	if (argc != 1)
		return "Invalid number of arguments.";
	esp_restart();
	return 0;
}


#if defined CONFIG_SPIFFS || defined CONFIG_FATFS
static const char *shell_df(Terminal &term, int argc, const char *args[])
{
	if (argc != 1)
		return "Invalid number of arguments.";
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
	term.printf("error: %d\n",err);
#endif
	return "No filesystem.";
}
#endif


static const char *part(Terminal &term, int argc, const char *args[])
{
	if (argc == 3) {
		if (strcmp(args[1],"erase"))
			return "Invalid argument #1.";
		if (esp_partition_iterator_t i = esp_partition_find(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,args[2])) {
			if (const esp_partition_t *p = esp_partition_get(i)) {
				if (esp_err_t e = spi_flash_erase_range(p->address,p->size)) {
					return esp_err_to_name(e);
				}
				return 0;
			}
		}
		return "Invalid argument #2.";
	}
	if (argc != 1)
		return "Invalid number of arguments.";
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
		term.printf("%s %02x %7d@%08x %s\n",p->type ? "data" : "app ", p->subtype, p->size, p->address, p->label);
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


static const char *mem(Terminal &term, int argc, const char *args[])
{
	term.printf(
		"32bit mem   : %u\n"
		"8bit mem    : %u\n"
		"DMA  mem    : %u\n"
		,heap_caps_get_free_size(MALLOC_CAP_32BIT)
		,heap_caps_get_free_size(MALLOC_CAP_8BIT)
		,heap_caps_get_free_size(MALLOC_CAP_DMA));
#ifdef CONFIG_IDF_TARGET_ESP32
	term.printf("exec mem    : %u\n",heap_caps_get_free_size(MALLOC_CAP_EXEC));
	term.printf("SPI  mem    : %u\n",heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
	term.printf("internal mem: %u\n",heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif
#ifdef CONFIG_VERIFY_HEAP
	if (0 == strcmp(args[1],"-c")) {
		return false == heap_caps_check_integrity_all(true) ? "Failed." : 0;
	}
	if (0 == strcmp(args[1],"-d")) {
		heap_caps_dump_all();
	}
#endif
	return 0;
}


const char *mac(Terminal &term, int argc, const char *args[])
{
	uint8_t mac[6];
	if (argc == 1) {
		return help_cmd(term,args[0]);
	} else if (argc == 2) {
		if (!strcmp("-l",args[1])) {
			if (ESP_OK == esp_wifi_get_mac(WIFI_IF_AP,mac))
				print_mac(term,"softap",mac);
			if (ESP_OK == esp_wifi_get_mac(WIFI_IF_STA,mac))
				print_mac(term,"station",mac);
			return 0;
		}
	}
	if (0 == term.getPrivLevel())
		return "Access denied.";
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
			return "Invalid argument #2.";
		for (int i = 0; i < 6; ++i) {
			if (inp[i] > 0xff)
				return "Invalid argument #2.";
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
			return "Invalid argument #1.";
		}
		if (esp_err_t e = esp_wifi_set_mac(w,mac)) {
			return esp_err_to_name(e);
		}
		return 0;
	}
	return "Invalid argument #1.";
}


#if 0
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
		return "Invalid number of arguments.";
	return "Invalid argument #1.";
}
#endif


static const char *hostname(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		return "Invalid number of arguments.";
	}
	if (argc == 1) {
//		term.printf("%.*s.%.*s\n",HostnameLen,Hostname,DomainnameLen,Domainname);
		term.println(Hostname);
		return 0;
	}
	if (0 == term.getPrivLevel())
		return "Access denied.";
	if (cfg_set_hostname(args[1]))
		return "Failed.";
	Config.set_nodename(args[1]);
	return 0;
}


static const char *parse_xxd(Terminal &term, vector<uint8_t> &buf)
{
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
			return "";
		}
		if (++x == 2) {
			buf.push_back(b);
			x = 0;
			b = 0;
		}
	}
	return 0;
}


static const char *hwconf(Terminal &term, int argc, const char *args[])
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
#ifdef CONFIG_HWCONF_DYNAMIC
	} else if (!strcmp("add",args[1])) {
		if (argc < 3)
			return "Missing argument.";
		char arrayname[strlen(args[2])+4];
		strcpy(arrayname,args[2]);
		strcat(arrayname,"[+]");
		return HWConf.setByName(arrayname,0) < 0 ? "Failed" : 0;
	} else if (!strcmp("set",args[1])) {
		if (argc < 4)
			return "Missing argument.";
		return (0 > HWConf.setByName(args[2],args[3])) ? "Failed" : 0;
	} else if (!strcmp("reset",args[1])) {
		return cfg_read_hwcfg() >= 0 ? "Failed" : 0;
	} else if (!strcmp("clear",args[1])) {
		if (argc == 3)
			return HWConf.setByName(args[2],0) < 0 ? "Failed" : 0;
		if (argc != 2)
			return "Invalid number of arguments.";
		HWConf.clear();
		HWConf.set_magic(0xAE54EDCB);
	} else if (!strcmp("xxd",args[1])) {
		size_t s = HWConf.calcSize();
		uint8_t *buf = (uint8_t *) malloc(s);
		HWConf.toMemory(buf,s);
		print_hex(term,buf,s);
		free(buf);
	} else if (!strcmp("print",args[1])) {
		HWConf.toASCII(term);
	} else if (!strcmp("show",args[1])) {
		HWConf.toASCII(term);
	} else if (!strcmp("json",args[1])) {
		HWConf.toJSON(term);
	} else if (!strcmp("write",args[1])) {
		return cfg_store_hwcfg() ? "Failed" : 0;
#else
	} else if (!strcmp("clear",args[1])) {
		if (argc != 2)
			return "Invalid number of arguments.";
		HWConf.clear();
		HWConf.set_magic(0xAE54EDCB);
#endif
	} else if (!strcmp("nvxxd",args[1])) {
		size_t s;
		uint8_t *buf = 0;
		if (int e = nvm_read_blob("hw.cfg",&buf,&s)) {
			return esp_err_to_name(e);
		}
		print_hex(term,buf,s);
		free(buf);
	} else if (!strcmp("parsexxd",args[1])) {
		if (parse_xxd(term,hwcfgbuf))
			return "Invalid hex input.";
		term.printf("parsing %u bytes\n",hwcfgbuf.size());
		HardwareConfig nc;
		int e = nc.fromMemory(hwcfgbuf.data(),hwcfgbuf.size());
		if (0 >= e) {
			term.printf("parser error: %d\n",e);
			return "";
		}
		HWConf = nc;
	} else if (!strcmp("writebuf",args[1])) {
		return nvm_store_blob("hw.cfg",hwcfgbuf.data(),hwcfgbuf.size()) ? "Failed." : 0;
	} else if (!strcmp("clearbuf",args[1])) {
		hwcfgbuf.clear();
	} else if (!strcmp("xxdbuf",args[1])) {
		print_hex(term,hwcfgbuf.data(),hwcfgbuf.size());
	} else {
		return "Invalid argument #1.";
	}
	return 0;
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


static void ms_to_timestr(char *buf, unsigned long ms)
{
	unsigned h,m,s;
	h = ms / (60UL*60UL*1000UL);
	ms -= h * (60UL*60UL*1000UL);
	m = ms / (60*1000);
	ms -= m * (60*1000);
	s = ms / 1000;
	ms -= s * 1000;
	/*
	if (h)
		sprintf(buf,"%u:%02u:%02u.%03lu h",h,m,s,ms);
	else if (m)
		sprintf(buf,"%u:%02u.%03lu m",m,s,ms);
	else if (s)
		sprintf(buf,"%u.%03lu s",s,ms);
	else
		sprintf(buf,"%lu ms",ms);
	return buf;
	*/
	char *b = buf;
	if (h)
		b += sprintf(b,"%u:",h);
	if (h || m)
		b += sprintf(b,"%02u:",m);
	if (h || m || s)
		b += sprintf(b,"%02u.",s);
	sprintf(b,"%lu",ms);
}


static const char *timefuse(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		return "Invalid number of arguments.";
	if (argc == 2) {
		if (!strcmp("-l",args[1])) {
			timefuse_t t = timefuse_iterator();
			// active timers cannot be queried for repeat!
			term.printf("%-16s %-16s active\n","name","interval");
			while (t) {
				char buf[20];
				int on = timefuse_active(t);
				ms_to_timestr(buf,timefuse_interval_get(t));
				term.printf("%-16s %-16s %-7s\n"
						,timefuse_name(t)
						,buf
						,on==0?"false":on==1?"true":"unknown");
				t = timefuse_next(t);
			}
			return 0;
		}
		return "Invalid argument #1.";
	}
	if (0 == term.getPrivLevel())
		return "Access denied.";
	if (argc == 3) {
		if (!strcmp("-s",args[1]))
			return timefuse_start(args[2]) ? "Failed." : 0;
		if (!strcmp("-t",args[1]))
			return timefuse_stop(args[2]) ? "Failed." : 0;
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
			return "No such timer.";
			// return timefuse_delete(args[2]);	-- incomplete
		}
		return "Invalid argument #1.";
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
		return "Invalid argument #1.";
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
		return "Invalid argument #1.";
	}
	char *e;
	long ms = strtol(args[3],&e,10);
	if ((e == args[3]) || (ms <= 0))
		return "Invalid argument #3.";
	if (*e == ' ')
		++e;
	switch (*e) {
	default:
		return "Invalid argument #3.";
	case 'h':
		ms *= 60*60*1000;
		break;
	case 'm':
		if (e[1] == 's')
			;	// milli-seconds
		else if (e[1] == 0)
			ms *= 60000;
		else
			return "Invalid argument #3.";
		break;
	case 's':
		ms *= 1000;
		break;
	case 0:;
	}
	if (0 == strcmp("-i",args[1])) {
		for (auto &t : *Config.mutable_timefuses()) {
			if (t.name() == args[2]) {
				t.set_time(ms);
				return timefuse_interval_set(args[2],ms) ? "Failed." : 0;
			}
		}
		return "Invalid argument #1.";
	}
	if (strcmp("-c",args[1]))
		return "Invalid argument #1.";
	EventTimer *t = Config.add_timefuses();
	t->set_name(args[2]);
	t->set_time(ms);
	unsigned config = 0;
	if (argc >= 5)
		config |= parse_bool(argc,args,4,false);
	if (argc == 6)
		config |= parse_bool(argc,args,5,false) << 1;
	if (config)
		t->set_config(config);
	timefuse_t r = timefuse_create(t->name().c_str(),ms,config&1);
	if ((r != 0) && (config & 2))
		timefuse_start(r);
	return (r == 0) ? "Failed." : 0;
}


static const char *wifi(Terminal &term, int argc, const char *args[])
{
	if (argc > 2)
		return "Invalid number of arguments.";
	term.printf("station mode %d\n",StationMode);
	wifi_mode_t m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_get_mode(&m)) {
		return esp_err_to_name(e);
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
		term.printf("softap is %s\n",m & 2 ? "on" : "off");
		/*
		if (m & 1) {
			uint8_t st = wifi_station_get_connect_status();
			term.printf("station status %u\n",st);
		}
		*/
	} else
		return "Invalid argument #1.";
	if (ESP_OK != esp_wifi_set_mode(m)) {
		term.println("error changing wifi mode");
	}
	return 0;
}


#ifdef CONFIG_SMARTCONFIG
static const char *sc(Terminal &term, int argc, const char *args[])
{
	if (argc > 2)
		return "Invalid number of arguments.";
	if (argc == 1)
		term.printf("smartconfig is %srunning",smartconfig_running() ? "" : "not ");
	else if (0 == strcmp(args[1],"start"))
		return smartconfig_start() ? "Failed." : 0;
	else if (0 == strcmp(args[1],"stop"))
		smartconfig_stop();
	else if (0 == strcmp(args[1],"version"))
		term.println(esp_smartconfig_get_version());
	else
		return "Invalid argument #1.";
	return 0;
}
#endif


#ifdef CONFIG_WPS
static const char *wps(Terminal &term, int argc, const char *args[])
{
	if (argc > 1)
		return "Invalid argument #1.";
	wifi_wps_start();
	return 0;
}
#endif


static const char *station(Terminal &term, int argc, const char *args[])
{
	if (argc > 3)
		return "Invalid number of arguments.";
	WifiConfig *s = Config.mutable_station();
	if (argc == 1) {
		term.printf("ssid: %s\npass: %s\nactive: %s\n"
			, s->has_ssid() ? s->ssid().c_str() : NotSet
			, s->has_pass() ? s->pass().c_str() : NotSet
			, s->activate() ? "true" : "false"
			);
		if (s->has_addr4()) {
			uint32_t a = s->addr4();
			char ipstr[32];
			ip4addr_ntoa_r((ip4_addr_t *)&a,ipstr,sizeof(ipstr));
			term.printf("addr: %s/%d\n",ipstr,s->netmask4());
		}
		if (s->has_gateway4()) {
			uint32_t a = s->gateway4();
			char ipstr[32];
			ip4addr_ntoa_r((ip4_addr_t *)&a,ipstr,sizeof(ipstr));
			term.printf("gw: %s\n",ipstr);
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
			return "ssid and pass needed";
		}
	} else if (!strcmp(args[1],"off")) {
		s->set_activate(false);
		wifi_stop_station();
	} else if (!strcmp(args[1],"clear")) {
		Config.clear_station();
		wifi_stop_station();
	} else if (!strcmp(args[1],"ip")) {
		if (argc != 3) {
			return "Invalid number of arguments.";
		}
		if (!strcmp(args[2],"-c")) {
			s->clear_addr4();
			s->clear_netmask4();
			s->clear_gateway4();
			return 0;
		}
		uint8_t x[5];
		if (5 != get_ip(args[2],x))
			return "Invalid argument #2.";
		uint32_t ip = x[0] | (x[1]<<8) | (x[2]<<16) | (x[3]<<24);
		s->set_addr4(ip);
		s->set_netmask4(x[4]);
	} else if (!strcmp(args[1],"gw")) {
		if (argc != 3)
			return "Invalid number of arguments.";
		uint8_t x[5];
		if (4 != get_ip(args[2],x))
			return "Invalid argument #2.";
		uint32_t ip = x[0] | (x[1]<<8) | (x[2]<<16) | (x[3]<<24);
		s->set_gateway4(ip);
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}


static const char *accesspoint(Terminal &term, int argc, const char *args[])
{
	if (argc > 3)
		return "Invalid number of arguments.";
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
			return "Failed.";
	} else if ((!strcmp(args[1],"on")) || (!strcmp(args[1],"up"))) {
		if (ap->has_ssid()) {
			ap->set_activate(true);
			if (!wifi_start_softap(ap->ssid().c_str(),ap->has_pass() ? ap->pass().c_str() : ""))
				return "Failed.";
		} else {
			return "ssid needed";
		}
	} else if (!strcmp(args[1],"off")) {
		ap->set_activate(false);
		wifi_stop_softap();
	} else if (!strcmp(args[1],"clear")) {
		Config.clear_softap();
		wifi_stop_softap();
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}


static const char *debug(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		return "Invalid number of arguments.";
	if (!strcmp("-a",args[1])) {
		for (int m = 1; m < NUM_MODULES; ++m)
			term.println(ModNames+ModNameOff[(logmod_t)m]);
		return 0;
	}
	if (!strcmp("-l",args[1])) {
		log_module_print(term);
		return 0;
	}
	if (0 == term.getPrivLevel())
		return "Access denied.";
	if (argc != 3) 
		return "Missing argument.";
	if (!strcmp("-d",args[1])) {
		auto *d = Config.mutable_debugs();
		for (auto i = d->begin(), e = d->end(); i != e; ++i) {
			if (*i == args[2]) {
				d->erase(i);
				break;
			}
		}
		return log_module_disable(args[2]) ? "Failed." : 0;
	}
	if (!strcmp("-e",args[1])) {
		if (log_module_enable(args[2]))
			return "Invalid argument #2.";
		Config.add_debugs(args[2]);
		return 0;
	}
	return "Invalid argument #1.";
}


#if defined HAVE_FS && defined CONFIG_OTA
static const char *download(Terminal &term, int argc, const char *args[])
{
	if ((argc < 2) || (argc > 3)) {
		return "Invalid number of arguments.";
	}
	return http_download(term,(char*)args[1], (argc == 3) ? args[2] : 0);
}
#endif


static const char *nslookup(Terminal &term, int argc, const char *args[])
{
	if (argc != 2) {
		return "Invalid number of arguments.";
	}
	ip_addr_t ip;
	if (err_t e = resolve_hostname(args[1],&ip))
		return strlwiperr(e);
	return inet_ntoa(ip);
}


#if 0
static int segv(Terminal &term, int argc, const char *args[])
{
	term.printf("triggering segment violoation\n");
	*(char*)0 = 1;
	return 0;
}
#endif


static const char *sntp(Terminal &term, int argc, const char *args[])
{
	if (argc > 3)
		return "Invalid number of arguments.";
	if (argc == 1) {
		if (Config.has_sntp_server())
			term.printf("sntp server: %s\n",Config.sntp_server().c_str());
		time_t now;
		time(&now);
		char buf[64];
		term.printf("UTC time   : %s",asctime_r(localtime(&now),buf));
		int64_t lu = sntp_last_update();
		if (lu) {
			int64_t now = esp_timer_get_time();
#ifdef CONFIG_NEWLIB_LIBRARY_LEVEL_FLOAT_NANO
			term.printf("last update: %3.1f sec ago\n",(double)(now-lu)*1E-6);
#else
			char buf[16];
			float_to_str(buf,(float)(now-lu)*1E-6);
			term.printf("last update: %s sec ago\n",buf);
#endif
		}
		if (const char *tz = Config.timezone().c_str()) {
			term.printf("timezone   : %s\n",tz);
			uint8_t h,m;
			get_time_of_day(&h,&m);
			term.printf("local time : %u:%02u\n",h,m);
		}
		return 0;
	}
	if (0 == term.getPrivLevel())
		return "Access denied.";
	if (!strcmp(args[1],"clear")) {
		Config.clear_sntp_server();
		sntp_set_server(0);
	} else if (!strcmp(args[1],"set")) {
		if (argc != 3)
			return "Invalid number of arguments.";
		Config.set_sntp_server(args[2]);
		sntp_set_server(args[2]);
	} else
		return "Invalid argument #1.";
	return 0;
}


static const char *timezone(Terminal &term, int argc, const char *args[])
{
	if ((argc != 1) && (argc != 2)) {
		return "Invalid number of arguments.";
	}
	if (argc == 1) {
		if (const char *tz = Config.timezone().c_str())
			term.printf("timezone: %s\n",tz);
		else
			term.println("timezone not set");
		return 0;
	}
	return update_setting(term,"timezone",args[1]) ? "Failed." : 0;
}


static const char *xxdSettings(Terminal &t)
{
	size_t s = Config.calcSize();
	uint8_t *buf = (uint8_t *)malloc(s);
	if (buf == 0)
		return "Out of memory.";
	Config.toMemory(buf,s);
	print_hex(t,buf,s);
	free(buf);
	return 0;
}


static const char *config(Terminal &term, int argc, const char *args[])
{
	static vector<uint8_t> rtcfgbuf;
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
			return "Missing argument.";
		char arrayname[strlen(args[2])+4];
		strcpy(arrayname,args[2]);
		strcat(arrayname,"[+]");
		return Config.setByName(arrayname,0) < 0 ? "Failed." : 0;
	} else if (!strcmp("set",args[1])) {
		if (argc == 4) {
			if (0 > Config.setByName(args[2],args[3]))
				return "Failed.";
		} else if (argc == 5) {
			char tmp[strlen(args[3])+strlen(args[4])+2];
			strcpy(tmp,args[3]);
			strcat(tmp," ");
			strcat(tmp,args[4]);
			if (0 > Config.setByName(args[2],tmp))
				return "Failed.";
		} else
			return "Missing argument.";
		return 0;
	} else if (!strcmp(args[1],"write")) {
		return cfg_store_nodecfg() ? "Failed." : 0;
	} else if (!strcmp(args[1],"read")) {
		return cfg_read_nodecfg() ? "Failed." : 0;
	} else if (!strcmp(args[1],"size")) {
		term << Config.calcSize() << '\n';
		return 0;
	} else if (!strcmp(args[1],"defaults")) {
		cfg_init_defaults();
	} else if (!strcmp(args[1],"activate")) {
		cfg_activate();
	} else if (!strcmp(args[1],"backup")) {
		return nvm_copy_blob("node.cfg.bak","node.cfg") ? "Failed." : 0;
	} else if (!strcmp(args[1],"restore")) {
		return nvm_copy_blob("node.cfg","node.cfg.bak") ? "Failed." : 0;
	} else if (!strcmp(args[1],"clear")) {
		if (argc != 2)
			return Config.setByName(args[2],0) < 0 ? "Failed." : 0;
		cfg_clear_nodecfg();
	} else if (!strcmp(args[1],"erase")) {
		if (argc != 2)
			return "Invalid number of arguments.";
		nvm_erase_key("node.cfg");
	} else if (!strcmp("parsexxd",args[1])) {
		if (parse_xxd(term,rtcfgbuf))
			return "Invalid hex input";
		term.printf("parsing %u bytes\n",rtcfgbuf.size());
		NodeConfig nc;
		int e = nc.fromMemory(rtcfgbuf.data(),rtcfgbuf.size());
		if (0 >= e) {
			term.printf("parser error: %d\n",e);
			return "";
		}
		Config = nc;
	} else if (!strcmp("writebuf",args[1])) {
		return nvm_store_blob("hw.cfg",rtcfgbuf.data(),rtcfgbuf.size()) ? "Failed." : 0;
	} else if (!strcmp("clearbuf",args[1])) {
		rtcfgbuf.clear();
	} else if (!strcmp("xxdbuf",args[1])) {
		print_hex(term,rtcfgbuf.data(),rtcfgbuf.size());
	} else if (!strcmp(args[1],"xxd")) {
		return xxdSettings(term);
	} else if (!strcmp(args[1],"nvxxd")) {
		size_t s;
		uint8_t *buf = 0;
		if (int e = nvm_read_blob("node.cfg",&buf,&s))
			return e ? "Failed." : 0;
		print_hex(term,buf,s);
		free(buf);
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}


// requires configUSE_TRACE_FACILITY in FreeRTOSConfig.h
#if configUSE_TRACE_FACILITY == 1
const char taskstates[] = "XRBSDI";
const char *ps(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		return "Invalid number of arguments.";
	}
	unsigned nt = uxTaskGetNumberOfTasks();
	TaskStatus_t *st = (TaskStatus_t*) malloc(nt*sizeof(TaskStatus_t));
	if (st == 0)
		return "Out of memory.";
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


static const char *print_data(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		return "Invalid number of arguments.";
	}
	runtimedata_to_json(term);
	term.write("\n",1);
	return 0;
}


#ifdef CONFIG_OTA
//#ifdef close
//#undef close
//#endif
static const char *update(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		return help_cmd(term,args[0]);
	if (heap_caps_get_free_size(MALLOC_CAP_32BIT) < (12<<10))
		term.println("WARNING: memory low!");
	UBaseType_t p = uxTaskPriorityGet(0);
	vTaskPrioritySet(0, 11);
	const char *r = 0;
	if ((argc == 3) && (0 == strcmp(args[1],"-b"))) {
		r = perform_ota(term,(char*)args[2],true);
		if (r == 0) {
			term.println("rebooting...");
			term.disconnect();
			vTaskDelay(400);
			esp_restart();
		}
	} else if ((argc == 4) && (0 == strcmp(args[1],"-p"))) {
		r = update_part(term,(char*)args[3],args[2]);
#ifdef CONFIG_ROMFS
		if (r == 0)
			romfs_setup();
#endif
	} else if (argc == 2) {
		r = perform_ota(term,(char*)args[1],false);
	} else {
		return "Invalid argument #1.";
	}
	vTaskPrioritySet(0, p);
	return r;
}
#endif


static const char *getuptime(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		return "Invalid number of arguments.";
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


static const char *cpu(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		return "Invalid number of arguments.";
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
		term.printf("ESP%u (rev %d) with %d core%s @ %uMHz"
			, ci.model ? 32 : 8266
			, ci.revision
			, ci.cores
			, ci.cores > 1 ? "s" : ""
			, mhz
			);
		if (ci.features & CHIP_FEATURE_WIFI_BGN)
			term.print(", WiFi");
		if (ci.features & CHIP_FEATURE_BT)
			term.print(", BT");
		if (ci.features & CHIP_FEATURE_BLE) 
			term.print(", BT/LE");
		if (ci.features & CHIP_FEATURE_EMB_FLASH) 
			term.print(", flash");
		term << '\n';
		return 0;
	}
	if (0 == term.getPrivLevel())
		return "Access denied.";
	errno = 0;
	long l = strtol(args[1],0,0);
	if ((l == 0) || (errno != 0))
		return "Invalid argument #1.";
	if (0 != set_cpu_freq(l))
		return "Failed.";
	Config.set_cpu_freq(l);
	return 0;
}


static const char *password(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		return "Invalid number of arguments.";
	}
	if (argc == 1) {
		return help_cmd(term,args[0]);
	}
	if (!strcmp(args[1],"-c")) {
		Config.clear_pass_hash();
		term.println("password cleared");
	} else if (!strcmp(args[1],"-r")) {
		term.println("password empty");
		setPassword("");
	} else if (!strcmp(args[1],"-s")) {
		char buf[32];
		term.write(PW,sizeof(PW));
		int n = term.readInput(buf,sizeof(buf),false);
		if (n < 0)
			return "No input.";
		buf[n] = 0;
		setPassword(buf);
		term.setPrivLevel(0);
		term.println();
	} else if (!strcmp(args[1],"-v")) {
		char buf[32];
		term.write(PW,sizeof(PW));
		int n = term.readInput(buf,sizeof(buf),false);
		if (n < 0)
			return "No input.";
		buf[n] = 0;
		term.printf("%smatch\n", verifyPassword(buf) ? "" : "mis");
	} else {
		setPassword(args[1]);
	}
	return 0;
}


#ifdef CONFIG_SIGNAL_PROC
static const char *func(Terminal &term, int argc, const char *args[])
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
		return "Access denied.";
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
			return "Missing argument.";
		Function *f = Function::getInstance(args[2]);
		if (f == 0)
			return "Invalid argument #2.";
		f->operator() (0);
		return 0;
	}
	if (!strcmp(args[1],"-c")) {
		if (argc < 4) 
			return "Missing argument.";
		Function *f = FunctionFactory::create(args[3],args[2]);
		if (0 == f)
			return "Function not found.";
		FunctionConfig *c = Config.add_functions();
		c->set_name(args[2]);
		c->set_func(args[3]);
		for (int i = 4; i < argc; ++i) {
			c->add_params(args[i]);
			if (DataSignal *s = DataSignal::getSignal(args[i])) {
				if (f->setParam(i-4,s))
					term.printf("cannot set %d to %s\n",i,args[i]);
			} else {
				term.printf("unknown signal %s\n",args[i]);
			}
		}
		return 0;
	}
	return "Invalid argument #1.";
}


static const char *signal(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		return help_cmd(term,args[0]);
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
		return "Access denied.";
	if (!strcmp("-c",args[1])) {
		if ((argc != 5) && (argc != 4))
			return "Invalid number of arguments.";
		DataSignal *s = 0;
		if (!strcmp(args[2],"int")) {
			s = new IntSignal(strdup(args[3]));
		} else if (!strcmp(args[2],"float")) {
			s = new FloatSignal(strdup(args[3]));
		} else {
			return "Invalid argument #2.";
		}
		if (argc == 4)
			return 0;
		return s->initFrom(args[4]) ? "Failed." : 0;
	}
	if (!strcmp("-s",args[1])) {
		if (argc != 4)
			return "Missing argument.";
		DataSignal *s = DataSignal::getSignal(args[2]);
		if (0 == s)
			return "Invalid argument #2.";
		return s->initFrom(args[3]) ? "Failed." : 0;
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
			return "Invalid number of arguments.";
		DataSignal *s = DataSignal::getSignal(args[2]);
		if (s == 0)
			return "Invalid argument #2.";
		Function *f = Function::getInstance(args[3]);
		if (f == 0)
			return "Invalid argument #3.";
		s->addFunction(f);
		return 0;
	}
	return "Invalid argument #1.";
}
#endif


static const char *su(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		goto done;
	if (argc > 2)
		return help_cmd(term,args[0]);
	if (!strcmp(args[1],"0")) {
		term.setPrivLevel(0);
		goto done;
	}
	if (strcmp(args[1],"1"))
		return "Invalid argument #1.";
	if (!Config.pass_hash().empty()) {
		char buf[32];
		term.write(PW,sizeof(PW));
		int n = term.readInput(buf,sizeof(buf)-1,false);
		if (n < 0)
			return "input error";
		buf[n] = 0;
		if (!verifyPassword(buf))
			return "access denied";
	}
	term.setPrivLevel(1);
done:
	term.printf("privileg level %u\n",term.getPrivLevel());
	return 0;
}


#ifdef CONFIG_THRESHOLDS
static void print_thresholds(Terminal &t, EnvObject *o, int indent)
{
	unsigned c = 0;
	while (EnvElement *e = o->getChild(c++)) {
		const char *name = e->name();
		if (0 == strcmp(name,"mqtt"))
			continue;
		if (EnvObject *c = e->toObject()) {
			for (int i = 0; i < indent; ++i)
				t << "    ";
			t << name;
			t << ":\n";
			print_thresholds(t,c,indent+1);
		} else if (EnvNumber *n = e->toNumber()) {
			for (int i = 0; i < indent; ++i)
				t << "    ";
			t << name;
			t << ": ";
			float lo = n->getLow();
			if (isnan(lo)) {
				t << "<no thresholds>\n";
			} else {
				t << lo;
				t << ',';
				t << n->getHigh();
				t << '\n';
			}
		}
	}
}


static const char *thresholds(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		print_thresholds(term,RTData,0);
	} else if (argc == 2) {
		if (EnvElement *e = RTData->find(args[1])) {
			if (EnvNumber *n = e->toNumber()) {
				term << n->name() << ": " << n->getLow() << ", " << n->getHigh() << '\n';
				return 0;
			}
		}
		return "Invalid argument #1.";
	} else if (argc == 4) {
		char *x;
		float lo = strtof(args[2],&x);
		if (*x != 0)
			return "Invalid argument #2.";
		float hi = strtof(args[3],&x);
		if (*x != 0)
			return "Invalid argument #3.";
		if (hi <= lo + FLT_EPSILON)
			return "Invalid arguments.";
		if (EnvElement *e = RTData->find(args[1])) {
			if (EnvNumber *n = e->toNumber()) {
				n->setThresholds(lo,hi);
			} else {
				return "Invalid argument #1.";
			}
		} else {
			return "Invalid argument #1.";
		}
		bool updated = false;
		for (auto t : *Config.mutable_thresholds()) {
			if (t.name() == args[1]) {
				t.set_low(lo);
				t.set_high(hi);
				updated = true;
				break;
			}
		}
		if (!updated) {
			ThresholdConfig *t = Config.add_thresholds();
			t->set_name(args[1]);
			t->set_high(hi);
			t->set_low(lo);
		}
	} else {
		return "Invalid number of arguments.";
	}
	return 0;
}
#endif


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
	char ipstr[32],gwstr[32];
	ip4addr_ntoa_r((ip4_addr_t *)&i->ip.addr,ipstr,sizeof(ipstr));
	ip4addr_ntoa_r((ip4_addr_t *)&i->gw.addr,gwstr,sizeof(gwstr));
	t.printf("%s%s gw=%s %s\n",itf,ipstr,gwstr,up ? "up":"down");
}


static const char *ifconfig(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		return "Invalid number of arguments.";
	}
	tcpip_adapter_ip_info_t ipconfig;
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipconfig))
		print_ipinfo(term, "if0/sta ip=", &ipconfig, wifi_station_isup());
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ipconfig))
		print_ipinfo(term, "if1/sap ip=", &ipconfig, wifi_softap_isup());
#if defined TCPIP_ADAPTER_IF_ETH
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_ETH, &ipconfig))
		print_ipinfo(term, "if2/eth ip=", &ipconfig, eth_isup());
#endif
#if defined CONFIG_LWIP_IPV6 || defined CONFIG_IDF_TARGET_ESP32
	if (!ip6_addr_isany_val(IP6G))
		term.printf("if0/sta %s (global)\n", inet6_ntoa(IP6G));
	if (!ip6_addr_isany_val(IP6LL))
		term.printf("if0/sta %s (link-local)\n", inet6_ntoa(IP6LL), wifi_station_isup());
#endif
	return 0;
}


static const char *version(Terminal &term, int argc, const char *args[])
{
	if (argc != 1)
		return "Invalid number of arguments.";
	term.printf("Atrium Version %s\n"
		"build on " __DATE__ ", " __TIME__ "\n"
		"IDF version: %s\n"
		, Version
		, esp_get_idf_version()
		);
	return 0;
}


extern const char *adc(Terminal &term, int argc, const char *args[]);
extern const char *at(Terminal &term, int argc, const char *args[]);
extern const char *bme(Terminal &term, int argc, const char *args[]);
extern const char *dht(Terminal &term, int argc, const char *args[]);
extern const char *dim(Terminal &term, int argc, const char *args[]);
extern const char *distance(Terminal &term, int argc, const char *args[]);
extern const char *dmesg(Terminal &term, int argc, const char *args[]);
extern const char *gpio(Terminal &term, int argc, const char *args[]);
extern const char *hall(Terminal &term, int argc, const char *args[]);
extern const char *holiday(Terminal &term, int argc, const char *args[]);
extern const char *i2c(Terminal &term, int argc, const char *args[]);
extern const char *inetadm(Terminal &term, int argc, const char *args[]);
extern const char *influx(Terminal &term, int argc, const char *args[]);
extern const char *led_set(Terminal &term, int argc, const char *args[]);
extern const char *lightctrl(Terminal &term, int argc, const char *args[]);
extern const char *mqtt(Terminal &term, int argc, const char *args[]);
extern const char *nightsky(Terminal &term, int argc, const char *args[]);
extern const char *prof(Terminal &term, int argc, const char *args[]);
extern const char *process(Terminal &term, int argc, const char *args[]);
extern const char *readelf(Terminal &term, int argc, const char *args[]);
extern const char *relay(Terminal &term, int argc, const char *args[]);
extern const char *shell_format(Terminal &term, int argc, const char *args[]);
extern const char *sm_cmd(Terminal &term, int argc, const char *args[]);
extern const char *sntp(Terminal &term, int argc, const char *args[]);
extern const char *subtasks(Terminal &term, int argc, const char *args[]);
extern const char *touchpad(Terminal &term, int argc, const char *args[]);
extern const char *uart_termcon(Terminal &term, int argc, const char *args[]);
extern const char *udns(Terminal &term, int argc, const char *args[]);
extern const char *udpc_stats(Terminal &term, int argc, const char *args[]);
extern const char *udp_stats(Terminal &term, int argc, const char *args[]);
extern const char *onewire(Terminal &term, int argc, const char *args[]);
extern const char *xluac(Terminal &t, int argc, const char *args[]);

static const char *help(Terminal &term, int argc, const char *args[]);


ExeName ExeNames[] = {
	{"?",0,help,"help",0},
	{"action",0,action,"actions",action_man},
	{"adc",0,adc,"A/D converter",adc_man},
	{"ap",1,accesspoint,"accesspoint settings",ap_man},
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
	{"config",1,config,"system configuration",config_man},
	{"cpu",1,cpu,"CPU speed",0},
	{"debug",0,debug,"enable debug logging (* for all)",debug_man},
#if defined CONFIG_SPIFFS || defined CONFIG_FATFS
	{"df",0,shell_df,"available storage",0},
#endif
#ifdef CONFIG_DHT
	{"dht",0,dht,"DHTxx family of sensors",dht_man},
#endif
#ifdef CONFIG_DIMMER
	{"dim",0,dim,"operate dimmer",dim_man},
#endif
#ifdef CONFIG_DIST
	{"dist",0,distance,"hc_sr04 distance measurement",0},
#endif
#if CONFIG_SYSLOG
	{"dmesg",0,dmesg,"system log",dmesg_man},
#endif
#if defined HAVE_FS && defined CONFIG_OTA
	{"download",1,download,"http file download",0},
#endif
	{"env",0,env,"print variables",0},
	{"event",0,event,"handle trigger events as actions",event_man},
#if defined CONFIG_SPIFFS || defined CONFIG_FATFS
	{"format",1,shell_format,"format storage",0},
#endif
#ifdef CONFIG_SIGNAL_PROC
	{"func",1,func,"function related operations",func_man},
#endif
	//{"flashtest",flashtest,0},
	{"gpio",1,gpio,"GPIO access",gpio_man},
#ifdef CONFIG_IDF_TARGET_ESP32
	{"hall",0,hall,"hall sensor",0},
#endif
	{"help",0,help,"help",0},
#ifdef CONFIG_HOLIDAYS
	{"holiday",0,holiday,"holiday settings",holiday_man},
#endif
	{"hwconf",1,hwconf,"hardware configuration",hwconf_man},
#ifdef CONFIG_I2C
	{"i2c",0,i2c,"list I2C devices",0},
#endif
	{"inetadm",1,inetadm,"manage network services",inetadm_man},
#ifdef CONFIG_INFLUX
	{"influx",1,influx,"configure influx UDP data gathering",influx_man},
#endif
	{"ip",0,ifconfig,"network IP and interface settings",ip_man},
#ifdef CONFIG_LEDS
	{"led",0,led_set,"status LED",status_man},
#endif
#ifdef CONFIG_LIGHTCTRL
	{"lightctrl",0,lightctrl,"light control APP settings",0},
#endif
	{"ls",0,shell_ls,"list storage",0},
#ifdef CONFIG_LUA
	{"lua",0,xluac,"run a lua script",lua_man},
	{"luac",0,xluac,"compile lua script",luac_man},
#endif
	{"mac",0,mac,"MAC addresses",mac_man},
	{"mem",0,mem,"RAM statistics",mem_man},
#ifdef CONFIG_FATFS
	{"mkdir",1,shell_mkdir,"make directory",0},
#endif
#ifdef CONFIG_MQTT
	{"mqtt",1,mqtt,"MQTT settings",mqtt_man},
#endif
#ifdef CONFIG_FATFS
	{"mv",1,shell_mv,"move/rename file",0},
#endif
	{"nodename",0,hostname,"get or set hostname",0},
#ifdef CONFIG_NIGHTSKY
	{"ns",0,nightsky,"nightsky operation",0},
#endif
	{"nslookup",0,nslookup,"lookup hostname in DNS",0},
#ifdef CONFIG_ONEWIRE
	{"ow",0,onewire,"one-wire driver access",ow_man},
#endif
	{"part",0,part,"partition table",part_man},
	{"passwd",3,password,"set password",passwd_man},
#ifdef CONFIG_FUNCTION_TIMING
	{"prof",0,prof,"profiling information",0},
#endif
#if configUSE_TRACE_FACILITY == 1
	{"ps",0,ps,"task statistics",0},
#endif
#ifdef CONFIG_ELF_LOADER
	{"readelf",0,readelf,"read ELF information from file",0},
#endif
	{"reboot",1,shell_reboot,"reboot system",0},
#ifdef CONFIG_RELAY
	{"relay",0,relay,"relay status/operation",relay_man},
#endif
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
#if 0
	{"set",1,set,"variable settings",set_man},
#endif
#ifdef CONFIG_SIGNAL_PROC
	{"signal",0,signal,"view/modify/connect signals",signal_man},
#endif
#ifdef CONFIG_STATEMACHINES
	{"sm",0,sm_cmd,"state-machine states and config",sm_man},
#endif
	{"sntp",0,sntp,"simple NTP client settings",sntp_man},
	{"station",1,station,"WiFi station settings",station_man},
#ifdef CONFIG_THRESHOLDS
	{"stt",0,thresholds,"view/set schmitt-trigger thresholds",stt_man},
#endif
	{"su",2,su,"set user privilege level",su_man},
	{"subtasks",0,subtasks,"statistics of cyclic subtasks",0},
#ifdef CONFIG_TERMSERV
	{"term",2,uart_termcon,"open terminal on UART",console_man},
#endif
	{"timer",1,timefuse,"create timer",timer_man},
	{"timezone",1,timezone,"set time zone for NTP (offset to GMT or 'CET')",0},
#ifdef HAVE_FS
	{"touch",1,shell_touch,"create file, update time-stamp",0},
#endif
#ifdef CONFIG_TOUCHPAD
	{"tp",1,touchpad,"touchpad output",0},
#endif
#ifdef CONFIG_UDNS
	{"udns",0,udns,"uDNS status",0},
#endif
#ifdef CONFIG_UDPCTRL
	{"udpcstat",0,udpc_stats,"display UDP statistics",0},
#endif
#ifdef CONFIG_OTA
	{"update",1,update,"OTA download procedure",update_man},
#endif
	{"uptime",0,getuptime,"time system is up and running",0},
	{"version",0,version,"version information",0},
	{"webdata",0,print_data,"data provided to webpages",0},
	{"wifi",1,wifi,"wifi station/AP mode",wifi_man},
#ifdef CONFIG_WPS
	{"wps",1,wps,"wifi configuration with WPS",0},
#endif
#ifdef HAVE_FS
	{"xxd",0,shell_xxd,"hex output of file",0},
#endif
};


const char *help_cmd(Terminal &term, const char *arg)
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
	return "help page not found";
}


static const char *help(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		term.println("help <cmd>: print help for command <cmd>");
		for (int i = 1; i < sizeof(ExeNames)/sizeof(ExeNames[0]); ++i) 
			term.printf("%-14s %-5s  %s\n",ExeNames[i].name,ExeNames[i].flags&1?"admin":"user",ExeNames[i].descr);
	} else {
		return help_cmd(term,args[1]);
	}
	return 0;
}


const char *xlua_exe(Terminal &t, const char *);


const char *shellexe(Terminal &term, char *cmd)
{
	PROFILE_FUNCTION();
	log_info(TAG,"execute '%s'",cmd);
	char *args[8];
	while ((*cmd == ' ') || (*cmd == '\t'))
		++cmd;
#ifdef CONFIG_LUA
	if ((0 == memcmp("lua",cmd,3)) && ((cmd[3] == ' ') || (cmd[3] == '\t')))
		return xlua_exe(term,cmd+4);
#endif
	char *at = cmd;
	size_t n = 0;
	do {
		if (n == sizeof(args)/sizeof(args[0])) {
			return "too many arguments";
		}
		args[n] = at;
		at = strchr(at,' ');
		while (at && (*at == ' ')) {
			*at = 0;
			++at;
		}
		++n;
	} while (at && *at);
	for (int i = 0; i < sizeof(ExeNames)/sizeof(ExeNames[0]); ++i) {
		if (0 == strcmp(args[0],ExeNames[i].name)) {
			if ((ExeNames[i].flags & EXEFLAG_INTERACTIVE) && !term.isInteractive()) {
				term.println("Cannot execute interactive command.");
			}
			if (!Config.pass_hash().empty() && ((ExeNames[i].flags & 1) > term.getPrivLevel())) {
				term.println("Access denied.");
			}
			if ((n == 2) && (0 == strcmp("-h",args[1])))
				return help_cmd(term,args[0]);
			//term.printf("calling shell function '%s'\n",ExeNames[i].name);
			return ExeNames[i].function(term,n,(const char **)args);
		}
	}
	return "Command not found.";
}


int exe_flags(char *cmd)
{
	while ((*cmd == ' ') || (*cmd == '\t'))
		++cmd;
	char *s = strchr(cmd,' ');
	if (s)
		*s = 0;
	char *t = strchr(cmd,'\t');
	if (t)
		*t = 0;
	int r = -1;
	for (int i = 0; i < sizeof(ExeNames)/sizeof(ExeNames[0]); ++i) {
		if (0 == strcmp(cmd,ExeNames[i].name)) {
			r = ExeNames[i].flags;
			break;
		}
	}
	if (t)
		*t = '\t';
	if (s)
		*s = ' ';
	return r;
}


void shell(Terminal &term, bool prompt)
{
	char com[128];
	if (PWD.empty())
		PWD = "/flash/";
	if (prompt) {
		term.write("> ",2);
		term.sync();
	}
	int r = term.readInput(com,sizeof(com)-1,true);
	while (r >= 0) {
		term.println();
		com[r] = 0;
		if ((r == 4) && !memcmp(com,"exit",4))
			break;
		if (const char *msg = shellexe(term,com)) {
			term.println(msg);
		} else {
			term.println("OK.");
		}
		if (prompt) {
			term.write("> ",2);
			term.sync();
		}
		r = term.readInput(com,sizeof(com)-1,true);
	}
}
