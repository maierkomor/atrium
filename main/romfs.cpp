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

#include "romfs.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"
#include <esp_vfs.h>
#include <esp_partition.h>
#ifdef ESP32
#if IDF_VERSION >= 50
#include <spi_flash_mmap.h>
#else
#include <esp_spi_flash.h>
#endif
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
#elif defined ESP32
#define RomEntry RomEntry32
#define ROMFS_MAGIC "ROMFS32"
#else
#error RomEntry variant not set
#endif

using namespace std;

#define TAG MODULE_ROMFS
static RomEntry *Entries = 0;
static unsigned NumEntries = 0;
uint32_t RomfsBaseAddr = 0, RomfsSpace = 0, RomfsBaseInstr;


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


#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32C3 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3
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
	if (o == s)
		return 0;
	if (o > s)
		return -1;
	uint32_t off = Entries[i].offset;
	log_dbug(TAG,"read %s at %u",Entries[i].name,off);
	assert(((uint32_t)buf & 3) == 0);
	if (o+n > s)
		n = s-o;
#ifdef ESP32
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


#ifdef CONFIG_ROMFS_VFS
struct fd_t {
	int idx;
	size_t off;
};
fd_t FDs[CONFIG_ROMFS_VFS_NUMFDS];

#ifndef CONFIG_IDF_TARGET_ESP8266
static int romfs_vfs_pread(int fd, void *dest, size_t size, off_t off)
{
	if ((fd < 0) || (fd > CONFIG_ROMFS_VFS_NUMFDS) || (FDs[fd].idx == -1)) {
		log_dbug(TAG,"romfs_vfs_pread(%d,...,%u,%u) EBADF",fd,size,off);
		errno = EBADF;
		return -1;
	}
	int n = romfs_read_at(FDs[fd].idx,(char*)dest,size,off);
	log_dbug(TAG,"romfs_vfs_pread(%d,...,%u,%u) = %d",fd,size,off,n);
	return n;
}
#endif

static int romfs_vfs_read(int fd, void *dest, size_t size)
{
	if ((fd < 0) || (fd > CONFIG_ROMFS_VFS_NUMFDS) || (FDs[fd].idx == -1)) {
		log_dbug(TAG,"romfs_vfs_read(%d,...,%u) EBADF",fd,size);
		errno = EBADF;
		return -1;
	}
	int n = romfs_read_at(FDs[fd].idx,(char*)dest,size,FDs[fd].off);
	if (n > 0)
		FDs[fd].off += n;
	log_dbug(TAG,"romfs_vfs_read(%d,...,%u) = %d",fd,size,n);
	return n;
}


static off_t romfs_vfs_lseek(int fd, off_t off, int whence)
{
	log_dbug(TAG,"romfs_vfs_lseek(%d,%d)",fd,off);
	if ((fd < 0) || (fd > CONFIG_ROMFS_VFS_NUMFDS) || (FDs[fd].idx == -1)) {
		errno = EBADF;
		return -1;
	}
	off_t noff;
	if (whence == SEEK_CUR) {
		noff = FDs[fd].off + off;
	} else if (whence == SEEK_SET) {
		noff = off;
	} else if (whence == SEEK_END) {
		noff = Entries[FDs[fd].idx].size - 1 + off;
	} else {
		errno = EINVAL;
		return -1;
	}
	if ((noff < 0) || (FDs[fd].off + off >= Entries[FDs[fd].idx].size)) {
		errno = EINVAL;
		return -1;
	}
	FDs[fd].off = noff;
	return noff;
}


static int romfs_vfs_open(const char *path, int flags, int mode)
{
	if ((flags != 0) && (mode != O_RDONLY)) {
		errno = EINVAL;
		return -1;
		log_dbug(TAG,"romfs_vfs_open('%s',0x%x,0x%x) = -1",path,flags,mode);
	}
	int idx = romfs_open(path+1);	// strip leading slash
	if (idx == -1) {
		log_dbug(TAG,"romfs_vfs_open('%s',0x%x,0x%x) = ENOENT",path,flags,mode);
		errno = ENOENT;
		return -1;
	}
	int fd = -1;
	do {
		++fd;
		if (fd == CONFIG_ROMFS_VFS) {
			log_dbug(TAG,"romfs_vfs_open('%s',0x%x,0x%x) = ENOFDS",path,flags,mode);
			errno = EMFILE;
			return -1;
		}
	} while (FDs[fd].idx != -1);
	FDs[fd].idx = idx;
	FDs[fd].off = 0;
	log_dbug(TAG,"romfs_vfs_open('%s',0x%x,0x%x) = %d [%d]",path,flags,mode,fd,idx);
	return fd;
}


