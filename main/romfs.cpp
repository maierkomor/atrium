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

// CONFIG_ENABLE_FLASH_MMAP is unsupported on ESP8266 due to HW limitations

#include "romfs.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include <esp_partition.h>
#ifdef CONFIG_IDF_TARGET_ESP32
#include <esp_spi_flash.h>
#else
#include <spi_flash.h>
#endif


#ifdef CONFIG_ROMFS

// flash ROM entry generated by mkrom

typedef struct RomEntry16
{
	uint16_t offset, size;
	char name[12];
} RomEntry16;


typedef struct RomEntry32
{
	uint32_t offset, size;
	char name[24];
} RomEntry32;

#if defined ROMFS16
#define RomEntry RomEntry16
#define ROMFS_MAGIC "ROMFS16"
#elif defined ROMFS32
#define RomEntry RomEntry32
#define ROMFS_MAGIC "ROMFS32"
#elif defined CONFIG_IDF_TARGET_ESP8266
#define RomEntry RomEntry16
#define ROMFS_MAGIC "ROMFS16"
#elif defined CONFIG_IDF_TARGET_ESP32
#define RomEntry RomEntry32
#define ROMFS_MAGIC "ROMFS32"
#else
#error RomEntry variant not set
#endif

using namespace std;

#define TAG MODULE_ROMFS
static RomEntry *Entries = 0;
static unsigned NumEntries = 0;
uint32_t RomfsBaseAddr = 0, RomfsSpace = 0;


uint32_t romfs_get_base(const char *pn)
{
	return RomfsBaseAddr;
}


static RomEntry *get_entry(const char *n)
{
	for (RomEntry *i = Entries, *e = Entries+NumEntries; i != e; ++i) {
		if (0 == strcmp(n,i->name))
			return i;
	}
	return 0;
}


ssize_t romfs_size(const char *n)
{
	RomEntry *i  = get_entry(n);
	if (i)
		return i->size;
	return -1;
}


int romfs_open(const char *n)
{
	RomEntry *i  = get_entry(n);
	if (i)
		return i - Entries;
	return -1;
}


ssize_t romfs_size_fd(int i)
{
	if ((i < 0) || (i >= NumEntries))
		return -1;
	return Entries[i].size;
}


const char *romfs_name(int i)
{
	if ((i < 0) || (i >= NumEntries))
		return 0;
	return Entries[i].name;
}


#ifdef CONFIG_ENABLE_FLASH_MMAP
void *romfs_mmap(int i)
{
	if ((i < 0) || (i >= NumEntries))
		return 0;
	return (void*)(RomfsBaseAddr+Entries[i].offset);
}
#endif


size_t romfs_offset(const char *n)
{
	RomEntry *i  = get_entry(n);
	if (i)
		return i->offset;
	return 0;
}


size_t romfs_offset(int i)
{
	if ((i < 0) || (i >= NumEntries))
		return 0;
	return Entries[i].offset;
}


int romfs_read_at(int i, char *buf, size_t n, size_t o)
{
	if ((i < 0) || (i >= NumEntries))
		return -1;
	uint32_t s = Entries[i].size;
	if ((o+n) > s)
		return -1;
	uint32_t off = Entries[i].offset;
	log_dbug(TAG,"read %s at %u",Entries[i].name,off);
	assert(((uint32_t)buf & 3) == 0);
#ifdef CONFIG_ENABLE_FLASH_MMAP
	memcpy(buf,(void*)(RomfsBaseAddr+off+o),n);
#else
	if (auto e = spi_flash_read(RomfsBaseAddr+off+o,buf,n)) {
		log_warn(TAG,"spi_flash_read: %s",esp_err_to_name(e));
		return -1;
	}
#endif
	return n;
}


void romfs_getentry(const char *n, size_t *s, size_t *o)
{
	RomEntry *i  = get_entry(n);
	if (i) {
		*o = i->offset;
		*s = i->size;
	} else {
		*o = 0;
		*s = 0;
	}
}


size_t romfs_num_entries()
{
	return NumEntries;
}


extern "C"
void romfs_setup()
{
	auto pi = esp_partition_find(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,0);
	const esp_partition_t *p = 0;
	log_dbug(TAG,"looking for " ROMFS_MAGIC);
	while (pi != 0) {
		p = esp_partition_get(pi);
		uint8_t magic[8];
		spi_flash_read(p->address,magic,sizeof(magic));
		log_hex(TAG,magic,sizeof(magic),"partition %s",p->label);
		if (0 == memcmp(magic,ROMFS_MAGIC,sizeof(magic)))
			break;
		pi = esp_partition_next(pi);
	}
	if (p == 0) {
		log_dbug(TAG,"no romfs found");
		return;
	}
	log_dbug(TAG,"using partition %s",p->label);

	RomfsBaseAddr = p->address;
	RomfsSpace = p->size;
#ifdef CONFIG_ENABLE_FLASH_MMAP
	spi_flash_mmap_handle_t handle;
	if (esp_err_t e = spi_flash_mmap(p->address,p->size,SPI_FLASH_MMAP_DATA,(const void**)&RomfsBaseAddr,&handle)) {
		log_error(TAG,"mmap failed: %s",esp_err_to_name(e));
		NumEntries = 0;
		return;
	}
	Entries = (RomEntry*)(RomfsBaseAddr+8);
	RomEntry *e = Entries;
	while ((e->size != 0) && (e->offset != 0))
		++e;
	NumEntries = e - Entries;
#else
	uint32_t flashrom = RomfsBaseAddr;
	char magic[8];
	if (esp_err_t e = spi_flash_read(flashrom,magic,sizeof(magic)))
		log_warn(TAG,"flash read %u@0x%x: %s",sizeof(magic),flashrom,esp_err_to_name(e));
	if (strcmp(magic,ROMFS_MAGIC)) {
		log_info(TAG,"no " ROMFS_MAGIC " at 0x%x: %02x %02x %02x %02x",flashrom, magic[0], magic[1], magic[2], magic[3]);
		return;
	}
	flashrom += 8;
	RomEntry e;
	spi_flash_read(flashrom,&e,sizeof(e));
	while ((e.size != 0) && (e.offset != 0)) {
		++NumEntries;
		RomEntry *n;
		if (Entries)
			n = (RomEntry*) realloc(Entries,NumEntries*sizeof(RomEntry));
		else
			n = (RomEntry*) malloc(NumEntries*sizeof(RomEntry));
		if (n == 0) {
			log_error(TAG,"out of memory");
			NumEntries = 0;
			if (Entries)
				free(Entries);
			Entries = 0;
			return;
		}
		Entries = n;
		memcpy(Entries+(NumEntries-1),&e,sizeof(RomEntry));
		flashrom += sizeof(RomEntry);
		spi_flash_read(flashrom,&e,sizeof(e));
	}
#endif
	log_info(TAG,"romfs at 0x%x: %u entries",RomfsBaseAddr,NumEntries);
}

#endif
