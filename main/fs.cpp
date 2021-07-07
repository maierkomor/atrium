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

#include "fs.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "terminal.h"
#include "settings.h"
#include "shell.h"
#include "swcfg.h"

#include <stdlib.h>

#define MOUNT_POINT "/flash"
#define DATA_PARTITION "storage"

static const char TAG[] = "FS";

#if CONFIG_SPIFFS
#if defined ESP8266 && IDF_VERSION < 32
#error SPIFFS does not work on ESP8266 for IDF < v3.2
#endif

#include <esp_spi_flash.h>
#include <esp_spiffs.h>


static void init_spiffs()
{
	esp_vfs_spiffs_conf_t conf;
	bzero(&conf,sizeof(conf));
	conf.base_path = "/flash";
	conf.partition_label = DATA_PARTITION;
	conf.max_files = 4;
	conf.format_if_mount_failed = false;

	if (esp_err_t e = esp_vfs_spiffs_register(&conf)) {
		log_info(TAG, "SPIFFS mounting failed: %s",esp_err_to_name(e));
	} else {
		size_t total = 0, used = 0;
		if (esp_err_t e = esp_spiffs_info(DATA_PARTITION, &total, &used)) log_error(TAG, "error getting SPIFFS info: %s",esp_err_to_name(e));
		else
			log_info(TAG, "SPIFFS at %s: %ukB of %ukB used",conf.base_path,used>>10,total>>10);
	}
}


static int shell_format_spiffs(Terminal &term, const char *arg)
{
	if (esp_err_t r = esp_spiffs_format(arg)) {
		term.println(esp_err_to_name(r));
		return 1;
	}
	return 0;
}
#endif



#if defined CONFIG_FATFS
#include <esp_vfs_fat.h>
#include <strings.h>

static wl_handle_t SpiFatFs = 0;

static void init_fatfs()
{
	esp_vfs_fat_mount_config_t fatconf;
	bzero(&fatconf,sizeof(fatconf));
	fatconf.format_if_mount_failed = false;
	fatconf.max_files = 4;
	fatconf.allocation_unit_size = 4096;	// esp-idf >= v3.1
	SpiFatFs = WL_INVALID_HANDLE;
	if (esp_err_t r = esp_vfs_fat_spiflash_mount(MOUNT_POINT, DATA_PARTITION, &fatconf, &SpiFatFs)) {
		log_error(TAG,"unable to mount flash with fatfs: %s",esp_err_to_name(r));
		SpiFatFs = 0;
	} else 
		log_info(TAG,"mounted fatfs partition " DATA_PARTITION);
}


static int shell_format_fatfs(Terminal &term, const char *arg)
{
	if (SpiFatFs)
		esp_vfs_fat_spiflash_unmount(MOUNT_POINT, SpiFatFs);
	esp_vfs_fat_mount_config_t fatconf;
	bzero(&fatconf,sizeof(fatconf));
	fatconf.format_if_mount_failed = true;
	fatconf.max_files = 4;
	fatconf.allocation_unit_size = 4096;	// esp-idf >= v3.1
	SpiFatFs = WL_INVALID_HANDLE;
	if (esp_err_t r = esp_vfs_fat_spiflash_mount(MOUNT_POINT, arg, &fatconf, &SpiFatFs)) {
		term.printf("unable to mount flash with fatfs: %s\n",esp_err_to_name(r));
		SpiFatFs = 0;
	} else 
		term.printf("mounted fatfs partition '%s'\n",arg);
	return 0;
}
#endif



#ifdef CONFIG_ROMFS
#include "romfs.h"

static void init_hwconf()
{
	log_info(TAG,"looking for hw.cfg in romfs");
	int fd = romfs_open("hw.cfg");
	ssize_t s = romfs_size_fd(fd);
	if (s <= 0) {
		log_warn(TAG,"unable to find hw.cfg");
		return;
	}
	char*buf = (char*)malloc(s);
	if (buf == 0) {
		log_error(TAG,"not enough RAM to copy hw.cfg");
		return;
	}
	romfs_read_at(fd,buf,s,0);
	if(0 == writeNVM("hw.cfg",(uint8_t*)buf,s))
		log_info(TAG,"wrote hw.cfg to NVM");
	free(buf);
}


static void init_romfs()
{
	log_info(TAG,"init romfs");
	romfs_setup();
	if (HWConf.calcSize() == 0)
		init_hwconf();
}
#endif


#if defined CONFIG_FATFS || defined CONFIG_SPIFFS
int shell_format(Terminal &term, int argc, const char *args[])
{
#if defined CONFIG_FATFS && defined CONFIG_SPIFFS
	if (argc != 3)
		return arg_missing(term);
	if ((argc == 3) && (!strcmp("spiffs",args[2]))) {
#if defined CONFIG_SPIFFS
		return shell_format_spiffs(term,args[1]);
#endif
	} else if ((argc == 3) && (!strcmp("fatfs",args[2]))) {
#if defined CONFIG_FATFS
		return shell_format_fatfs(term,args[1]);
#endif
	}
#else
	if (argc != 2)
		return arg_missing(term);
#if defined CONFIG_SPIFFS
	return shell_format_spiffs(term,args[1]);
#endif
#if defined CONFIG_FATFS
	return shell_format_fatfs(term,args[1]);
#endif
#endif
	return 1;
}
#endif


extern "C"
void init_fs()
{
#ifdef CONFIG_ROMFS
	init_romfs();
#endif
#ifdef CONFIG_SPIFFS
	init_spiffs();
#endif
#ifdef CONFIG_FATFS
	init_fatfs();
#endif
}