static int romfs_vfs_close(int fd)
{
	if ((fd < 0) || (fd >= CONFIG_ROMFS_VFS_NUMFDS) || (FDs[fd].idx == -1)) {
		errno = EBADF;
		return -1;
	}
	FDs[fd].idx = -1;
	return 0;
}


static DIR *romfs_vfs_opendir(const char *path)
{
	log_dbug(TAG,"romfs_vfs_opendir('%s')",path);
	if (strcmp(path,"/"))
		return 0;
	DIR *d = (DIR *) malloc(sizeof(DIR));
	bzero(d,sizeof(DIR));
	return d;
}


static int romfs_vfs_readdir_r(DIR *d, struct dirent *e, struct dirent **r)
{
	log_dbug(TAG,"romfs_vfs_readdir_r(%d,...)",d ? d->dd_rsv : -1);
	if ((d == 0) || (e == 0))
		return EINVAL;
	if (NumEntries <= d->dd_rsv) {
		if (r)
			*r = 0;
		e->d_name[0] = 0;
		return 0;
	}
	if (r)
		*r = e;
	e->d_ino = 0;
	e->d_type = 1;
	strcpy(e->d_name,Entries[d->dd_rsv].name);
	log_dbug(TAG,"entry %s",e->d_name);
	++d->dd_rsv;
	return 0;
}


static struct dirent *romfs_vfs_readdir(DIR *d)
{
	static struct dirent e;
	struct dirent *r = 0;
	if (0 == romfs_vfs_readdir_r(d,&e,&r))
		return r;
	return 0;
}


static int romfs_vfs_closedir(DIR *d)
{
	if (d) {
		free(d);
		return 0;
	}
	errno = EBADF;
	return -1;
}


static int romfs_vfs_fstat(int fd, struct stat *st)
{
	if ((fd < 0) || (fd >= CONFIG_ROMFS_VFS_NUMFDS) || (FDs[fd].idx == -1)) {
		log_dbug(TAG,"romfs_vfs_fstat(%d,...) = -1",fd);
		errno = EINVAL;
		return -1;
	}
	st->st_size = Entries[FDs[fd].idx].size;
	st->st_mode = S_IFREG;
	log_dbug(TAG,"romfs_vfs_fstat(%d,...): size=%d",fd,st->st_size);
	return 0;
}


static int romfs_vfs_stat(const char *p, struct stat *st)
{
	log_dbug(TAG,"romfs_vfs_stat(%s,...)",p);
	if ((p == 0) || (st == 0)) {
		errno = EINVAL;
		return -1;
	}
#ifdef CONFIG_ROMFS_VFS
	if ((p[0] == '/') && (p[1] == 0)) {
		st->st_size = 0;
		st->st_mode = S_IFDIR;
		return 0;
	}
#endif
	RomEntry *i  = get_entry(p+1);	// omit leading slash
	if (i == 0) {
		errno = ENOENT;
		return -1;
	}
	st->st_size = i->size;
	st->st_mode = S_IFREG;
	return 0;
}
#endif


