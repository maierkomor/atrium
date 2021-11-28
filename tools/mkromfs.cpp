/*
 *  Copyright (C) 2018-2020, Thomas Maier-Komor
 *  Tool for generating a romfs binary for flashing.
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

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>

#include <map>
#include <string>
#include <vector>

using namespace std;

/*
 ROMFS-16 layout:
- all entries are 4-bytes aligned
- strings are \0 terminated
- header entries always consist of 16 bytes:
	- offset	(2 bytes in little endian)
	- size		(2 bytes in little endian)
	- name		(max 11bytes+\0)
followed by 4 bytes of 0x00

 ROMFS-32 layout:
- all entries are 4-bytes aligned
- strings are \0 terminated
- header entries always consist of 32 bytes:
	- offset	(4 bytes in little endian)
	- size		(4 bytes in little endian)
	- name		(max 23bytes+\0)
followed by 4 bytes of 0x00
*/

struct Entry16
{
	public:
	uint16_t offset = 0;
	uint16_t size = 0;
	char name[12] = {0};
};


struct Entry32
{
	uint32_t offset = 0;
	uint32_t size = 0;
	char name[24] = {0};
};


class Entry
{
	public:
	Entry(unsigned offset, unsigned size, const char *name);
	Entry(const Entry &);
	~Entry()
	{
		if (e16)
			delete e16;
		if (e32)
			delete e32;
	}

	void *data()
	{ return e16 ? (void*)e16 : (void*)e32; }

	void increase_offset(unsigned);

	unsigned size()
	{ return e16 ? e16->size : e32->size; }

	unsigned offset()
	{ return e16 ? e16->offset : e32->offset; }

	const char *name()
	{ return e16 ? e16->name : e32->name; }

	private:
	Entry& operator = (const Entry &);

	Entry16 *e16;
	Entry32 *e32;
};


bool M32 = false;
const char *Srcdir = 0;
long RomfsAddr = 0;
string Outfile, Port;

vector<string> FileNames;
vector<string> FileData;
vector<Entry> Entries;


void check_exit(int e, const char *m, ...)
{
	if (e == 0)
		return;
	va_list val;
	va_start(val,m);
	vprintf(m,val);
	va_end(val);
	exit(EXIT_FAILURE);
}


Entry::Entry(unsigned offset, unsigned size, const char *name)
{
	if (M32) {
		e16 = 0;
		e32 = new Entry32;
		e32->offset = offset;
		e32->size = size;
		check_exit(strlen(name) > 23,"name '%s' is too long\n",name);
		strcpy(e32->name,name);
	} else {
		e32 = 0;
		e16 = new Entry16;
		check_exit(offset > UINT16_MAX,"offset 0x%x of %s too big\n",offset,name);
		e16->offset = offset;
		check_exit(size > UINT16_MAX,"size of %s too big\n",name);
		e16->size = size;
		check_exit(strlen(name) > 11,"name '%s' is too long\n",name);
		strcpy(e16->name,name);
	}
}


Entry::Entry(const Entry &e)
{
	if (e.e16) {
		e16 = new Entry16(*e.e16);
		e32 = 0;
	} else if (e.e32) {
		e32 = new Entry32(*e.e32);
		e16 = 0;
	}
}


void Entry::increase_offset(unsigned o)
{
	if (e16) {
		check_exit(e16->offset + o > UINT16_MAX, "offset of %s is too big",e16->name);
		e16->offset += o;
	} else {
		e32->offset += o;
	}
}


char *read_image(const char *fn)
{
	int fd = open(fn,O_RDONLY);
	check_exit(fd == -1,"unable to open file %s: %s\n",fn,strerror(errno));
	struct stat st;
	int r = fstat(fd,&st);
	check_exit(r == -1,"unable to stat file %s: %s\n",fn,strerror(errno));
	char *buf = (char *) malloc(st.st_size);
	check_exit(buf == 0,"out of memory");
	int n = read(fd,buf,st.st_size);
	check_exit(n == -1,"unable to read file %s: %s\n",fn,strerror(errno));
	close(fd);
	return buf;
}


