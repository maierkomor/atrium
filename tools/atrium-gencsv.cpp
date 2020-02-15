/*
 * Copyright 2019, Thomas Maier-Komor
 * LICENSE: GPLv3, see file LICENSE for details
 */

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <map>

#include "binformats.h"

const char *OutName = 0;
int In = STDIN_FILENO;
FILE *Out = stdout;


void fail_if(int e, const char *m, ...)
{
	if (e == 0)
		return;
	va_list val;
	va_start(val,m);
	vfprintf(stderr,m,val);
	va_end(val);
	exit(EXIT_FAILURE);
}

void parse_argv(char a)
{
	int v;
	switch (a) {
	default:
		fprintf(stderr,"invalid option -%c\n",a);
		exit(EXIT_FAILURE);
	case 'o':
		Out = fopen(optarg,"w");
		fail_if(Out == 0,"open of %s failed: %s\n",optarg,strerror(errno));
		break;
	case 'i':
		In = open(optarg,O_RDONLY);
		fail_if(In == -1,"open of %s failed: %s\n",optarg,strerror(errno));
		break;
	case 'h':
		fprintf(stderr,"options:\n"
				"-i <file>: input file (default read stdin)\n"
				"-o <file>: output file (default write to stdout)\n"
		       );
		break;
	}

}

int main(int argc, char *argv[])
{
	int i;
	while ((i = getopt(argc,argv,"hi:o:")) != -1)
		parse_argv(i);
	struct stat st;
	size_t s = 0;
	char *buf = 0;
	if (0 == fstat(In,&st)) {
		buf = (char *) malloc(st.st_size);
		int n = read(In,buf,st.st_size);
		fail_if(n < 0,"error reading: %s\n",strerror(errno));
		s = n;
	} else {
		size_t off = 0;
		int n;
		do {
			buf = (char *)realloc(buf,s+4096);
			n = read(In,buf+off,4096);
			fail_if(n < 0,"error reading input: %s\n",strerror(errno));
			off += n;
		} while (n != 0);
	}
	EnvLog el;
	fail_if(0 > el.fromMemory(buf,s),"error parsing log\n");
	for (size_t i = 0, j = el.data_size(); i != j; ++i) {
		const EnvData &e = el.data(i);
		uint32_t ts = e.timestamp();
		uint8_t mon = (ts>>24) & 0xf;
		uint8_t day = (ts>>19) & 0x1f;
		uint8_t h = (ts>>14) & 0x1f;
		uint8_t m = (ts>>7) & 0x7f;
		uint8_t s = ts & 0x7f;
		fprintf(Out,"%d.%d, %d:%02d:%02d, ",day,mon,h,m,s);
		if (e.has_temperature()) {
			int16_t t = e.temperature();
			fprintf(Out,"%4.1f",(float)t/10);
		}
		fprintf(Out,", ");
		if (e.has_humidity()) {
			uint8_t h = e.humidity();
			fprintf(Out,"%4.1f",(float)h/2);
		}
		fprintf(Out,"\n");
	}
	return 0;
}
