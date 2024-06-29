/*
 *  Copyright (C) 2018-2024, Thomas Maier-Komor
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
#include "display.h"
#include "event.h"
#include "fonts.h"
#include "fs.h"
#include "globals.h"
#include "hwcfg.h"
#include "leds.h"
#include "log.h"
#include "nvm.h"
#include "ota.h"
#include "memfiles.h"
#include "mqtt.h"
#include "netsvc.h"
#include "profiling.h"
#include "romfs.h"
#include "settings.h"
#include "shell.h"
#include "usntp.h"
#include "support.h"
#include "swcfg.h"
#include "syslog.h"
#include "terminal.h"
#include "timefuse.h"
#include "env.h"
#include "wifi.h"

#include <float.h>
#include <sstream>

#ifdef ESP32
#include <esp_core_dump.h>
#endif

#include <esp_image_format.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#if IDF_VERSION >= 50
#include <spi_flash_mmap.h>
#include <esp_chip_info.h>
#include <rom/ets_sys.h>
#endif

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
#if defined CONFIG_FATFS || defined CONFIG_SPIFFS
#define HAVE_FS
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/FreeRTOSConfig.h>
#include <freertos/task.h>

#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
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

#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
#if IDF_VERSION >= 40
extern "C" {
//#include <esp32/clk.h>
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

static const char PW[] = "password:", NotSet[] = "<not set>";

static void action_print(void *p, const Action *a)
{
	if (a->text && a->name) {
		Terminal *t = (Terminal*)p;
		t->printf("\t%-24s  %s\n",a->name,a->text);
	}
}


static void action_perf(void *p, const Action *a)
{
	Terminal *t = (Terminal*)p;
	if (a->num)
		t->printf("\t%-20s %5u %9u (%6u/%6u/%6u)\n",a->name,a->num,a->sum,a->min,a->sum/a->num,a->max);
}


static const char *action(Terminal &t, int argc, const char *args[])
{
	if (argc == 1)
		return help_cmd(t,args[0]);
	if (argc == 3) {
		if (action_activate_arg(args[1],(void*)args[2]))
			return "Invalid argument #1.";
	} else if (argc != 2) {
		return "Invalid number of arguments.";
	} else if (args[1][0] != '-') {
		if (action_activate(args[1]))
			return "Invalid argument #1.";
	} else if (args[1][2]) {
		return "Invalid argument #1.";
	} else if (args[1][1] == 'l') {
		action_iterate(action_print,(void*)&t);
	} else if (0 == strcmp(args[1],"-p")) {
		t.printf("\t%-20s %5s %9s (%6s/%6s/%6s)\n","name","count","total","min.","avg.","max.");
		action_iterate(action_perf,(void*)&t);
	} else if (args[1][1] == 'F') {
		if (0 == t.getPrivLevel())
			return "Access denied.";
		Config.set_actions_enable(Config.actions_enable()|2);
	} else if (args[1][1] == 'f') {
		if (0 == t.getPrivLevel())
			return "Access denied.";
		Config.set_actions_enable(Config.actions_enable()&~2);
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


static void print_ele(Terminal &t, EnvElement *e)
{
	t << ' ';
	e->writeValue(t);
	if (const char *d = e->getDimension()) {
		t << ' ';
		t.print(d);
	}
	t.println();
}


static void print_obj(Terminal &t, EnvObject *o, int indent)
{
	unsigned c = 0;
	while (EnvElement *e = o->getChild(c++)) {
		const char *n = e->name();
		for (int i = 0; i < indent; ++i)
			t << "    ";
		t << n;
		t << ':';
		if (EnvObject *c = e->toObject()) {
			t << '\n';
			print_obj(t,c,indent+1);
		} else {
			print_ele(t,e);
		}
	}
}


static const char *env(Terminal &t, int argc, const char *args[])
{
	const char *r = 0;
	rtd_lock();
	if (argc == 1) {
		print_obj(t,RTData,0); 
	} else if (argc == 2) {
		EnvElement *c = RTData->getChild(args[1]);
		if (c == 0)
			r = "Invalid argument #1.";
		else if (EnvObject *o = c->toObject())
			print_obj(t,o,0); 
		else
			print_ele(t,c);
	} else {
		r = "Invalid number of arguments.";
	}
	rtd_unlock();
	return r;
}


const char *event(Terminal &t, int argc, const char *args[])
{
	if (argc == 1) {
		return help_cmd(t,args[0]);
	} else if ((args[1][0] != '-') || (args[1][2])) {
		return "Invalid argument #1.";
	}
	char com = args[1][1];
	const char *err = "Invalid argument #1.";
	if (argc == 2) {
		if (com == 'l') {
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
			err = 0;
		} else if (com == 's') {
			event_status(t);
			err = 0;
		}
	} else if (argc == 3) {
		if (com == 't') {
			if (event_t e = event_id(args[2])) {
				event_trigger(e);
				err = 0;
			} else {
				err = "Invalid argument #2.";
			}
		}
	} else if (argc == 4) {
		if (com == 't') {
			if (event_t e = event_id(args[2])) {
				event_trigger_arg(e,strdup(args[3]));
				return 0;
			}
			return "Invalid argument #2.";
		}
		if (0 == t.getPrivLevel())
			return "Access denied.";
		Action *a = action_get(args[3]);
		event_t e = event_id(args[2]);
		if (a == 0) {
			err = "Invalid argument #3.";
		} else if (e == 0) {
			err = "Invalid argument #2.";
		} else if (com == 'a') {
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
			err = 0;
		} else if (com == 'd') {
			event_detach(e,a);
			err = "Failed.";
			auto &t = *Config.mutable_triggers();
			for (auto i = t.begin(), e = t.end(); i != e; ++i) {
				if (i->event() == args[2]) {
					auto &ma = *i->mutable_action();
					auto j = ma.begin();
					while (j != ma.end()) {
						if (*j == args[3]) {
							j = ma.erase(j);
							err = 0;
						} else {
							++j;
						}
					}
				}
			}
		} 
	} else if (argc == 5) {
		if (0 == t.getPrivLevel())
			return "Access denied.";
		Action *a = action_get(args[3]);
		event_t e = event_id(args[2]);
		if (a == 0) {
			err = "Invalid argument #3.";
		} else if (e == 0) {
			err = "Invalid argument #2.";
		} else if (com == 'a') {
			size_t cl = strlen(args[3]);
			char *arg = (char*) malloc(cl+strlen(args[4])+2);
			memcpy(arg,args[3],cl);
			arg[cl] = ' ';
			strcpy(arg+cl+1,args[4]);
			event_callback_arg(e,a,args[4]);
			for (auto &t : *Config.mutable_triggers()) {
				if (t.event() == args[2]) {
					t.add_action(arg);
					return 0;
				}
			}
			Trigger *t = Config.add_triggers();
			t->set_event(args[2]);
			t->add_action(arg);
			err = 0;
		} 
	} else {
		err = "Invalid number of arguments.";
	}
	return err;
}


#if defined CONFIG_FATFS || defined CONFIG_ROMFS_VFS
static const char *shell_cd(Terminal &term, int argc, const char *args[])
{
	if (argc > 2)
		return "Invalid number of arguments.";
	if (argc == 1) {
		term.println(term.getPwd().c_str());
		return 0;
	}
	return term.setPwd(args[1]) ? "Invalid argument #1." : 0;
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
		estring fn = term.getPwd();
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
		dn = term.getPwd();
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
		dn = term.getPwd();
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
		fn0 = term.getPwd();
	fn1 += args[1];
	if (args[2][0] != '/')
		fn1 = term.getPwd();
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


#ifdef HAVE_FS
static const char *ls_print_dir(Terminal &term, const char *dir)
{
	DIR *d = opendir(dir);
	if (d == 0) {
		term.printf("unable to open dir %s: %s\n",dir,strerror(errno));
		return "";
	}
	size_t dl = strlen(dir);
	char fn[128];
	memcpy(fn,dir,dl);
	if (fn[dl-1] != '/') {
		fn[dl] = '/';
		++dl;
	}
	while (struct dirent *e = readdir(d)) {
		size_t el = strlen(e->d_name);
		if (dl+el+1 > sizeof(fn)) {
			term.printf("%s\t[name too long]\n",e->d_name);
			continue;
		}
		memcpy(fn+dl,e->d_name,el+1);
		struct stat st;
		if (stat(fn,&st) != 0)
			term.printf("%-15s [%s]\n",e->d_name,strerror(errno));
		else if ((st.st_mode & S_IFDIR) == S_IFDIR)
			term.printf("%-15s  <DIR>\n",e->d_name);
		else
			term.printf("%-15s %6u\n",e->d_name,st.st_size);
	}
	closedir(d);
	return 0;
}
#endif


static const char *shell_ls(Terminal &term, int argc, const char *args[])
{
#if defined CONFIG_ROMFS && !defined CONFIG_ROMFS_VFS
	if (unsigned n = romfs_num_entries()) {
		for (int i = 0; i < n; ++i) {
			const char *n = romfs_name(i);
			assert(n);
			size_t s,o;
			romfs_getentry(n,&s,&o);
			term.printf("\t%5u %-12s\n",s,n);
		}
	}
	return 0;
#endif
#ifdef HAVE_FS
	if (argc == 1)
		return ls_print_dir(term,term.getPwd().c_str());
	int a = 1;
	char dir[128];
	const char *r = 0;
	const char *pwd = term.getPwd().c_str();
	while (a < argc) {
		if ('/' == args[a][0])
			dir[0] = 0;
		else
			strcpy(dir,pwd);
		strcat(dir,args[a]);
		if (ls_print_dir(term,dir))
			r = "Had errors.";
		++a;
	}
	return r;
#else
	return "no filesystem";
#endif
}


static const char *shell_cat1(Terminal &term, const char *arg)
{
#if defined CONFIG_ROMFS && !defined CONFIG_ROMFS_VFS
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
		size_t p = term.getPwd().size();
		size_t a = strlen(arg);
		filename = (char *) malloc(a+p+1);
		if (filename == 0)
			return "Out of memory.";
		memcpy(filename,term.getPwd().data(),p);
		memcpy(filename+p,arg,a+1);
	}
	int fd = open(filename,O_RDONLY);
	if (filename != arg)
		free(filename);
	if (fd < 0)
		return strerror(errno);
	char buf[512];
	int t = 0;
	int n;
	do {
		n = read(fd,buf,sizeof(buf));	// for USB-host-FS does not know pread
		if (n == -1) {
			int e = errno;
			close(fd);
			return strerror(e);
		} else if (n > 0) {
			t += n;
			term.print(buf,n);
		}
	} while (n != 0);
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
	for (int i = 1; i < argc; ++i) {
		if (const char *r = shell_cat1(term,args[i]))
			return r;
	}
	return 0;
}


#ifdef HAVE_FS
static const char *shell_cp(Terminal &term, int argc, const char *args[])
{
	if (argc != 3)
		return "Invalid number of arguments.";
	const char *bn = strrchr(args[1],'/');
	if (bn)
		++bn;
	else
		bn = args[1];
	estring from;
	if (args[1][0] != '/')
		from = term.getPwd();
	from += args[1];
	int f = open(from.c_str(),O_RDONLY);
	if (f == -1) {
		term.printf("open %s: %s\n",from.c_str(),strerror(errno));
		return "";
	}
	const estring &pwd = term.getPwd();
	size_t ps = pwd.size();
	size_t l2 = strlen(args[2]);
	char *to = (char *) alloca(l2+strlen(bn)+ps+2);
	char *at = to;
	if ('/' != args[2][0]) {
		memcpy(to,pwd.data(),ps);
		at += ps;
	}
	memcpy(at,args[2],l2+1);
	struct stat st;
	if ((0 == stat(to,&st)) && (S_IFDIR == (st.st_mode & S_IFMT))) {
		strcat(at,"/");
		strcat(at,bn);
	}
	int t = open(to,O_CREAT|O_WRONLY|O_TRUNC,0666);
	if (t == -1) {
		close(f);
		term.printf("create %s: %s\n",to,strerror(errno));
		return "";
	}
	char buf[512];
	int n = read(f,buf,sizeof(buf));
	while (n > 0) {
		int w = write(t,buf,n);
		if (w < 0) {
			close(t);
			close(f);
			term.printf("writing %s: %s\n",args[2],strerror(errno));
			return "";
		}
		n = read(f,buf,sizeof(buf));
	}
	int e = errno;
	close(t);
	close(f);
	if (n < 0) {
		term.printf("reading %s: %s\n",from.c_str(),strerror(e));
		return "";
	}
	return 0;
}
#endif


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
		size_t l = strlen(args[1]);
		char path[term.getPwd().size()+l+1];
		strcpy(path,term.getPwd().c_str());
		strcat(path,args[1]);
		fd = creat(path,0666);
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
	const char *p = args[1];
	if (p[0] != '/') {
		size_t al = strlen(args[1]);
		const estring &pwd = term.getPwd();
		size_t pl = pwd.size();
		char *path = (char *)alloca(al+pl+1);
		memcpy(path,pwd.data(),pl);
		memcpy(path+pl,args[1],al+1);
		p = path;
	}
	int fd = open(p,O_RDONLY);
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
		term.print_hex(buf,n,off);
		n = read(fd,buf,sizeof(buf));
		off += n;
	}
	close(fd);
	return 0;
}
#endif


static const char *shell_reboot(Terminal &term, int argc, const char *args[])
{
#if CONFIG_MQTT
	mqtt_stop();
#endif
#if CONFIG_SYSLOG
	syslog_stop();
#endif
	term.println("rebooting...");
	term.sync();
	term.disconnect();
	vTaskDelay(400);
#if IDF_VERSION < 50
#ifndef CONFIG_IDF_TARGET_ESP8266
	esp_unregister_shutdown_handler((shutdown_handler_t)esp_wifi_stop);
#endif
#endif
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
	term.printf("SPIFFS error: %s\n",esp_err_to_name(ret));
#elif defined CONFIG_FATFS
	DWORD nc = 0;
	FATFS *fs = 0;
	int err = f_getfree(term.getPwd().c_str(),&nc,&fs);
	if ((err == 0) && (fs != 0)) {
		term.printf("%lu kiB free\n",nc*fs->csize*fs->ssize>>10);
		return 0;
	}
	term.printf("error: %d\n",err);
#endif
	return "No filesystem.";
}
#endif


static void print_part(Terminal &t, const esp_partition_t *p, bool nl)
{
	t.printf("%s %02x %7d@%08x %-8s%c",p->type ? "data" : "app ", p->subtype, p->size, p->address, p->label, nl?'\n':' ');
}


static const char *part(Terminal &term, int argc, const char *args[])
{
	if (argc == 3) {
		if (strcmp(args[1],"erase"))
			return "Invalid argument #1.";
		if (esp_partition_iterator_t i = esp_partition_find(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,args[2])) {
			if (const esp_partition_t *p = esp_partition_get(i)) {
#if IDF_VERSION >= 50
				if (esp_err_t e = esp_partition_erase_range(p,0,p->size)) {
					return esp_err_to_name(e);
				}
#else
				if (esp_err_t e = spi_flash_erase_range(p->address,p->size)) {
					return esp_err_to_name(e);
				}
#endif
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
#ifdef ESP32
		print_part(term,p,false);
		esp_app_desc_t desc;
		if (0 == esp_ota_get_partition_description(p,&desc))
			term.printf("[%s, %s, %s]\n",desc.project_name,desc.version,desc.idf_ver);
		else
			term.println("[<no information>]");
#else
		print_part(term,p,true);
#endif
		i = esp_partition_next(i);
	}
	i = esp_partition_find(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,0);
	while (i) {
		const esp_partition_t *p = esp_partition_get(i);
		print_part(term,p,true);
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
#ifdef ESP32
	term.printf("exec mem    : %u\n",heap_caps_get_free_size(MALLOC_CAP_EXEC));
	term.printf("SPI  mem    : %u\n",heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
	term.printf("internal mem: %u\n",heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif
#ifdef CONFIG_VERIFY_HEAP
	if (argc > 1) {
		if (0 == strcmp(args[1],"-d"))
			heap_caps_dump_all();
		if (0 == strcmp(args[1],"-c"))
			return false == heap_caps_check_integrity_all(true) ? "Failed." : 0;
	}
#endif
	return 0;
}


const char *mac(Terminal &term, int argc, const char *args[])
{
	uint8_t mac[6];
	if (argc == 1) {
		return help_cmd(term,args[0]);
	} else if ((args[1][0] != '-') || args[1][2]) {
		return "Invalid argument #1.";
	} else if (argc == 2) {
		if (args[1][1] == 'l') {
#if IDF_VERSION >= 50

			esp_netif_t *nif = esp_netif_next(0);
			while (nif) {
				char name[8];
				name[0] = 0;
				if (esp_netif_get_mac(nif,mac)) {
				} else if (esp_netif_get_netif_impl_name(nif,name)) {
				} else {
					print_mac(term,name,mac);
				}
				nif = esp_netif_next(nif);
			}
#else
			if (ESP_OK == esp_wifi_get_mac(WIFI_IF_AP,mac))
				print_mac(term,"softap",mac);
			if (ESP_OK == esp_wifi_get_mac(WIFI_IF_STA,mac))
				print_mac(term,"station",mac);
			return 0;
#endif
		}
	}
	if (0 == term.getPrivLevel())
		return "Access denied.";
	// priveleged commands
	if (argc == 2) {
		if (args[1][1] == 'c') {
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
		if (args[1][1] == 's') {
			w = WIFI_IF_AP;
			Config.mutable_station()->set_mac(mac,6);
		} else if (args[1][1] == 'a') {
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
	// accepted format:
	// <offset>: <byte> <byte> <byte>
	uint32_t v = 0;
	bool nl = false, valid = false;
	char c;
	while (term.get_ch(&c) == 1) {
		if (c == '\r') {
			if (nl)
				return 0;
			nl = true;
		} else {
			nl = false;
		}
		if ((c == ' ') || (c == '\t') || (c == '\n') || (c == '\r')) {
			if (valid) {
				buf.push_back(v);
				v = 0;
				valid = false;
			}
		} else if (c == ':') {
			if (v != buf.size()) {
				//buf.resize(v);
				// resize/holes intentionally not supported
				return "Invalid input.";
			}
			v = 0;
			valid = false;
		} else {
			v <<= 4;
			if ((c >= '0') && (c <= '9')) {
				v |= (c-'0');
			} else if ((c >= 'a') && (c <= 'f')) {
				v |= (c-'a')+10;
			} else if ((c >= 'A') && (c <= 'F')) {
				v |= (c-'A')+10;
			} else {
				return "Invalid input.";
			}
			valid = true;
		}
	}
	return "Terminal error.";
}


static const char *hwconf(Terminal &term, int argc, const char *args[])
{
	static vector<uint8_t> hwcfgbuf;
	if (argc == 1) {
		term.printf(
				"Available commands:\n"
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
		return HWConf.setByName(arrayname,3 == argc ? 0 : args[3]) < 0 ? "Failed." : 0;
	} else if (!strcmp("set",args[1])) {
		if (argc < 4)
			return "Missing argument.";
		int r = HWConf.setByName(args[2],args[3]);
		if (r < 0) {
			term.printf("error %d\n",r);
			return "";
		}
		return 0;
	} else if (!strcmp("reset",args[1])) {
		return cfg_read_hwcfg() >= 0 ? "Failed." : 0;
	} else if (!strcmp("clear",args[1])) {
		if (argc == 3)
			return HWConf.setByName(args[2],0) < 0 ? "Failed." : 0;
		if (argc != 2)
			return "Invalid number of arguments.";
		HWConf.clear();
		HWConf.set_magic(0xAE54EDCB);
	} else if (!strcmp("xxd",args[1])) {
		size_t s = HWConf.calcSize();
		uint8_t *buf = (uint8_t *) malloc(s);
		HWConf.toMemory(buf,s);
		term.print_hex(buf,s);
		free(buf);
	} else if ((!strcmp("show",args[1])) || !strcmp("print",args[1])) {
		if (argc == 2)
			HWConf.toASCII(term);
#ifdef HAVE_GET_MEMBER
		else if (Message *m = HWConf.getMember(args[2]))
			m->toASCII(term);
		else
			return "Invalid argument #2.";
#else
		else
			return "Invalid number of arguments.";
#endif
		term.println();
	} else if (!strcmp("json",args[1])) {
		if (argc == 2)
			HWConf.toJSON(term);
#ifdef HAVE_GET_MEMBER
		else if (Message *m = HWConf.getMember(args[2]))
			m->toJSON(term);
		else
			return "Invalid argument #2.";
#else
		else
			return "Invalid number of arguments.";
#endif
	} else if (!strcmp("write",args[1])) {
		return cfg_store_hwcfg() ? "Failed." : 0;
#else
	} else if (!strcmp("clear",args[1])) {
		if (argc != 2)
			return "Invalid number of arguments.";
		HWConf.clear();
		HWConf.set_magic(0xAE54EDCB);
#endif
	} else if (!strcmp("nvxxd",args[1])) {
		size_t s = 0;
		uint8_t *buf = 0;
		if (int e = nvm_read_blob("hw.cfg",&buf,&s)) {
			return esp_err_to_name(e);
		}
		term.print_hex(buf,s);
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
		term.print_hex(hwcfgbuf.data(),hwcfgbuf.size());
	} else if (!strcmp(args[1],"read")) {
		return cfg_read_hwcfg() ? "Failed." : 0;
	} else if (!strcmp(args[1],"backup")) {
		return nvm_copy_blob("hwcfg.bak","hw.cfg");
	} else if (!strcmp(args[1],"restore")) {
		return nvm_copy_blob("hw.cfg","hwcfg.bak");
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}



static bool parse_bool(int argc, const char *args[], int a, bool d)
{
	bool r;
	if ((a >= argc) || arg_bool(args[a],&r))
		return d;
	return r;
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
	char *b = buf;
	if (h)
		b += sprintf(b,"%u:%02u:%02u",h,m,s);
	else if (m)
		b += sprintf(b,"%u:%02u",m,s);
	else
		b += sprintf(b,"%u",s);
	b += sprintf(b,".%03lu",ms);
}


static const char *timefuse(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		return "Invalid number of arguments.";
	if ((args[1][0] != '-') || (args[1][2] != 0))
		return "Invalid argument #1.";
	char optchr = args[1][1];
	if (argc == 2) {
		if ('l' == optchr) {
			timefuse_t t = timefuse_iterator();
			// active timers cannot be queried for repeat!
#ifdef ESP32
			term.printf(" id %-10s interval state   repeat\n","name");
#else
			term.printf(" id %-10s interval state\n","name");
#endif
			while (t) {
				char buf[20];
				int on = timefuse_active(t);
				ms_to_timestr(buf,timefuse_interval_get(t));
#ifdef ESP32
				bool rep = timefuse_repeat_get(t);
				term.printf("%3u %-10s %-8s %-7s %s\n"
						,t
						,timefuse_name(t)
						,buf
						,on==0?"dormant":on==1?"active":"unknown"
						,rep?"true":"false"
						);
#else
				term.printf("%3u %-10s %-8s %-7s\n"
						,t
						,timefuse_name(t)
						,buf
						,on==0?"dormant":on==1?"active":"unknown"
						);
#endif
				t = timefuse_next(t);
			}
			return 0;
		}
		return "Invalid argument #1.";
	}
	if (0 == term.getPrivLevel())
		return "Access denied.";
	if (argc == 3) {
		const char *r = 0;
		if ('s' == optchr) {
			if (timefuse_start(args[2]))
				r = "Failed.";
		} else if ('t' == optchr) {
			if (timefuse_stop(args[2]))
				r = "Failed.";
		} else if ('d' == optchr) {
			/*
			 * Deletes only from config.
			 * dynamic deletion would require deletion of
			 * actions and events associated with the timer!
			 */
			timefuse_stop(args[2]);
			auto t = Config.mutable_timefuses();
			for (auto i = t->begin(), e = t->end(); i != e; ++i) {
				if (i->name() == args[2]) {
					t->erase(i);
					return 0;
				}
			}
			r = "Invalid argument #2.";
			// return timefuse_delete(args[2]);	-- incomplete
		} else {
			r = "Invalid argument #1.";
		}
		return r;
	}
	// argc >= 4
	if ('r' == optchr) {
		const char *r = 0;
		bool rep;
		if (arg_bool(args[3],&rep))
			r = "Invalid argument #3.";
#ifdef ESP32
		else if (timefuse_repeat_set(args[2],rep))
			r = "Failed.";
#endif
		for (EventTimer &t : *Config.mutable_timefuses()) {
			if (t.name() == args[2]) {
				unsigned config = t.config();
				config &= ~1;
				config |= rep;
				t.set_config(config);
				return r;
			}
		}
		return "Invalid argument #2.";
	}
	if ('a' == optchr) {
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
#ifdef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	long ms = strtol(args[3],&e,0);
	if (*e == '.')
		return "cannot parse floats";
#else
	float ms = strtof(args[3],&e);
#endif
	if (e == args[3])
		return "Invalid argument #3.";
	switch (*e) {
	default:
		return "Invalid argument #3.";
	case 'h':
		ms *= 60*60*1000;
		break;
	case 'm':
		if (e[1] == 's') {
			// milli-seconds
			if (ms < portTICK_PERIOD_MS)
				return "Invalid argument #3.";
		} else if (e[1] == 0) {
			ms *= 60000;
		} else {
			return "Invalid argument #3.";
		}
		break;
	case 0:
	case 's':
		ms *= 1000;
		break;
	}
	if ('i' == optchr) {
		for (auto &t : *Config.mutable_timefuses()) {
			if (t.name() == args[2]) {
				t.set_time((unsigned)ms);
				return timefuse_interval_set(args[2],ms) ? "Failed." : 0;
			}
		}
		return "Invalid argument #1.";
	}
	if ('c' != optchr)
		return "Invalid argument #1.";
	EventTimer *t = Config.add_timefuses();
	t->set_name(args[2]);
	t->set_time((unsigned)ms);
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
	} else if (m == WIFI_MODE_NULL) {
		esp_wifi_stop();
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
	if (argc < 3) 
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
		int x = 2;
		while (x < argc) {
			if (log_module_enable(args[x]))
				return "Invalid argument #2.";
			bool add = true;
			for (auto &d : Config.debugs()) {
				if (d == args[x]) {
					add = false;
					break;
				}
			}
			if (add)
				Config.add_debugs(args[x]);
			++x;
		}
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


#ifdef CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
static const char *dumpcore(Terminal &term, const char *fn)
{
#ifndef HAVE_FS
	if (fn)
		return "No filesystem.";
#endif
	size_t addr,size;
	if (esp_err_t e = esp_core_dump_image_get(&addr,&size))
		return esp_err_to_name(e);
	spi_flash_mmap_handle_t handle;
	const uint8_t *data = 0;
	if (esp_err_t e = spi_flash_mmap(addr,size,SPI_FLASH_MMAP_DATA,(const void**)&data,&handle)) {
		term.printf("mmap flash: %s\n",esp_err_to_name(e));
		return "";
	}
	const char *ret = 0;
	if (fn == 0) {
		term.print_hex(data,size);
	} else {
		const char *f = fn;
		if (fn[0] != '/') {
			const estring &pwd = term.getPwd();
			size_t pl = pwd.size();
			size_t l = strlen(fn);
			char *p = (char *)alloca(l+pl+1);
			memcpy(p,pwd.data(),pl);
			memcpy(p+pl,fn,l+1);
			f = p;
		}
		int fd = open(f,O_CREAT|O_RDWR|O_EXCL);
		if (fd == -1) {
			ret = strerror(errno);
		} else {
			int n = write(fd,data,size);
			if (n == -1)
				ret = strerror(errno);
			close(fd);
		}
	}
	spi_flash_munmap(handle);
	return ret;;
}


static const char *dumpadm(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {

	} else if (argc == 2) {
		if (0 == strcmp(args[1],"-s")) {
			size_t addr,size;
			if (esp_err_t e = esp_core_dump_image_get(&addr,&size))
				return esp_err_to_name(e);
			term.printf("core dump: %d@0x%x\n",size,addr);
			esp_core_dump_summary_t sum;
			if (esp_err_t e = esp_core_dump_get_summary(&sum))
				return esp_err_to_name(e);
			term.printf("crash at 0x%x in task %s\n",sum.exc_pc,sum.exc_task);
		} else if (0 == strcmp(args[1],"-c")) {
			esp_err_t e = esp_core_dump_image_check();
			term.println(esp_err_to_name(e));
		} else if (0 == strcmp(args[1],"-e")) {
			if (esp_err_t e = esp_core_dump_image_erase())
				return esp_err_to_name(e);
		} else if (0 == strcmp(args[1],"-f")) {
			dumpcore(term,"core");
		} else if (0 == strcmp(args[1],"-x")) {
			dumpcore(term,0);
		} else {
			return "Invalid argument #1.";
		}
	} else if (argc == 3) {
		if (0 == strcmp(args[1],"-f")) {
			dumpcore(term,args[2]);
		} else {
			return "Invalid argument #1.";
		}
	} else {
		return "Invalid number of arguments.";
	}
	return 0;
}
#endif


#ifdef CONFIG_DISPLAY
static const char *font(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		return "Invalid number of arguments.";
	if (0 == strcmp(args[1],"ls")) {
		TextDisplay *t = TextDisplay::getFirst();
		if (0 == t)
			return "No display.";
		unsigned x = 0;
		term.println("default fonts:");
		term.printf("tiny  : %s\n",DefaultFonts[0]->name);
		term.printf("small : %s\n",DefaultFonts[1]->name);
		term.printf("medium: %s\n",DefaultFonts[2]->name);
		term.printf("large : %s\n",DefaultFonts[3]->name);
		term.println("\nid miny maxy boff maxw yadv nchr name");
		while (const Font *f = t->getFont(x)) {
			term.printf("%2u %4d %4u %4u %4u %4u %4u %s\n",x,f->minY,f->maxY,f->blOff,f->maxW,f->yAdvance,f->extra+f->last-f->first+1,f->name);
			++x;
		}
	} else if (0 == strcmp(args[1],"print")) {
		if (argc != 3) 
			return "Invalid number of arguments.";
		TextDisplay *t = TextDisplay::getFirst();
		if (0 == t)
			return "No display.";
		const Font *f = t->getFont(args[2]);
		if (0 == f) {
			char *e;
			long id = strtol(args[2],&e,0);
			f = t->getFont(id);
			if ((*e) || (0 == f))
				return "Font not found.";
		}
		term.printf("first 0x%x, last 0x%x, extra %d\n",f->first,f->last,f->extra);
		unsigned num = f->last-f->first+f->extra;
		const glyph_t *g = &f->glyph[0];
		for (unsigned x = 0; x <= num; ++x) {
			uint8_t c = g->iso8859;
			if (c < 0x80)
				term.printf("0x%02x '%c'",c,c);
			else
				term.printf("0x%02x '%c%c'",c,0xc0|(c>>6),(c&0x3f)|0x80);
			term.printf(": %2ux%2u at %+3d,%+3d adv %u\n",g->width,g->height,g->xOffset,g->yOffset,g->xAdvance);
			++g;
		}
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}
#endif


static const char *nslookup(Terminal &term, int argc, const char *args[])
{
	if (argc != 2)
		return "Invalid number of arguments.";
	ip_addr_t ip;
	if (err_t e = resolve_hostname(args[1],&ip))
		return strlwiperr(e);
	if (ip_addr_isany_val(ip))
		return "Unknown host.";
	char tmp[60];
	ipaddr_ntoa_r(&ip,tmp,sizeof(tmp));
	term.println(tmp);
	return 0;
}


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
			if (get_time_of_day(&h,&m))
				return "Time invalid.";
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
		const char *tz = Config.timezone().c_str();
		if (tz == 0)
			tz = "<not set>";
		term.printf("timezone: %s\n",tz);
		return 0;
	}
	return update_setting(term,"timezone",args[1]);
}


