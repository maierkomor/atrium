#include "binformats.h"
#include "strstream.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sstream>

using namespace std;

int main(int argc, char *argv[])
{
	assert(argc == 2);
	int fd = open(argv[1],O_RDONLY);
	assert(fd != -1);
	struct stat st;
	int r = fstat(fd,&st);
	assert(r == 0);
	char *buf = (char *)malloc(st.st_size);
	assert(buf);
	r = read(fd,buf,st.st_size);
	assert(r == st.st_size);

	NodeConfig c;
	c.fromMemory(buf,st.st_size);
	stringstream ss;
	c.toASCII(ss);
	printf("%s\n",ss.str().c_str());
	free(buf);
	return 0;
}
