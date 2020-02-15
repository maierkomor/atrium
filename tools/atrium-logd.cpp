/*
 * Copyright 2019, Thomas Maier-Komor
 * LICENSE: GPLv3, see file LICENSE for details
 */

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <map>

#include "binformats.h"

#define PACKETSIZE (1<<16)

using namespace std;


static map<string,int> LogFiles;
static uint8_t Packet[PACKETSIZE];	/* maximum size of one packet */
static map<string,int> Logs;
static struct sockaddr_in From;
static int LogDir = -1, Sock = -1, Foreground = 0, Verbose = 2;
static uint16_t Port = 22488;
static const char *McastAddr = "224.0.0.88";


static void info(const char *f, ...)
{
	va_list val;
	va_start(val,f);
	vsyslog(LOG_INFO,f,val);
	va_end(val);
	if (Foreground) {
		va_start(val,f);
		vfprintf(stderr,f,val);
		va_end(val);
		fprintf(stderr,"\n");
	}
}


static void warning(const char *f, ...)
{
	va_list val;
	va_start(val,f);
	vsyslog(LOG_WARNING,f,val);
	va_end(val);
	if (Foreground) {
		fprintf(stderr,"warning: ");
		va_start(val,f);
		vfprintf(stderr,f,val);
		va_end(val);
		fprintf(stderr,"\n");
	}
}


static void error(const char *f, ...)
{
	va_list val;
	va_start(val,f);
	vsyslog(LOG_ERR,f,val);
	va_end(val);
	if (Foreground) {
		fprintf(stderr,"error: ");
		va_start(val,f);
		vfprintf(stderr,f,val);
		va_end(val);
		fprintf(stderr,"\n");
	}
}


static void fatal(const char *f, ...)
{
	va_list val;
	va_start(val,f);
	vsyslog(LOG_ERR,f,val);
	va_end(val);
	if (Foreground) {
		fprintf(stderr,"error: ");
		va_start(val,f);
		vfprintf(stderr,f,val);
		va_end(val);
		fprintf(stderr,"\n");
	}
	exit(EXIT_FAILURE);
}


static int setup_source()
{
	struct sockaddr_in MA;
	struct ip_mreq mreq;
	int on = 1, fl;

	Sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (-1 == Sock)
		fatal("cannot create socket: %s",strerror(errno));
	bzero(&MA,sizeof(MA));
	MA.sin_family = AF_INET;
	MA.sin_port = htons(Port);
#if defined __CYGWIN__
	MA.sin_addr.s_addr = htonl(INADDR_ANY);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	mreq.imr_multiaddr.s_addr = inet_addr(McastAddr);
#else
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	if (-1 == inet_pton(AF_INET, McastAddr, &MA.sin_addr))
		error("cannot set multicast address: %s",strerror(errno));
	memcpy(&mreq.imr_multiaddr.s_addr,(void *)&MA.sin_addr,sizeof(struct in_addr));
#endif
	if (-1 == bind(Sock,(struct sockaddr *)&MA,sizeof(MA))) {
		fatal("cannot bind socket: %s",strerror(errno));
	} else if (-1 == setsockopt(Sock,IPPROTO_IP,IP_ADD_MEMBERSHIP,&mreq,sizeof(mreq))) {
		fatal("could not join multicast group: %s",strerror(errno));
	} else
		return Sock;
	close(Sock);
	Sock = -1;
	return -1;
}


static int receive_stats()
{
	socklen_t len = sizeof(From);
	int num = recvfrom(Sock,Packet,sizeof(Packet),0,(struct sockaddr *)&From,&len);
	if (num == -1) {
		if (errno == EWOULDBLOCK)
			return 0;
		error("cannot read from socket: %s",strerror(errno));
		return 0;
	}
	//printf("received %d bytes\n",num);
	return num;
}


