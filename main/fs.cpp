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

#include "fs.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "nvm.h"
#include "terminal.h"
#include "settings.h"
#include "swcfg.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>

#include <esp_vfs.h>
#ifdef CONFIG_USB_HOST_FS
#include <esp_vfs_semihost.h>
#endif

#define MOUNT_POINT "/flash"
#define DATA_PARTITION "storage"

#define TAG MODULE_FS

#define CONFIG_ROOTFS_NUMENTR 4


#ifdef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
#define rootfs_add(x)
#else
static const char *RootEntr[CONFIG_ROOTFS_NUMENTR] = { 0 };


void rootfs_add(const char *entr)
{
	log_dbug(TAG,"rootfs add: %s",entr);
	if (entr[0] == '/')
		++entr;
	for (auto &r : RootEntr) {
		if (r == 0) {
			r = entr;
			return;
		}
	}
	log_error(TAG,"rootfs out of entries");
}

#if defined ESP32 || defined CONFIG_USING_ESP_VFS
static int rootfs_vfs_closedir(DIR *d)
{
	if (d) {
		free(d);
		return 0;
	}
	errno = EBADF;
	return -1;
}



static DIR *rootfs_vfs_opendir(const char *path)
{
	log_dbug(TAG,"rootfs_vfs_opendir('%s')",path);
	if (strcmp(path,"/"))
		return 0;
	DIR *d = (DIR *) malloc(sizeof(DIR));
	bzero(d,sizeof(DIR));
	return d;
}


static int rootfs_vfs_readdir_r(DIR *d, struct dirent *e, struct dirent **r)
{
	log_dbug(TAG,"rootfs_vfs_readdir_r(%d,...)",d ? d->dd_rsv : -1);
	if ((d == 0) || (e == 0))
		return EINVAL;
	if ((sizeof(RootEntr)/sizeof(RootEntr[0]) <= d->dd_rsv) || (RootEntr[d->dd_rsv] == 0)) {
		if (r)
			*r = 0;
		e->d_name[0] = 0;
		return 0;
	}
	if (r)
		*r = e;
	strcpy(e->d_name,RootEntr[d->dd_rsv]);
	++d->dd_rsv;
	e->d_ino = d->dd_rsv;
	e->d_type = DT_DIR;
	log_dbug(TAG,"entry %s",e->d_name);
	return 0;
}


static struct dirent *rootfs_vfs_readdir(DIR *d)
{
	static struct dirent e;
	struct dirent *r = 0;
	if (0 == rootfs_vfs_readdir_r(d,&e,&r))
		return r;
	return 0;
}
#endif


#if 0 // not needed, as stat is directed to child filesystem
static int rootfs_vfs_stat(const char *p, struct stat *st)
{
	log_dbug(TAG,"rootfs_vfs_stat(%s,...)",p);
	for (auto e : RootEntr) {
		if (0 == strcmp(p,e)) {
			st->st_size = 0;
			st->st_mode = S_IFDIR;
			return 0;
		}
	}
	return -1;
}
#endif


void rootfs_init()
{
#if defined ESP32 || defined CONFIG_USING_ESP_VFS
	esp_vfs_t vfs;
	bzero(&vfs,sizeof(vfs));
	vfs.flags = ESP_VFS_FLAG_DEFAULT;
	vfs.closedir = rootfs_vfs_closedir;
	vfs.opendir = rootfs_vfs_opendir;
	vfs.readdir = rootfs_vfs_readdir;
	vfs.readdir_r = rootfs_vfs_readdir_r;
	if (esp_err_t e = esp_vfs_register("",&vfs,0))
		log_warn(TAG,"VFS register rootfs: %s",esp_err_to_name(e));
	else
		log_info(TAG,"rootfs mounted");
#endif
}
#endif


#if CONFIG_SPIFFS
#if defined ESP8266 && IDF_VERSION < 32
#error SPIFFS does not work on ESP8266 for IDF < v3.2
#endif

#include <esp_spi_flash.h>
#ifdef CONFIG_SPIFFS
#include <esp_spiffs.h>
#endif


static void spiffs_init()
{
	esp_vfs_spiffs_conf_t conf;
	bzero(&conf,sizeof(conf));
	conf.base_path = MOUNT_POINT;
	conf.partition_label = DATA_PARTITION;
	conf.max_files = 4;
	conf.format_if_mount_failed = false;

	if (esp_err_t e = esp_vfs_spiffs_register(&conf)) {
		log_warn(TAG, "SPIFFS mounting failed: %s",esp_err_to_name(e));
	} else {
		rootfs_add(MOUNT_POINT+1);
		size_t total = 0, used = 0;
		if (esp_err_t e = esp_spiffs_info(DATA_PARTITION, &total, &used))
			log_warn(TAG,"query SPIFFS: %s",esp_err_to_name(e));
		else
			log_info(TAG, "SPIFFS at %s: %ukB of %ukB used",conf.base_path,used>>10,total>>10);
	}
}


static const char *shell_format_spiffs(Terminal &term, const char *arg)
{
	if (esp_err_t r = esp_spiffs_format(arg)) {
		term.println(esp_err_to_name(r));
		return "Failed.";
	}
	return 0;
}
#endif