void list_image(const char *fn)
{
	char *buf = read_image(fn);
	if (0 == memcmp(buf,"ROMFS16",8)) {
		Entry16 *e = (Entry16 *) (buf+8);
		while ((e->size != 0) || (e->offset != 0)) {
			printf("%5u@%04x %-12s\n",e->size,e->offset,e->name);
			++e;
		}
	} else if (0 == memcmp(buf,"ROMFS32",8)) {
		Entry32 *e = (Entry32 *) (buf+8);
		while ((e->size != 0) || (e->offset != 0)) {
			printf("%5u@%04x %-24s\n",e->size,e->offset,e->name);
			++e;
		}
	} else {
		printf("%s is no ROMFS image\n",fn);
	}
	free(buf);
}


void print_or_dump_file(const char *arg, void (*fun)(const char *,size_t))
{
	char *fn = strdup(arg);
	char *dn = strrchr(fn,'/');
	check_exit(dn == 0,"missing dump argument - use <image>/<dump>\n");
	*dn = 0;
	++dn;
	char *buf = read_image(fn);
	size_t off = 0, size = 0;
	if (0 == memcmp(buf,"ROMFS16",8)) {
		Entry16 *e = (Entry16 *) (buf+8);
		while ((e->size != 0) || (e->offset != 0)) {
			if (!strcmp(e->name,dn)) {
				off = e->offset;
				size = e->size;
				break;
			}
			++e;
		}
		if (off == 0)
			fprintf(stderr,"file not found");
	} else if (0 == memcmp(buf,"ROMFS32",8)) {
		Entry32 *e = (Entry32 *) (buf+8);
		while ((e->size != 0) || (e->offset != 0)) {
			if (!strcmp(e->name,dn)) {
				off = e->offset;
				size = e->size;
				break;
			}
			++e;
		}
		if (off == 0)
			fprintf(stderr,"file not found");
	} else {
		fprintf(stderr,"%s is no ROMFS image\n",fn);
	}
	if (size)
		fun(buf+off,size);
	free(buf);
}


void dump_data(const char *d, size_t l)
{
	while (l > 16) {
		printf("%02x %02x %02x %02x ",d[0],d[1],d[2],d[3]);
		d += 4;
		printf("%02x %02x %02x %02x ",d[0],d[1],d[2],d[3]);
		d += 4;
		printf(" %02x %02x %02x %02x",d[0],d[1],d[2],d[3]);
		d += 4;
		printf(" %02x %02x %02x %02x\n",d[0],d[1],d[2],d[3]);
		d += 4;
		l -= 16;
	}
	if (l > 4) {
		printf("%02x %02x %02x %02x",d[0],d[1],d[2],d[3]);
		d += 4;
		l -= 4;
	}
	if (l > 4) {
		printf(" %02x %02x %02x %02x",d[0],d[1],d[2],d[3]);
		d += 4;
		l -= 4;
	}
	if (l > 4) {
		printf("  %02x %02x %02x %02x",d[0],d[1],d[2],d[3]);
		d += 4;
		l -= 4;
	}
	while (l) {
		printf(" %02x",d[0]);
		++d;
		--l;
	}
	printf("\n");
}


void dump_file(const char *arg)
{
	print_or_dump_file(arg,dump_data);
}


void print_data(const char *d, size_t l)
{
	write(STDOUT_FILENO,d,l);

}


void print_file(const char *arg)
{
	print_or_dump_file(arg,print_data);
}


void set_port(const char *p)
{
	Port = p;
}


void set_flash_addr(const char *p)
{
	long l = strtol(p,0,0);
	if (l == 0)
		printf("invalid argument '%s' for option -a\n",p);
	else
		RomfsAddr = l;
}