static const char *xxdSettings(Terminal &t)
{
	size_t s = Config.calcSize();
	uint8_t *buf = (uint8_t *)malloc(s);
	if (buf == 0)
		return "Out of memory.";
	Config.toMemory(buf,s);
	t.print_hex(buf,s);
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
		if (argc == 2)
			Config.toJSON(term);
#ifdef HAVE_GET_MEMBER
		else if (Message *m = Config.getMember(args[2]))
			m->toJSON(term);
		else
			return "Invalid argument #2.";
#else
		else
			return "Invalid number of arguments.";
#endif
		return 0;
	} else if (0 == strcmp(args[1],"print")) {
#ifdef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
		term.println("compiled out");
#else
		if (argc == 2)
			Config.toASCII(term);
#ifdef HAVE_GET_MEMBER
		else if (Message *m = Config.getMember(args[2]))
			m->toASCII(term);
		else
			return "Invalid argument #2.";
#else
		else
			return "Invalid number of arguments.";
#endif
		term.println();
#endif
		return 0;
	} else if (!strcmp("add",args[1])) {
		if (argc < 3)
			return "Missing argument.";
		char arrayname[strlen(args[2])+4];
		strcpy(arrayname,args[2]);
		strcat(arrayname,"[+]");
		if (Config.setByName(arrayname, argc > 3 ? args[3] : 0) < 0)
			return "Failed.";
		return 0;
	} else if (!strcmp("set",args[1])) {
		int err;
		if (argc == 4) {
			err = Config.setByName(args[2],args[3]);
		} else if (argc == 5) {
			char tmp[strlen(args[3])+strlen(args[4])+2];
			strcpy(tmp,args[3]);
			strcat(tmp," ");
			strcat(tmp,args[4]);
			err = Config.setByName(args[2],tmp);
		} else
			return "Missing argument.";
		if (err < 0) {
			term.printf("error %d\n",err);
			return "";
		}
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
			return "Invalid hex input.";
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
		term.print_hex(rtcfgbuf.data(),rtcfgbuf.size());
	} else if (!strcmp(args[1],"xxd")) {
		return xxdSettings(term);
	} else if (!strcmp(args[1],"nvxxd")) {
		size_t s = 0;
		uint8_t *buf = 0;
		if (int e = nvm_read_blob("node.cfg",&buf,&s))
			return e ? "Failed." : 0;
		term.print_hex(buf,s);
		free(buf);
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}


