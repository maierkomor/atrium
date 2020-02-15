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

#include "log.h"
#include "terminal.h"

#if CONFIG_SPIFFS
#if defined ESP8266 && IDF_VERSION < 32
#error SPIFFS does not work on ESP8266 for IDF < v3.2
#endif

#include <esp_spi_flash.h>
#include <esp_spiffs.h>

static char TAG[] = "FS";

extern "C"
void init_fs()
{
	log_info(TAG,"init spiffs");
	esp_vfs_spiffs_conf_t conf;
	bzero(&conf,sizeof(conf));
	conf.base_path = CONFIG_MOUNT_POINT;
	conf.partition_label = "storage";
	conf.max_files = 4;
	conf.format_if_mount_failed = false;

	if (esp_err_t e = esp_vfs_spiffs_register(&conf)) {
		log_error(TAG, "SPIFFS mounting failed: %s",esp_err_to_name(e));
	} else {
		size_t total = 0, used = 0;
		if (esp_err_t e = esp_spiffs_info("storage", &total, &used))
			log_error(TAG, "error getting SPIFFS info: %s",esp_err_to_name(e));
		else
			log_info(TAG, "SPIFFS at %s: %ukB of %ukB used",conf.base_path,used>>10,total>>10);
	}
}


int shell_format(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		term.printf("%s: 1 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1) {
		term.printf("%s: syntax: format <partition-label>\n",args[0]);
	} else if (esp_err_t r = esp_spiffs_format(args[1])) {
		term.printf("format failed: %s\n",esp_err_to_name(r));
		return 1;
	}
	return 0;
}
#elif defined CONFIG_FATFS
#include <esp_vfs_fat.h>
#include <strings.h>

static char TAG[] = "FS";
char PartitionName[] = "storage";
wl_handle_t SpiFatFs;

extern "C"
void init_fs()
{
	esp_vfs_fat_mount_config_t fatconf;
	bzero(&fatconf,sizeof(fatconf));
	fatconf.format_if_mount_failed = false;
	fatconf.max_files = 4;
	fatconf.allocation_unit_size = 4096;	// esp-idf >= v3.1
	SpiFatFs = WL_INVALID_HANDLE;
	if (esp_err_t r = esp_vfs_fat_spiflash_mount(CONFIG_MOUNT_POINT, PartitionName, &fatconf, &SpiFatFs)) {
		log_error(TAG,"unable to mount flash with fatfs: %s",esp_err_to_name(r));
		SpiFatFs = 0;
	} else 
		log_info(TAG,"mounted fatfs partition '%s'",PartitionName);
}


int shell_format(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		term.printf("%s: 1 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1) {
		term.printf("%s: syntax: format <partition-label>\n",args[0]);
		return 0;
	}
	extern wl_handle_t SpiFatFs;
	extern char PartitionName[];
	esp_vfs_fat_mount_config_t fatconf;
	bzero(&fatconf,sizeof(fatconf));
	fatconf.format_if_mount_failed = false;
	fatconf.max_files = 4;
	fatconf.allocation_unit_size = 4096;	// esp-idf >= v3.1
	SpiFatFs = WL_INVALID_HANDLE;
	if (esp_err_t r = esp_vfs_fat_spiflash_mount(CONFIG_MOUNT_POINT, PartitionName, &fatconf, &SpiFatFs)) {
		term.printf("unable to mount flash with fatfs: %s\n",esp_err_to_name(r));
		SpiFatFs = 0;
	} else 
		term.printf("mounted fatfs partition '%s'\n",PartitionName);
	return 0;
}
#elif defined CONFIG_ROMFS
#include "romfs.h"

static char TAG[] = "FS";

extern "C"
void init_fs()
{
	log_info(TAG,"init romfs");
	romfs_setup();
}


#else

extern "C"
void init_fs()
{

}

#endif