void usage()
{
	printf("synopsis: mkrom [options] <files>\n"
			"valid options are:\n"
			"-h           : print this help\n"
			"-c <dir>     : cd to <dir> for reading files\n"
			"-s <dir>     : include all files from directory <dir>\n"
			"-o <filename>: write output to file <filename>\n"
			"-16          : use 16 bit size/offset entries (default)\n"
			"-32          : use 32 bit size/offset entries\n"
			"-a <addr>    : flash image to address <addr>\n"
			"-P <port>    : use port <port> to flash romfs\n"
			"-l <binfile> : list contents of binary ROM image file\n"
			"-d <img/file>: dump file <file> of ROM image <img>\n"
			"-p <img/file>: print file <file> of ROM image <img>\n"
	      );
	exit(EXIT_FAILURE);
}


static void addSourceDir(const char *dn)
{
	DIR *d = opendir(dn);
	check_exit(d == 0,"unable to open directory %s: %s\n",dn,strerror(errno));
	printf("adding files in directory %s\n",dn);
	while (struct dirent *e = readdir(d)) {
		string tmp = dn;
		if (tmp.back() != '/')
			tmp += '/';
		tmp += e->d_name;
		if (DT_REG == e->d_type)
			FileNames.push_back(tmp);
		else if (e->d_name[0] == '.');
		else if (DT_DIR == e->d_type)
			addSourceDir(tmp.c_str());
	}
	closedir(d);
}