extern "C"
const char *romfs_setup()
{
	auto pi = esp_partition_find(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,0);
	const esp_partition_t *p = 0;
	log_dbug(TAG,"looking for " ROMFS_MAGIC);
	while (pi != 0) {
		p = esp_partition_get(pi);
		uint8_t magic[8];
#if IDF_VERSION >= 50
		esp_partition_read_raw(p,0,(char *)magic,sizeof(magic));
#else
		spi_flash_read(p->address,magic,sizeof(magic));
#endif
		log_hex(TAG,magic,sizeof(magic),"partition %s",p->label);
		if (0 == memcmp(magic,ROMFS_MAGIC,sizeof(magic))) {
			log_info(TAG,"%s has ROMFS",p->label);
			break;
		}
		log_dbug(TAG,"%s has no ROMFS",p->label);
		pi = esp_partition_next(pi);
		p = 0;
	}
#if 0 // maybe needed in the future
	if (p == 0) {
		auto pi = esp_partition_find(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_ANY,0);
		log_dbug(TAG,"looking for " ROMFS_MAGIC);
		while (pi != 0) {
			p = esp_partition_get(pi);
			uint8_t magic[8];
			spi_flash_read(p->address,magic,sizeof(magic));
			log_hex(TAG,magic,sizeof(magic),"partition %s",p->label);
			if (0 == memcmp(magic,ROMFS_MAGIC,sizeof(magic))) {
				log_info(TAG,"%s has ROMFS",p->label);
				break;
			}
			log_dbug(TAG,"%s has no ROMFS",p->label);
			pi = esp_partition_next(pi);
			p = 0;
		}
	}
#endif
	if (p == 0) {
		log_dbug(TAG,"no romfs found");
		return 0;
	}
	log_dbug(TAG,"using partition %s",p->label);

	RomfsBaseAddr = p->address;
	RomfsSpace = p->size;
#ifdef ESP32
	spi_flash_mmap_handle_t handle;
	if (esp_err_t e = spi_flash_mmap(p->address,p->size,SPI_FLASH_MMAP_DATA,(const void**)&RomfsBaseAddr,&handle)) {
		log_error(TAG,"mmap failed: %s",esp_err_to_name(e));
		NumEntries = 0;
		return 0;
	}
	if (esp_err_t e = spi_flash_mmap(p->address,p->size,SPI_FLASH_MMAP_INST,(const void**)&RomfsBaseInstr,&handle))
		log_warn(TAG,"mmap instr failed: %s",esp_err_to_name(e));
	else
		log_info(TAG,"romfs execution mmaped to %p",RomfsBaseInstr);
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
		return 0;
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
			log_error(TAG,"Out of memory.");
			NumEntries = 0;
			if (Entries)
				free(Entries);
			Entries = 0;
			return 0;
		}
		Entries = n;
		memcpy(Entries+(NumEntries-1),&e,sizeof(RomEntry));
		flashrom += sizeof(RomEntry);
		spi_flash_read(flashrom,&e,sizeof(e));
	}
#endif
	log_info(TAG,"romfs at 0x%x: %u entries",RomfsBaseAddr,NumEntries);

#ifdef CONFIG_ROMFS_VFS
	for (auto &fd : FDs) {
		fd.idx = -1;
		fd.off = 0;
	}
	char mountpoint[strlen(p->label)+2];
	mountpoint[0] = '/';
	strcpy(mountpoint+1,p->label);
	esp_vfs_t vfs;
	bzero(&vfs,sizeof(vfs));
	vfs.flags = ESP_VFS_FLAG_DEFAULT;
	vfs.open = romfs_vfs_open;
	vfs.read = romfs_vfs_read;
	vfs.lseek = romfs_vfs_lseek;
	vfs.close = romfs_vfs_close;
	vfs.closedir = romfs_vfs_closedir;
	vfs.opendir = romfs_vfs_opendir;
	vfs.readdir = romfs_vfs_readdir;
	vfs.readdir_r = romfs_vfs_readdir_r;
	vfs.fstat = romfs_vfs_fstat;
	vfs.stat = romfs_vfs_stat;
#ifndef CONFIG_IDF_TARGET_ESP8266
	vfs.pread = romfs_vfs_pread;
#endif
	if (esp_err_t e = esp_vfs_register(mountpoint,&vfs,0)) {
		log_warn(TAG,"VFS register for patitions %s: %s",p->label,esp_err_to_name(e));
		return 0;
	} else {
		log_info(TAG,"romfs partition %s mounted on %s",p->label,mountpoint);
		return strdup(mountpoint+1);
	}
#else
	return 0;
#endif
}

#endif