#if defined CONFIG_FATFS
#include <esp_vfs_fat.h>
#include <strings.h>

static wl_handle_t SpiFatFs = WL_INVALID_HANDLE;

static void fatfs_init()
{
	esp_vfs_fat_mount_config_t fatconf;
	bzero(&fatconf,sizeof(fatconf));
	fatconf.format_if_mount_failed = true;
	fatconf.max_files = 4;
	fatconf.allocation_unit_size = CONFIG_WL_SECTOR_SIZE;
	SpiFatFs = WL_INVALID_HANDLE;
#if IDF_VERSION >= 50
	if (esp_err_t r = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_POINT, DATA_PARTITION, &fatconf, &SpiFatFs)) {
#else
	if (esp_err_t r = esp_vfs_fat_spiflash_mount(MOUNT_POINT, DATA_PARTITION, &fatconf, &SpiFatFs)) {
#endif
		log_error(TAG,"mount %s on %s with fatfs: %s", DATA_PARTITION, MOUNT_POINT, esp_err_to_name(r));
		SpiFatFs = WL_INVALID_HANDLE;
	} else  {
		log_info(TAG,"mounted fatfs partition " DATA_PARTITION);
		rootfs_add(MOUNT_POINT+1);
	}
}


static int shell_format_fatfs(Terminal &term, const char *arg)
{
	if (SpiFatFs != WL_INVALID_HANDLE) {
#if IDF_VERSION >= 50
		esp_vfs_fat_spiflash_unmount_rw_wl(MOUNT_POINT, SpiFatFs);
#else
		esp_vfs_fat_spiflash_unmount(MOUNT_POINT, SpiFatFs);
#endif
	}
	esp_vfs_fat_mount_config_t fatconf;
	bzero(&fatconf,sizeof(fatconf));
	fatconf.format_if_mount_failed = true;
	fatconf.max_files = 4;
	fatconf.allocation_unit_size = CONFIG_WL_SECTOR_SIZE;
	SpiFatFs = WL_INVALID_HANDLE;
#if IDF_VERSION >= 50
	if (esp_err_t r = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_POINT, arg, &fatconf, &SpiFatFs)) {
#else
	if (esp_err_t r = esp_vfs_fat_spiflash_mount(MOUNT_POINT, arg, &fatconf, &SpiFatFs)) {
#endif
		term.printf("unable to mount flash with fatfs: %s\n",esp_err_to_name(r));
		SpiFatFs = WL_INVALID_HANDLE;
	} else {
		rootfs_add(MOUNT_POINT+1);
		term.printf("mounted fatfs partition '%s'\n",arg);
	}
	return 0;
}
#endif



#ifdef CONFIG_ROMFS
#include "romfs.h"

static void init_hwconf()
{
	int fd = romfs_open("hw.cfg");
	ssize_t s = romfs_size_fd(fd);
	if (s <= 0) {
		log_warn(TAG,"no hw.cfg on ROMFS");
		return;
	}
	if (char *buf = (char*)malloc(s)) {
		romfs_read_at(fd,buf,s,0);
		int w = nvm_store_blob("hw.cfg",(uint8_t*)buf,s);
		free(buf);
		if (w == 0) {
			log_info(TAG,"wrote hw.cfg to NVM");
			return;
		}
	}
	log_error(TAG,"read hw.cfg: failed");
}


static void romfs_init()
{
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	if (const char *r = romfs_setup()) {
		rootfs_add(r);
	}
#endif
	if (HWConf.calcSize() == 0)
		init_hwconf();
}
#endif


#if defined CONFIG_FATFS || defined CONFIG_SPIFFS
const char *shell_format(Terminal &term, int argc, const char *args[])
{
#if defined CONFIG_FATFS && defined CONFIG_SPIFFS
	if (argc != 3)
		return "Missing argument.";
	if ((argc == 3) && (!strcmp("spiffs",args[2]))) {
#if defined CONFIG_SPIFFS
		return shell_format_spiffs(term,args[1]) ? "Failed." : 0;
#endif
	} else if ((argc == 3) && (!strcmp("fatfs",args[2]))) {
#if defined CONFIG_FATFS
		return shell_format_fatfs(term,args[1]) ? "Failed." : 0;
#endif
	}
#else
	if (argc != 2)
		return "Missing argument.";
#if defined CONFIG_SPIFFS
	return shell_format_spiffs(term,args[1]) ? "Failed." : 0;
#endif
#if defined CONFIG_FATFS
	return shell_format_fatfs(term,args[1]) ? "Failed." : 0;
#endif
#endif
	return "Failed.";
}
#endif


extern "C"
void fs_init()
{
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	rootfs_init();
#endif
#ifdef CONFIG_ROMFS
	romfs_init();
#endif
#ifdef CONFIG_SPIFFS
	spiffs_init();
#endif
#ifdef CONFIG_FATFS
	fatfs_init();
#endif
#ifdef CONFIG_USB_HOST_FS
	if (esp_err_t e = esp_vfs_semihost_register("/usb")) {
		log_warn(TAG,"USB semihost failed: %s",esp_err_to_name(e));
	} else {
		log_info(TAG,"mounted /usb");
		rootfs_add("usb");
	}
#endif
}