int main(int argc, char *argv[])
{
	char pwd[PATH_MAX] = "";
	if (argc == 1)
		usage();
	for (int i = 1; i < argc; ++i) {
		if (0 == strcmp("-o",argv[i])) {
			Outfile = argv[++i];
		} else if (0 == strncmp("-o",argv[i],2)) {
			Outfile = argv[i]+2;
		} else if (0 == strcmp("-c",argv[i])) {
			getcwd(pwd,sizeof(pwd));
			check_exit(-1 == chdir(argv[++i]), "cannot change to directory: %s\n",strerror(errno));
		} else if (0 == strncmp("-c",argv[i],2)) {
			getcwd(pwd,sizeof(pwd));
			check_exit(-1 == chdir(argv[i]+2), "cannot change to directory: %s\n",strerror(errno));
		} else if (0 == strcmp("-s",argv[i])) {
			addSourceDir(argv[++i]);
		} else if (0 == strncmp("-s",argv[i],2)) {
			addSourceDir(argv[i]+2);
		} else if (0 == strcmp("-16",argv[i])) {
			M32 = false;
		} else if (0 == strcmp("-32",argv[i])) {
			M32 = true;
		} else if (0 == strcmp("-h",argv[i])) {
			usage();
		} else if (0 == strcmp("-l",argv[i])) {
			list_image(argv[++i]);
		} else if (0 == strncmp("-l",argv[i],2)) {
			list_image(argv[i]+2);
		} else if (0 == strcmp("-a",argv[i])) {
			set_flash_addr(argv[++i]);
		} else if (0 == strncmp("-a",argv[i],2)) {
			set_flash_addr(argv[i]+2);
		} else if (0 == strcmp("-P",argv[i])) {
			set_port(argv[++i]);
		} else if (0 == strncmp("-P",argv[i],2)) {
			set_port(argv[i]+2);
		} else if (0 == strcmp("-p",argv[i])) {
			print_file(argv[++i]);
		} else if (0 == strncmp("-p",argv[i],2)) {
			print_file(argv[i]+2);
		} else if (0 == strcmp("-d",argv[i])) {
			dump_file(argv[++i]);
		} else if (0 == strncmp("-d",argv[i],2)) {
			dump_file(argv[i]+2);
		} else {
			struct stat st;
			if (0 == stat(argv[i],&st)) {
				if ((st.st_mode & S_IFMT) == S_IFDIR) 
					addSourceDir(argv[i]);
				else
					FileNames.push_back(argv[i]);
			}
		}
	}
	size_t total = M32 ? 16 : 12;	// 8 bytes magic + 4 or 8 bytes separator between header entries and data
	int n = FileNames.size();
	if (n == 0)
		return 0;
	map<string,string> entries;
	for (int i = 0; i < n; ++i) {
		const char *fname = FileNames[i].c_str();
		const char *ename = strrchr(fname,'/');
		if (ename)
			++ename;
		else
			ename = fname;
		if (!entries.insert(make_pair(ename,fname)).second) {
			printf("warning: ignoring duplicate entry name '%s' (file %s)\n",ename,fname);
			continue;
		}
		check_exit(strlen(ename)>(M32 ? 15 : 11),"filename '%s' is too long\n",ename);
	}
	unsigned idx = 0;
	for (auto x : entries) {
		const char *ename = x.first.c_str();
		const char *fname = x.second.c_str();
		int fd = open(fname,O_RDONLY);
		check_exit(fd<0,"error opening %s: %s\n",fname,strerror(errno));
		struct stat st;
		check_exit(fstat(fd,&st) < 0,"unable to stat file %s: %s\n",fname,strerror(errno));
		check_exit(st.st_size > UINT16_MAX,"file %s is too big\n",fname);
		Entry e(total,st.st_size,ename);
		size_t len = st.st_size;
		if (len&3) {
			len &= ~3;
			len += 4;
		}
		total += len;
		FileData.push_back(string());
		string &str = FileData.back();
		str.resize(len);
		int r = read(fd,(void*)str.data(),st.st_size);
		check_exit(r != st.st_size,"file %s: expected %d bytes, got %d\n",fname,st.st_size,r);
		close(fd);
		Entries.push_back(e);
	}
	if (pwd[0])
		chdir(pwd);
	size_t header = (M32 ? 32 : 16) * n;
	size_t off = header;
	char *rom = (char *)malloc(header+total);
	memcpy(rom,M32 ? "ROMFS32" : "ROMFS16",8);
	char *at = rom + 8;
	for (int i = 0; i < entries.size(); ++i) {
		Entries[i].increase_offset(header);
		size_t s = M32 ? sizeof(Entry32) : sizeof(Entry16);
		memcpy(at,Entries[i].data(),s);
		at += s;
		printf("%5u@%04x: %s\n",Entries[i].size(),Entries[i].offset(),Entries[i].name());
	}
	bzero(at,M32?8:4);
	at += M32 ? 8 : 4;
	for (int i = 0; i < n; ++i) {
		size_t s = FileData[i].size();
		assert((s & 3) == 0);
		memcpy(at,FileData[i].data(),FileData[i].size());
		at += s;
	}
	int out;
	bool temp = Outfile.empty();
	if (temp) {
		Outfile = "mkromfs-temp-output.XXXXXX";
		out = mkstemp((char*)Outfile.c_str());
	} else {
		out = open(Outfile.c_str(),O_RDWR|O_CREAT|O_TRUNC,0666);
	}
	check_exit(out < 0,"unable to open output file %s: %s\n",Outfile,strerror(errno));
	printf("writing %ld bytes to %s\n",header+total,Outfile.c_str());
	int w = write(out,rom,header+total);
	check_exit(w < 0,"error writing: %s\n",strerror(errno));
	check_exit(w != (header+total),"writing truncated: expected %u got %u\n",header+total,w);

	if (RomfsAddr) {
		char *cmd;
		asprintf(&cmd,"%s/components/esptool_py/esptool/esptool.py --port %s write_flash 0x%lx %s"
			, getenv("IDF_PATH")
			, Port.empty() ? "/dev/ttyUSB0" : Port.c_str()
			, RomfsAddr
			, Outfile.c_str()
			);
		system(cmd);
		free(cmd);
	}
	if (temp)
		unlink(Outfile.c_str());
	close(out);
	return 0;
}