static void writelog(const string &node, const EnvLog &dl)
{
	if (node == "")
		return;
	auto i = LogFiles.find(node);
	if (i == LogFiles.end()) {
		string fname = node;
		fname += ".dat";
		int fd = openat(LogDir,fname.c_str(),O_CREAT|O_APPEND|O_RDWR,0666);
		if (fd == -1)
			error("unable to open logfile %s: %s",fname.c_str(),strerror(errno));
		else
			info("adding node %s",node.c_str());
		i = LogFiles.insert(make_pair(node,fd)).first;
	}
	if (-1 == i->second)
		return;
	int ps = dl.toMemory(Packet,sizeof(Packet));
	int n = write(i->second,Packet,ps);
	if (n == -1) {
		error("error writing to logfile %s: %s",i->first.c_str(),strerror(errno));
		close(i->second);
		i->second = -1;
	}
}


void usage(const char *m, int r)
{
	printf( "atrium-dlogd [option]\n"
		"Options:\n"
		"-h        : display this help screen and exit\n"
		"-f        : run in foreground\n"
		"-d <dir>  : write data logs to <dir>\n"
		//"-l <log>  : write log to file <log>\n"
		"-m <a:p>  : set multicast group address and port (optional) to <a:p>\n"
		"-p <port> : set multicast group port to <port>\n"
		"-v <lvl>  : set verbose mode to level <lvl>\n"
		"-V        : display version number and exit\n");
	if (m[0]) 
		printf("\n%s\n",m);
	exit(r);
}


void open_logdir(const char *path)
{
	DIR *d = opendir(path);
	if (0 == d) {
		error("unable to open logging directory %s: %s",path,strerror(errno));
		exit(EXIT_FAILURE);
	}
	LogDir = dirfd(d);
}


void parse_argv(char a)
{
	int v;
	switch (a) {
	default:
		fprintf(stderr,"invalid option -%c\n",a);
		exit(EXIT_FAILURE);
		break;
	case 'f':
		Foreground = 1;
		break;
	case 'h':
		usage("",EXIT_SUCCESS);
		break;
		/*
	case 'l':
		setLogfile(optarg);
		break;
		*/
	case 'm':
		McastAddr = optarg;
		break;
	case 'd':
		open_logdir(optarg);
		break;
	case 'p':
		Port = strtol(optarg,0,0);
		break;
	case 'v':
		if (1 == sscanf(optarg,"%d",&v)) {
			if (Verbose < 0 || Verbose > 5)
				error("value out of range for option -v");
			else
				Verbose = v;
			info("verbose set to %d",Verbose);
		} else
			error("invalid argument for option -v");
		break;
	case 'V':
		printf("atrium-logd version 0.1\n");
		exit(EXIT_SUCCESS);
	case ':':
		usage("missing argument",EXIT_FAILURE);
		break;
	case '?':
		usage("",EXIT_FAILURE);
		break;
	}
}


int main(int argc, char *argv[])
{
	int i;
	while ((i = getopt(argc,argv,"d:fhl:m:p:Vv:")) != -1)
		parse_argv(i);
	if (LogDir == -1)
		open_logdir("/var/atrium");
	openlog(argv[0],LOG_CONS|LOG_PERROR,LOG_USER);
	setup_source();
	if (Sock == -1)
		exit(EXIT_FAILURE);
	if (Foreground == 0) {
		if (fork()) 
			exit(EXIT_SUCCESS);
		if (fork()) 
			exit(EXIT_SUCCESS);
	}

	for (;;) {
		int n = receive_stats();
		if (n <= 0)
			continue;
		EnvLog dl;
		EnvData *ed = dl.add_data();
		stringstream str;
		int i = ed->fromMemory(Packet,n);
		if (i <= 0) {
			fprintf(stderr,"parser error %d\n",i);
			continue;
		}
		dl.toJSON(str);
		if (Foreground)
			printf("%s\n",str.str().c_str());
		if (!ed->has_node())
			continue;
		string node = ed->node();
		ed->clear_node();
		writelog(node,dl);

	}
	return 0;
}