// requires configUSE_TRACE_FACILITY in FreeRTOSConfig.h
// requires CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS for CPU times
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
		term.printf("%3u  %c %4u %10lu %5u %3d %s\n"
#else
		term.printf("%3u  %c %4u %10lu %5u %s\n"
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
static const char *update(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		return help_cmd(term,args[0]);
	if (heap_caps_get_free_size(MALLOC_CAP_32BIT) < (12<<10))
		term.println("WARNING: memory low!");
	UBaseType_t p = uxTaskPriorityGet(0);
	vTaskPrioritySet(0, 11);
	const char *r = 0;
	if (argc == 3) {
		if (0 == strcmp(args[1],"-b")) {
			r = perform_ota(term,(char*)args[2],true);
			if (r == 0) {
				shell_reboot(term,argc,args);
			}
		} else if (0 == strcmp(args[1],"-v")) {
			return ota_from_server(term,Config.otasrv().c_str(),args[2]);
		} else {
			return "Invalid argument #1.";
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
#if IDF_VERSION >= 50
		uint32_t mhz = ets_get_cpu_frequency();
#else
		int32_t f = esp_clk_cpu_freq();
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
#ifdef RTC_CPU_FREQ_XTAL
		case RTC_CPU_FREQ_XTAL:
			mhz = rtc_clk_xtal_freq_get();
			break;
#endif
#ifdef RTC_CPU_FREQ_240M
		case RTC_CPU_FREQ_240M:
			mhz = 240;
			break;
#endif
#ifdef RTC_CPU_FREQ_2M
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
			term.print(", BLE");
		if (ci.features & CHIP_FEATURE_EMB_FLASH) 
			term.print(", flash");
#ifdef CHIP_FEATURE_EMB_PSRAM
		if (ci.features & CHIP_FEATURE_EMB_PSRAM) 
			term.print(", PSRAM");
#endif
#ifdef CHIP_FEATURE_IEEE802154
		if (ci.features & CHIP_FEATURE_IEEE802154)
			term.print(", IEEE 802.15.4");
#endif
		term.println();
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
	if (args[1][0] != '-') {
		setPassword(args[1]);
	} else if (args[1][2]) {
		return "Invalid argument #1.";
	} else if (args[1][1] == 'c') {
		Config.clear_pass_hash();
		term.println("password cleared");
	} else if (args[1][1] == 'r') {
		term.println("password empty");
		setPassword("");
	} else if (args[1][1] == 's') {
		char buf[32];
		term.write(PW,sizeof(PW));
		int n = term.readInput(buf,sizeof(buf),false);
		if (n < 0)
			return "No input.";
		buf[n] = 0;
		setPassword(buf);
		term.setPrivLevel(0);
		term.println();
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}


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
		term.println();
		if (!verifyPassword(buf))
			return "Access denied.";
	}
	term.setPrivLevel(1);
done:
	term.printf("Privileg level %u\n",term.getPrivLevel());
	return 0;
}


static const char *termtype(Terminal &term, int argc, const char *args[])
{
	return term.type();
}


#ifdef CONFIG_THRESHOLDS
static void print_thresholds(Terminal &t, EnvObject *o, int indent)
{
	unsigned c = 0;
	while (EnvElement *e = o->getChild(c++)) {
		const char *name = e->name();
		if (EnvObject *c = e->toObject()) {
			for (int i = 0; i < indent; ++i)
				t << "    ";
			t << name;
			t << ":\n";
			print_thresholds(t,c,indent+1);
		} else if (EnvNumber *n = e->toNumber()) {
			float lo = n->getLow();
			if (!isnan(lo)) {
				for (int i = 0; i < indent; ++i)
					t << "    ";
				t << name;
				t << ": ";
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
		if (EnvElement *e = RTData->getByPath(args[1])) {
			if (EnvNumber *n = e->toNumber()) {
				term << n->name() << ": " << n->getLow() << ", " << n->getHigh() << '\n';
				return 0;
			}
		} else if (EnvElement *e = RTData->find(args[1])) {
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
		EnvNumber *n = 0;
		if (EnvElement *e = RTData->getByPath(args[1])) {
			n = e->toNumber();
		} else if (EnvElement *e = RTData->find(args[1])) {
			n = e->toNumber();
		}
		if (n == 0)
			return "Invalid argument #1.";
		n->setThresholds(lo,hi);
		bool updated = false;
		for (ThresholdConfig &t : *Config.mutable_thresholds()) {
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


#if IDF_VERSION >= 50
static void print_ipinfo(Terminal &t, esp_netif_t *itf, esp_netif_ip_info_t *i)
{
	char name[8], ipstr[32],gwstr[32];
	uint8_t mac[6];
	uint8_t m = 0;
	uint32_t nm = ntohl(i->netmask.addr);
	while (nm & (1<<(31-m)))
		++m;
	if (esp_netif_get_netif_impl_name(itf,name))
		return;
	t.printf("if%d/%s (%s):\n",esp_netif_get_netif_impl_index(itf),name,esp_netif_get_desc(itf));
	if (ESP_OK == esp_netif_get_mac(itf,mac))
		t.printf("\tmac       : %02x:%02x:%02x:%02x:%02x:%02x\n",
			mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
	ip4addr_ntoa_r((ip4_addr_t *)&i->ip.addr,ipstr,sizeof(ipstr));
	ip4addr_ntoa_r((ip4_addr_t *)&i->gw.addr,gwstr,sizeof(gwstr));
	t.printf("\tipv4      : %s/%d, gw: %s, %s\n",ipstr,m,gwstr,esp_netif_is_netif_up(itf) ? "up":"down");
#if defined CONFIG_LWIP_IPV6
	esp_ip6_addr_t ip6;
	if (ESP_OK == esp_netif_get_ip6_linklocal(itf,&ip6)) {
		ip6addr_ntoa_r((ip6_addr_t*)&ip6,ipstr,sizeof(ipstr));
		t.printf("\tlink-local: %s\n",ipstr);
	}
	if (ESP_OK == esp_netif_get_ip6_global(itf,&ip6)) {
		ip6addr_ntoa_r((ip6_addr_t*)&ip6,ipstr,sizeof(ipstr));
		t.printf("\tglobal    : %s\n",ipstr);
	}
#endif
}
#else
static void print_ipinfo(Terminal &t, const char *itf, tcpip_adapter_ip_info_t *i, bool up)
{
	uint8_t m = 0;
	uint32_t nm = ntohl(i->netmask.addr);
	while (nm & (1<<(31-m)))
		++m;
	char ipstr[32],gwstr[32];
	ip4addr_ntoa_r((ip4_addr_t *)&i->ip.addr,ipstr,sizeof(ipstr));
	ip4addr_ntoa_r((ip4_addr_t *)&i->gw.addr,gwstr,sizeof(gwstr));
	t.printf("%s%s/%u gw=%s %s\n",itf,ipstr,m,gwstr,up ? "up":"down");
}
#endif


static const char *ifconfig(Terminal &term, int argc, const char *args[])
{
	if (argc != 1) {
		return "Invalid number of arguments.";
	}
#if IDF_VERSION >= 50
	esp_netif_t *nif = 0;
	while (0 != (nif = esp_netif_next(nif))) {
		esp_netif_ip_info_t ipconfig;
		if (ESP_OK == esp_netif_get_ip_info(nif,&ipconfig))
			print_ipinfo(term, nif, &ipconfig);
	} 
#else
	tcpip_adapter_ip_info_t ipconfig;
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipconfig))
		print_ipinfo(term, "if0/sta ip=", &ipconfig, wifi_station_isup());
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ipconfig))
		print_ipinfo(term, "if1/sap ip=", &ipconfig, wifi_softap_isup());
#if defined TCPIP_ADAPTER_IF_ETH
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_ETH, &ipconfig))
		print_ipinfo(term, "if2/eth ip=", &ipconfig, eth_isup());
#endif
#if defined CONFIG_LWIP_IPV6 || defined ESP32
	if (!ip6_addr_isany_val(IP6G))
		term.printf("if0/sta %s (global)\n", inet6_ntoa(IP6G));
	if (!ip6_addr_isany_val(IP6LL))
		term.printf("if0/sta %s (link-local)\n", inet6_ntoa(IP6LL), wifi_station_isup());
#endif
#endif
	return 0;
}

#ifdef CONFIG_IDF_TARGET_ESP8266
extern char LDTIMESTAMP;	// address set by linker, no value, content is inaccessible!
#else
char LDTIMESTAMP;		// address set by linker, no value, content is inaccessible!
#endif

static const char *version(Terminal &term, int argc, const char *args[])
{
	if (argc != 1)
		return "Invalid number of arguments.";
	time_t s = (uint32_t) &LDTIMESTAMP;
	struct tm tm;
	gmtime_r((time_t *)&s,&tm);
	term.printf("Atrium Version %s\n"
		"%s\n"
		"%s\n"
		"firmware config %s\n"
		"built on %u-%u-%u, %u:%02u:%02u, UTC\n" 
		"IDF version: %s\n"
		"https://github.com/maierkomor/atrium\n"
		, Version
		, Copyright
		, License
		, FwCfg
		, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec
		, esp_get_idf_version()
		);
	return 0;
}


extern const char *adc(Terminal &term, int argc, const char *args[]);
extern const char *at(Terminal &term, int argc, const char *args[]);
extern const char *bme(Terminal &term, int argc, const char *args[]);
extern const char *buzzer(Terminal &t, int argc, const char *args[]);
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
extern const char *mqtt(Terminal &term, int argc, const char *args[]);
extern const char *nightsky(Terminal &term, int argc, const char *args[]);
extern const char *ping(Terminal &t, int argc, const char *args[]);
extern const char *prof(Terminal &term, int argc, const char *args[]);
extern const char *process(Terminal &term, int argc, const char *args[]);
extern const char *readelf(Terminal &term, int argc, const char *args[]);
extern const char *relay(Terminal &term, int argc, const char *args[]);
extern const char *shell_format(Terminal &term, int argc, const char *args[]);
extern const char *sm_cmd(Terminal &term, int argc, const char *args[]);
extern const char *sntp(Terminal &term, int argc, const char *args[]);
extern const char *spicmd(Terminal &term, int argc, const char *args[]);
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
#endif
#ifdef CONFIG_BUZZER
	{"buzzer",0,buzzer,"buzzer with frequency and time",0},
#endif
	{"cat",0,shell_cat,"cat file",0},
#if defined CONFIG_FATFS || defined CONFIG_ROMFS_VFS
	{"cd",0,shell_cd,"change directory",0},
#endif
	{"config",1,config,"system configuration",config_man},
#if defined CONFIG_SPIFFS || defined CONFIG_FATFS
	{"cp",0,shell_cp,"copy file",0},
#endif
	{"cpu",1,cpu,"CPU speed",0},
	{"debug",0,debug,"enable debug logging (* for all)",debug_man},
#if defined CONFIG_SPIFFS || defined CONFIG_FATFS
	{"df",0,shell_df,"available storage",0},
#endif
#ifdef CONFIG_DIMMER
	{"dim",0,dim,"operate dimmer",dim_man},
#endif
#if CONFIG_SYSLOG
	{"dmesg",0,dmesg,"system log",dmesg_man},
#endif
#if defined HAVE_FS && defined CONFIG_OTA
	{"download",1,download,"http file download",0},
#endif
#ifdef CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
	{"dumpadm",1,dumpadm,"handle core dumps",dumpadm_man},
#endif
	{"env",0,env,"print variables",0},
	{"event",0,event,"handle trigger events as actions",event_man},
#ifdef CONFIG_DISPLAY
	{"font",0,font,"font listing",font_man},
#endif
#if defined CONFIG_SPIFFS || defined CONFIG_FATFS
	{"format",1,shell_format,"format storage",0},
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
	{"i2c",0,i2c,"list/configure/operate I2C devices",i2c_man},
#endif
	{"inetadm",1,inetadm,"manage network services",inetadm_man},
#ifdef CONFIG_INFLUX
	{"influx",1,influx,"configure influx UDP data gathering",influx_man},
#endif
	{"ip",0,ifconfig,"network IP and interface settings",ip_man},
#ifdef CONFIG_LEDS
	{"led",0,led_set,"status LED",status_man},
#endif
	{"ls",0,shell_ls,"list storage",0},
#ifdef CONFIG_LUA
	{"lua",0,xluac,"run a lua script",lua_man},
	{"luac",0,xluac,"compile lua script",luac_man},
#endif
#if IDF_VERSION < 50
	{"mac",0,mac,"MAC addresses",mac_man},
#endif
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
	{"passwd",1,password,"set password",passwd_man},
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	{"ping",0,ping,"ping IP address",0},
#endif
#ifdef CONFIG_FUNCTION_TIMING
	{"prof",0,prof,"profiling information",0},
#endif
#if configUSE_TRACE_FACILITY == 1
	{"ps",0,ps,"task statistics",0},
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
	{"sc",1,sc,"SmartConfig actions",0},
#endif
#ifdef CONFIG_SPI
	{"spi",0,spicmd,"list/configure/operate SPI devices",0},
#endif
#ifdef CONFIG_STATEMACHINES
	{"sm",0,sm_cmd,"state-machine states and config",sm_man},
#endif
	{"sntp",0,sntp,"simple NTP client settings",sntp_man},
	{"station",1,station,"WiFi station settings",station_man},
#ifdef CONFIG_THRESHOLDS
	{"stt",0,thresholds,"view/set schmitt-trigger thresholds",stt_man},
#endif
	{"su",0,su,"set user privilege level",su_man},
	{"subtasks",0,subtasks,"statistics of cyclic subtasks",0},
#ifdef CONFIG_TERMSERV
	{"term",0,uart_termcon,"open terminal on UART",console_man},
#endif
	{"timer",1,timefuse,"create timer",timer_man},
	{"timezone",1,timezone,"set time zone for NTP (offset to GMT or 'CET')",0},
#ifdef HAVE_FS
	{"touch",1,shell_touch,"create file, update time-stamp",0},
#endif
#ifdef CONFIG_TOUCHPAD
	{"tp",1,touchpad,"touchpad output",0},
#endif
	{"type",0,termtype,"print terminal type",0},
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
#if defined CONFIG_ROMFS || defined HAVE_FS
	char tmp[strlen(arg)+6];
#endif
#ifdef CONFIG_ROMFS
	strcpy(tmp,arg);
	strcat(tmp,".man");
	if (0 == shell_cat1(term,tmp))
		return 0;
#endif
#ifdef HAVE_FS
	strcpy(tmp,"/man/");
	strcat(tmp,arg);
	if (0 == shell_cat1(term,tmp))
		return 0;
#endif
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
	if (*cmd == 0)
		return 0;
	log_info(TAG,"on %s execute '%s'",term.type(),cmd);
	char *args[8];
	/* space trimming is done in shell()
	while ((*cmd == ' ') || (*cmd == '\t'))
		++cmd;
	*/
#ifdef CONFIG_LUA
	if ((0 == memcmp("lua",cmd,3)) && ((cmd[3] == ' ') || (cmd[3] == '\t')) && !Config.lua_disable())
		return xlua_exe(term,cmd+4);
#endif
	char *at = cmd;
	size_t n = 0;
	enum lps_e { noarg = 0, normal, squote, dquote };
	lps_e lps = noarg;
	do {
		if (n == sizeof(args)/sizeof(args[0])) {
			return "Too many arguments.";
		}
		switch (lps) {
		case noarg:
			if (*at == '\'') {
				++at;
				lps = squote;
			} else if (*at == '"') {
				++at;
				lps = dquote;
			} else if ((*at == ' ') || (*at == '\t')) {
				++at;
				continue;
			} else {
				lps = normal;;
			}
			args[n] = at;
			++at;
			++n;
			break;
		case normal:
			if ((*at == ' ') || (*at == '\t')) {
				*at = 0;
				lps = noarg;
			} else if (*at == 0) {
				continue;
			}
			++at;
			break;
		case squote:
			if (*at == '\'') {
				lps = noarg;
				*at = 0;
			} else if (at == 0) {
				return "missing '";
			}
			++at;
			break;
		case dquote:
			if (*at == '"') {
				lps = noarg;
				*at = 0;
			} else if (at == 0) {
				return "missing \"";
			}
			++at;
			break;
		default:
			abort();
		}
	} while (at && *at);

	for (const auto &e : ExeNames) {
		if (0 == strcmp(args[0],e.name)) {
			if ((e.flags & EXEFLAG_INTERACTIVE) && !term.isInteractive()) {
				return "Non-interactive terminal.";
			}
			if (!Config.pass_hash().empty() && ((e.flags & EXEFLAG_ADMIN) > term.getPrivLevel())) {
				return "Access denied.";
			}
			if ((n == 2) && (0 == strcmp("-h",args[1])))
				return help_cmd(term,args[0]);
			//term.printf("calling shell function '%s'\n",ExeNames[i].name);
			return e.function(term,n,(const char **)args);
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
	int r;
	for (;;) {
		if (prompt) {
			term.write("> ",2);
			term.sync();
		}
		r = term.readInput(com,sizeof(com)-1,true);
		char *at = com;
		while ((r != 0) && ((*at == ' ') || (*at == '\t'))) {
			++at;
			--r;
		}
		if ((r < 0) || ((r == 4) && !memcmp(at,"exit",4)))
			return;
		const char *msg = 0;
		if (at[0] == '#') {
		} else if (r > 0) {
			term.println();
			at[r] = 0;
			msg = shellexe(term,at);
			if (msg == 0)
				msg = "OK.";
		}
		term.println(msg);
	}
}
