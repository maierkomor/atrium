/*
 *  Copyright (C) 2020-2021, Thomas Maier-Komor
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

#include "binformats.h"
#include "strstream.h"
#include "version.h"

#if defined __MINGW32__ || defined __MINGW64__
#include <windows.h>
#include <wincrypt.h>
#define setenv(a,b,c) SetEnvironmentVariable(a,b)
#else
#include <editline/readline.h>
#include <editline/history.h>
#include <md5.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <estring.h>
#include <sys/stat.h>
#include <unistd.h>

#include <set>
#include <utility>

using namespace std;

HardwareConfig HwCfg;
NodeConfig NodeCfg;

bool Software = false;
char *HwFilename = 0, *SwFilename = 0;
char *Port;
const char *Tmpdir;
char *IdfPath;
int NvsAddr = -1;

/* future work for ESP8266 verification
const char *PadNames[] = {
	"mtdi",
	"mtck",
	"mtms",
	"mtdo",
	"u0rxd",
	"u0txd",
	"sdio_clk",
	"sdio_data_0",
	"sdio_data_1",
	"sdio_data_2",
	"sdio_data_3",
	"sdio_cmd",
	"gpio0",
	"gpio2",
	"gpio4",
	"gpio5",
	"xpd_dcdc",
};

const char *Pad0Funcs[] = {
	"mtdi", "i2si_data", "hspi_miso", "gpio12", "u0dtr"
};

const char *Pad1Funcs[] = {
	"mtck", "i2si_bck", "hspi_mosi", "gpio13", "u0cts"
};

const char *Pad2Funcs[] = {
	"mtms", "i2si_ws", "hspi_clk", "gpio14", "u0dsr"
};

const char *Pad3Funcs[] = {
	"mtdo", "i2so_bck", "hspi_cs", "gpio15", "u0rts"
};

typedef enum { unset = 0, mtdi = 1, i2si_data, hspi_miso, gpio12, u0dtr } gpio12_funcs_t;
typedef enum { unset = 0, mtck = 1, i2si_bck, hspi_mosi, gpio13, u0cts } gpio13_funcs_t;
typedef enum { unset = 0, mtms = 1, i2si_ws, hspi_clk, gpio14, u0dsr } gpio14_funcs_t;
typedef enum { unset = 0, mtdo = 1, i2so_bck, hspi_cs, gpio15, u0rts } gpio15_funcs_t;
*/


const char *FnTypes[] = {
	"<",
	"<=",
	"==",
	"random",
	"mqtt_publish",
	"influx_send",
	0
};


#if defined __MINGW32__ || defined __MINGW64__
/*
int asprintf(char **pstr, const char *f, ...) 
{
	va_list val;
	va_start(val,f);
	int n = vsnprintf(0,0,f,val);
	*pstr = (char *)malloc(n+1);
	vsnprintf(*pstr,n+1,f,val);
	va_val(val);
	return n;
}

int vasprintf(char **pstr, const char *f, va_list val) 
{
	int n = vsnprintf(0,0,f,val);
	*pstr = (char *)malloc(n+1);
	vsnprintf(*pstr,n+1,f,val);
	return n;
}
*/

char *readline(const char *p)
{
	char buf[256];
	write(1,p,strlen(p));
	scanf("%255s",&buf);
	if (buf[0] == 0)
		return 0;
	return strdup(buf);
}

void add_history(const char *e)
{

}
#endif


int set_filename(const char *arg)
{
	if (arg == 0) {
		const char *f = Software ? SwFilename : HwFilename;
		printf("%s\n", f ? f : "<unset>");
	} else if (Software) {
		if (SwFilename)
			free(SwFilename);
		SwFilename = strdup(arg);
	} else {
		if (HwFilename)
			free(HwFilename);
		HwFilename = strdup(arg);
	}
	return 0;
}


int read_config(const char *arg)
{
	const char *fn = arg;
	if (fn == 0) { 
		if (Software)
			fn = SwFilename;
		else
			fn = HwFilename;
		if (fn == 0) {
			printf("missing argument\n");
			return 1;
		}
	}
	int r = -1, n;
	int fd = open(fn,O_RDONLY);
	if (fd == -1) {
		printf("error opening %s: %s\n",arg,strerror(errno));
		return 1;
	}
	uint8_t *buf = 0;
	struct stat st;
	if (-1 == fstat(fd,&st)) {
		printf("error stating %s: %s\n",arg,strerror(errno));
		goto done;
	}
	buf = (uint8_t *) malloc(st.st_size);
	if (buf == 0)
		goto done;
	n = read(fd,buf,st.st_size);
	if (n < 0) {
		printf("error reading %s: %s\n",arg,strerror(errno));
		goto done;
	}
	if (n != st.st_size)
		printf("short read: got %d instead of %zd\n",n,st.st_size);
	if (n > 4) {
		uint8_t hwmagic[] = {0x5,0xcb,0xed,0x54,0xae};
		uint8_t swmagic[] = {0x5,0xc0,0xed,0x54,0xae};
		if (0 == memcmp(buf,hwmagic,sizeof(hwmagic))) {
			printf("'%s' looks like a hardware config file\n",arg);
			r = HwCfg.fromMemory(buf,n);
			Software = false;
		} else if (0 == memcmp(buf,swmagic,sizeof(swmagic))) {
			printf("'%s' looks like a software config file\n",arg);
			r = NodeCfg.fromMemory(buf,n);
			Software = true;
		} else {
			printf("no valid magic signature found\n");
			if (Software)
				r = NodeCfg.fromMemory(buf,n);
			else
				r = HwCfg.fromMemory(buf,n);
		}
	}
	if (r < 0) {
		printf("parsing had error %d\n",r);
	} else if (fn == arg) {
		if (Software) {
			free(SwFilename);
			SwFilename = strdup(fn);
		} else {
			free(HwFilename);
			HwFilename = strdup(fn);
		}
	}
done:	
	if (buf)
		free(buf);
	close(fd);
	return r < 0;
}


int write_config(const char *arg)
{
	const char *fn = arg;
	size_t s;
	if (Software) {
		if (fn == 0)
			fn = SwFilename;
		s = NodeCfg.calcSize();
	} else {
		if (fn == 0)
			fn = HwFilename;
		s = HwCfg.calcSize();
	}
	if (fn == 0) {
		printf("missing filename\n");
		return 1;
	}
	uint8_t *buf = (uint8_t *) malloc(s);
	size_t n;
	if (Software)
		n = NodeCfg.toMemory(buf,s);
	else
		n = HwCfg.toMemory(buf,s);
	assert(n == s);
	int fd = open(fn,O_WRONLY|O_TRUNC|O_CREAT,0666);
	if (fd == -1) {
		printf("error creating %s: %s\n",arg,strerror(errno));
		return 1;
	}
	int w = write(fd,buf,s);
	if (w < 0)
		printf("error reading %s: %s\n",arg,strerror(errno));
	else if (w != n)
		printf("short write: wrote %d instead of %zd\n",w,n);
	if (arg) {
		if (Software) {
			free(SwFilename);
			SwFilename = strdup(arg);
		} else {
			free(HwFilename);
			HwFilename = strdup(arg);
		}
	}
	free(buf);
	close(fd);
	return 0;
}


int clear_config(const char *arg)
{
	if (arg) {
		if (Software)
			return NodeCfg.setByName(arg,0) < 0;
		else
			return HwCfg.setByName(arg,0) < 0;
	}
	if (Software) {
		NodeCfg.clear();
		NodeCfg.set_magic(0xae54edc0);
	} else {
		HwCfg.clear();
		HwCfg.set_magic(0xae54edcb);
	}
	return 0;
}


int verify_sw()
{
	return 0;
}


int check_ident(const char *id)
{
	char c = *id;
	if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')))
		++id;
	else
		return 1;
	c = *id;
	while (c) {
		if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_'))
			++id;
		else
			return 1;
		c = *id;
	}
	return 0;
}


int verify_hw()
{
	int ret = 0;
	set<int8_t> gpios;
	int n = 0;
	for (auto c: HwCfg.uart()) {
		if (c.has_tx_gpio() && !gpios.insert(c.tx_gpio()).second) {
			printf("duplicate use of gpio %d\n",c.tx_gpio());
			ret = 1;
		}
		if (c.has_rx_gpio() && !gpios.insert(c.rx_gpio()).second) {
			printf("duplicate use of gpio %d\n",c.rx_gpio());
			ret = 1;
		}
		if (c.has_cts_gpio() && !gpios.insert(c.cts_gpio()).second) {
			printf("duplicate use of gpio %d\n",c.cts_gpio());
			ret = 1;
		}
		if (c.has_rts_gpio() && !gpios.insert(c.rts_gpio()).second) {
			printf("duplicate use of gpio %d\n",c.rts_gpio());
			ret = 1;
		}
	}
	for (auto g: HwCfg.gpio()) {
		int gpio = g.gpio();
		if (g.name().empty() && (gpio == -1)) {
			// empty config
		} else if (check_ident(g.name().c_str())) {
			printf("gpio config #%d has invalid name\n",n);
			ret = 1;
		} else if (gpio == -1) {
			printf("gpio config #%d has no gpio\n",n);
			ret = 1;
		} else if (!gpios.insert(gpio).second) {
			printf("gpio %d: duplicate use in relay #%d\n",gpio,n);
			ret = 1;
		}
		++n;
	}
	n = 0;
	for (auto r: HwCfg.relay()) {
		int gpio = r.gpio();
		if (r.name().empty() && (gpio == -1)) {
			// empty config
		} else if (check_ident(r.name().c_str())) {
			printf("relay #%d has invalid name\n",n);
			ret = 1;
		} else if (gpio == -1) {
			printf("relay #%d has no gpio\n",n);
			ret = 1;
		} else if (!gpios.insert(gpio).second) {
			printf("gpio %d: duplicate use in relay #%d\n",gpio,n);
			ret = 1;
		}
		++n;
	}
	n = 0;
	for (auto l: HwCfg.led()) {
		int gpio = l.gpio();
		if (l.name().empty() && (gpio == -1)) {
			// empty config
		} else if (check_ident(l.name().c_str())) {
			printf("led config #%d has invalid name\n",n);
			ret = 1;
		} else if (gpio == -1) {
			printf("led config #%d has no gpio\n",n);
			ret = 1;
		} else if (!gpios.insert(gpio).second) {
			printf("gpio %d: duplicate use in led config #%d\n",gpio,n);
			ret = 1;
		}
		++n;
	}
	n = 0;
	for (auto b: HwCfg.button()) {
		if (b.name().empty()) {
			printf("button #%d has no name\n",n);
			ret = 1;
		} else if (check_ident(b.name().c_str())) {
			printf("button #%d has invalid name\n",n);
			ret = 1;
		}
		int gpio = b.gpio();
		if (gpio == -1) {
			printf("button #%d has no gpio\n",n);
			ret = 1;
		} else if (!gpios.insert(gpio).second) {
			printf("gpio %d: duplicate use in button #%d\n",gpio,n);
			ret = 1;
		}
		++n;
	}
	if (HwCfg.has_max7219()) {
		const Max7219Config &c = HwCfg.max7219();
		int8_t clk = c.clk();
		int8_t dout = c.dout();
		int8_t cs = c.cs();
		if ((clk != -1) && !gpios.insert(clk).second) {
			printf("gpio %d: duplicate use in max7129\n",clk);
			ret = 1;
		}
		if ((cs != -1) && !gpios.insert(clk).second) {
			printf("gpio %d: duplicate use in max7129\n",cs);
			ret = 1;
		}
		if ((dout != -1) && !gpios.insert(clk).second) {
			printf("gpio %d: duplicate use in max7129\n",dout);
			ret = 1;
		}
		auto digits = c.digits();
		if ((clk != -1) && (cs != -1) && (dout != -1) && (digits != 0)) {
			// config OK
		} else if ((clk == -1) && (cs == -1) && (dout == -1) && (digits == 0)) {
			// config empty
		} else {
			printf("max7219 has incomplete config\n");
			ret = 1;
		}
	}
	if (HwCfg.has_tlc5947()) {
		const Tlc5947Config &c = HwCfg.tlc5947();
		int8_t sin = c.sin();
		int8_t sclk = c.sclk();
		int8_t xlat = c.xlat();
		int8_t blank = c.blank();
		if ((sin != -1) && !gpios.insert(sin).second) {
			printf("gpio %d: duplicate use in tlc5947\n",sin);
			ret = 1;
		}
		if ((sclk != -1) && !gpios.insert(sclk).second) {
			printf("gpio %d: duplicate use in tlc5947\n",sclk);
			ret = 1;
		}
		if ((xlat != -1) && !gpios.insert(xlat).second) {
			printf("gpio %d: duplicate use in tlc5947\n",xlat);
			ret = 1;
		}
		if ((blank != -1) && !gpios.insert(blank).second) {
			printf("gpio %d: duplicate use in tlc5947\n",blank);
			ret = 1;
		}
		auto ntlc = c.ntlc();
		if ((sin != -1) && (sclk != -1) && (xlat != -1) && (blank != -1) && (ntlc != 0)) {
			// config OK
		} else if ((sin == -1) && (sclk == -1) && (xlat == -1) && (blank == -1) && (ntlc == 0)) {
			// config empty
		} else {
			printf("tlc5947 has incomplete config\n");
			ret = 1;
		}
	}
	if (HwCfg.has_ws2812b()) {
		const Ws2812bConfig &c = HwCfg.ws2812b();
		int8_t gpio = c.gpio();
		if ((gpio != -1) && !gpios.insert(gpio).second) {
			printf("gpio %d: duplicate use in ws2812b\n",gpio);
			ret = 1;
		}
		auto nleds = c.nleds();
		if ((gpio != -1) && (nleds != 0)) {
			// config OK
		} else if ((gpio == -1) && (nleds == 0)) {
			// config empty
		} else {
			printf("ws2812b has incomplete config\n");
			ret = 1;
		}
	}
	for (const I2CConfig &c : HwCfg.i2c()) {
		int8_t sda = c.sda();
		if ((sda != -1) && !gpios.insert(sda).second) {
			printf("gpio %d: duplicate use in i2c\n",sda);
			ret = 1;
		}
		int8_t scl = c.sda();
		if ((scl != -1) && !gpios.insert(scl).second) {
			printf("gpio %d: duplicate use in i2c\n",scl);
			ret = 1;
		}
		if ((sda != -1) && (scl != -1)) {
			// config OK
		} else if ((sda == -1) && (scl == -1)) {
			// config empty
		} else {
			printf("bme280 has incomplete config\n");
			ret = 1;
		}
	}
	if (HwCfg.has_dht()) {
		const DhtConfig &c = HwCfg.dht();
		int8_t gpio = c.gpio();
		if ((gpio != -1) && !gpios.insert(gpio).second) {
			printf("gpio %d: duplicate use in dht\n",gpio);
			ret = 1;
		}
		dht_model_t m = c.model();
		if ((gpio != -1) && (m != DHT_NONE)) {
			// config OK
		} else if ((gpio == -1) && (m == DHT_NONE)) {
			// config empty
		} else {
			printf("dht has incomplete config\n");
			ret = 1;
		}
	}
	if (HwCfg.has_hcsr04()) {
		const HcSr04Config &c = HwCfg.hcsr04();
		int8_t trigger = c.trigger();
		int8_t echo = c.echo();
		if ((trigger != -1) && (echo != -1)) {
			// config OK
		} else if ((trigger == -1) && (echo == -1)) {
			// config empty
		}

	}
	return ret;
}


int set_password(const char *arg)
{
	if (!strcmp(arg,"-c")) {
		NodeCfg.clear_pass_hash();
		return 0;
	}
#if defined __MINGW32__ || defined __MINGW64__
	HCRYPTPROV hCryptProv;
	HCRYPTHASH hHash;
	CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, 0);
	CryptCreateHash(hCryptProv, CALG_MD5, 0, 0, &hHash);
	if(hHash) 
		CryptDestroyHash(hHash);
	if(hCryptProv) 
		CryptReleaseContext(hCryptProv,0);
#else
	MD5Context ctx;
	MD5Init(&ctx);
	MD5Update(&ctx,(uint8_t*)arg,strlen(arg));
	uint8_t md5[16];
	MD5Final(md5,&ctx);
	NodeCfg.set_pass_hash(md5,sizeof(md5));
#endif
	return 0;
}


int verify_config(const char *arg)
{
	if (arg == 0) {
		if (Software) {
			return verify_sw();
		} else {
			return verify_hw();
		}
	} else if (!strcmp("hw",arg)) {
		return verify_hw();
	} else if (!strcmp("sw",arg)) {
		return verify_sw();
	} else {
		printf("invalid argument %s",arg);
		return 1;
	}
}


int to_hw(const char *arg)
{
	Software = false;
	return 0;
}


int to_sw(const char *arg)
{
	Software = true;
	return 0;
}


int show_config(const char *arg)
{
	estring s;
	strstream ss(s);
	if (Software)
		NodeCfg.toASCII(ss);
	else
		HwCfg.toASCII(ss);
	write(STDOUT_FILENO,s.data(),s.size());
	write(STDOUT_FILENO,"\n",1);
	return 0;
}


int json_config(const char *arg)
{
	estring s;
	strstream ss(s);
	if (Software)
		NodeCfg.toJSON(ss);
	else
		HwCfg.toJSON(ss);
	write(STDOUT_FILENO,s.data(),s.size());
	write(STDOUT_FILENO,"\n",1);
	return 0;
}


int set_config(const char *key)
{
	if (key == 0)
		return 1;
	// The rest of the line is the argument.
	// e.g. system.board_name may have spaces
	const char *value = strtok(0,"");
	if (Software)
		return NodeCfg.setByName(key,value) < 0;
	else
		return HwCfg.setByName(key,value) < 0;
}


int add_field(const char *f)
{
	if (f == 0) {
		printf("missing argument");
		return 1;
	}
	size_t l = strlen(f);
	char name[l+4];
	strcpy(name,f);
	strcat(name,"[+]");
	if (Software)
		return NodeCfg.setByName(name,0) < 0;
	else
		return HwCfg.setByName(name,0) < 0;
}


int genpart(const char *pname)
{
	const char header[] = "key,type,encoding,value\ncfg,namespace,,\n";
	char *csvfile = 0, *swfile = 0, *hwfile = 0, *buf = 0, *cmd = 0;
	int r = 1, hwfd, swfd, fd, n;
	estring sws, hws;

	if (0 == IdfPath) {
		printf("error: IDF_PATH is not set\n");
		goto done;
	}
#if defined __MINGW32__ || defined __MINGW64__
	hwfd = open("hw.cfg.tmp",O_CREAT|O_RDWR|O_TRUNC,0666);
#else
	asprintf(&hwfile,"%s/hw-XXXXXX.cfg",Tmpdir);
	hwfd = mkstemps(hwfile,4);
	if (-1 == hwfd) {
		printf("unable to create hardware configuration file: %s\n",strerror(errno));
		goto done;
	}
	HwCfg.toString(hws);
#endif
	if (-1 == write(hwfd,hws.data(),hws.size())) {
		printf("unable to write hardware configuration file: %s\n",strerror(errno));
		goto done;
	}
	close(hwfd);

#if defined __MINGW32__ || defined __MINGW64__
	swfd = open("sw.cfg.tmp",O_CREAT|O_RDWR|O_TRUNC,0666);
#else
	asprintf(&swfile,"%s/node-XXXXXX.cfg",Tmpdir);
	swfd = mkstemps(swfile,4);
	if (-1 == swfd) {
		printf("unable to create software configuration file: %s\n",strerror(errno));
		goto done;
	}
#endif

	NodeCfg.toString(sws);
	if (-1 == write(swfd,sws.data(),sws.size())) {
		printf("unable to write software configuration file: %s\n",strerror(errno));
		goto done;
	}
	close(swfd);

#if defined __MINGW32__ || defined __MINGW64__
	fd = open("part.csv.tmp",O_CREAT|O_RDWR|O_TRUNC,0666);
#else
	asprintf(&csvfile,"%s/nvs_part-XXXXXX.csv",Tmpdir);
	fd = mkstemps(csvfile,4);
	if (-1 == fd) {
		printf("unable to create partition csv file: %s\n",strerror(errno));
		goto done;
	}
#endif
	if (-1 == write(fd,header,sizeof(header)-1)) {
		printf("error writing csv header: %s\n",strerror(errno));
		goto done;
	}
	
	n = asprintf(&buf,"hw.cfg,file,binary,%s\nnode.cfg,file,binary,%s\n",hwfile,swfile);
	if (-1 == write(fd,buf,n)) {
		printf("error writing csv body: %s\n",strerror(errno));
		goto done;
	}
	close(fd);

	asprintf( &cmd
		, "%s/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py --input=%s --output=%s --size 16384"
		, IdfPath
		, csvfile
		, pname);
	printf("executing %s\n",cmd);
	r = system(cmd);
done:
	if (csvfile) {
		unlink(csvfile);
		free(csvfile);
	}
	if (hwfile) {
		unlink(hwfile);
		free(hwfile);
	}
	if (swfile) {
		unlink(swfile);
		free(swfile);
	}
	if (cmd)
		free(cmd);
	if (buf)
		free(buf);

	if (0 != r)
		printf("error crating nvs partition\n");
	return r;
}


int flashnvs(const char *pfile)
{
	char *cmd;
	if (0 == IdfPath) {
		printf("error: IDF_PATH is not set\n");
		return 1;
	}
	if (NvsAddr > 0)
		asprintf(&cmd,"%s/components/esptool_py/esptool/esptool.py --port %s write_flash 0x%x %s"
			, IdfPath
			, Port ? Port : "/dev/ttyUSB0"
			, NvsAddr
			, pfile
			);
	else
		asprintf(&cmd,"%s/components/partition_table/parttool.py --port %s --partition-name nvs write_partition --input=%s"
			, IdfPath
			, Port ? Port : "/dev/ttyUSB0"
			, pfile
			);
	printf("executing %s\n",cmd);
	if (0 != system(cmd)) {
		free(cmd);
		return 1;
	}
	free(cmd);
	return 0;
}


int updatenvs(const char *ignored)
{
	char *pfile;
	asprintf(&pfile,"%s/nvs-part-XXXXXX",Tmpdir);
	mkstemp(pfile);
	printf("creating %s\n",pfile);
	if (genpart(pfile)) {
		free(pfile);
		return 1;
	}
	printf("writing %s\n",pfile);
	int r = flashnvs(pfile);
	unlink(pfile);
	free(pfile);
	return r;
}


int setport(const char *name)
{
	if (name == 0) {
		printf("port is %s\n", Port ? Port : "<not set>");
		return 0;
	}
	struct stat st;
	if (-1 == stat(name,&st)) {
		printf("error accessing %s: %s\n",name,strerror(errno));
		return 1;
	}
	free(Port);
	Port = strdup(name);
	return 0;
}


int isDir(const char *d)
{
	if (d == 0)
		return 1;
	struct stat st;
	if (-1 == stat(d,&st)) {
		printf("Unable to stat directory %s: %s\n",d,strerror(errno));
		return 1;
	}
	if ((st.st_mode & S_IFMT) != S_IFDIR) {
		printf("%s is not a directory\n",d);
		return 1;
	}
	if (-1 == access(d,R_OK|X_OK)) {
		printf("Unable to access directory %s: %s\n",d,strerror(errno));
		return 1;
	}
	return 0;
}


int set_idf(const char *path)
{
	if (path == 0) {
		printf("IDF_PATH=%s\n",IdfPath);
		return 0;
	}
	if (isDir(path) != 0)
		return 1;
	if (IdfPath)
		free(IdfPath);
	IdfPath = strdup(path);
	setenv("IDF_PATH",IdfPath,1);
	return 0;
}


int term(const char *)
{
	exit(0);
}


int nvsaddr(const char *a)
{
	char *e;
	if (a == 0) {
		printf("0x%x\n",NvsAddr);
		return 0;
	}
	long l = strtol(a,&e,0);
	if ((l <= 0) || (e == 0) || (*e != 0)) {
		printf("invalid address argument.\n");
		return 1;
	}
	NvsAddr = l;
	return 0;
}


int function(const char *a)
{
	if (a == 0) {
		printf("function <name> <type> [<param> ...]\n"
			"valid function types are:\n");
		const char **f = FnTypes;
		while (*f) 
			printf("\t%s\n",*f++);
		return 1;
	}
	for (auto &s : NodeCfg.functions()) {
		if (s.name() == a) {
			printf("signal already exists\n");
			return 1;
		}
	}
	return 0;
}

int signal_add(const char *a)
{
	if (a == 0) {
		printf("signal <name> [int|float] [<initial value>]\n");
		return 1;
	}
	char *t = strtok(0," \t");
	if (t == 0) {
		printf("need signal type\n");
		return 1;
	}
	char *iv = strtok(0," \t");
	sigtype_t type = st_invalid;
	if (0 == strcmp(t,"int")) {
		type = st_int;
		if (iv) {
			char *e;
			long long ll = strtoll(iv,&e,0);
			if (*e != 0) {
				printf("intial value must be an int64 value\n");
				return 1;
			}
		}
	} else if (0 == strcmp(t,"float")) {
		type = st_float;
		if (iv) {
			char *e;
			double d = strtod(iv,&e);
			if (*e != 0) {
				printf("intial value must be a double value\n");
				return 1;
			}
		}
	} else {
		printf("invalid signal type\n");
		return 1;
	}
	for (auto &s : NodeCfg.signals()) {
		if (s.name() == a) {
			printf("signal already exists\n");
			return 1;
		}
	}
	SignalConfig *c = NodeCfg.add_signals();
	c->set_name(a);
	c->set_type(type);
	if (iv)
		c->set_iv(iv);
	return 0;
}


void print_hex(uint8_t *b, size_t s, size_t off = 0)
{
	uint8_t *a = b, *e = b + s;
	while (a+16 <= e) {
		printf("%02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx  %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx\n"
			, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]
			, a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
		a += 16;
	}
	if (a == e)
		return;
	int i = 0;
	while (a < e) {
		printf("%02hhx%s",*a,i == 7 ? "  " : " ");
		++i;
		++a;
	}
	printf("\n");
}


int print_hex(const char *ignored)
{
	size_t s;
	if (Software)
		s = NodeCfg.calcSize();
	else
		s = HwCfg.calcSize();
	uint8_t *b = (uint8_t*)malloc(s);
	if (Software)
		NodeCfg.toMemory(b,s);
	else
		HwCfg.toMemory(b,s);
	print_hex(b,s);
	free(b);
	return 0;
}


int print_size(const char *ignored)
{
	printf("node.cfg has %zu bytes\n",NodeCfg.calcSize());
	printf("hw.cfg has %zu bytes\n",HwCfg.calcSize());
	return 0;
}

int print_help(const char *arg);

struct FuncDesc
{
	const char *name;
	int (*func)(const char *);
	const char *help;
};


FuncDesc Functions[] = {
	{ "help",	print_help,	"display this help" },
	{ "?",		print_help,	"display this help" },
	{ "add",	add_field,	"add element to array" },
	{ "clear",	clear_config,	"clear (sub)config" },
	{ "exit",	term,		"terminate" },
	{ "file",	set_filename,	"set name of current file" },
	{ "flashnvs",	flashnvs,	"flash binary file <binfile> to NVS partition" },
	{ "function",	function,	"add function named <f> of type <t> with params {p}" },
	{ "genpart",	genpart,	"generate an NVS partition from current config" },
	{ "hw",		to_hw,		"switch to hardware configuration" },
	{ "idf",	set_idf,	"set directory of IDF to <path>" },
	{ "json",	json_config,	"output current config as JSON" },
	{ "nvsaddr",	nvsaddr,	"set address of NVS partition" },
	{ "passwd",	set_password,	"-c to clear password hash, otherwise calc hash from <pass>" },
	{ "port",	setport,	"set port for flash programming (default: /dev/ttyUSB0)" },
	{ "print",	show_config,	"print currecnt configuration" },
	{ "quit",	term,		"alias to exit" },
	{ "read",	read_config,	"read config from file <filename>" },
	{ "set",	set_config,	"set field <f> to value <v>" },
	{ "show",	show_config,	"print current configuration" },
	{ "signal",	signal_add,	"add signal with name <s> and type <t> and initival value <i>" },
	{ "size",	print_size,	"print size of current configuration" },
	{ "sw",		to_sw,		"switch to software configuration" },
	{ "updatenvs",	updatenvs,	"update NVS partition on target with currenct configuration" },
	{ "verify",	verify_config,	"perform some sanity checks on current config" },
	{ "write",	write_config,	"write config to file <filename>" },
	{ "xxd" ,	print_hex,	"print ASCII hex dump of config" },
};


int print_help(const char *arg)
{
	for (const auto &p : Functions)
		printf("%-10s: %s\n",p.name,p.help);
	return 0;
}


void readSettings()
{
	int fd = open("settings.mk",O_RDONLY);
	if (fd == -1) {
		printf("error opening settings.mk: %s\n",strerror(errno));
		return;
	}
	struct stat st;
	if (-1 == fstat(fd,&st)) {
		printf("error stating settings.mk: %s\n",strerror(errno));
		close(fd);
		return;
	}
	char *buf = (char *) malloc(st.st_size+1);
	int n = read(fd,buf,st.st_size);
	if (-1 == n) {
		printf("error reading settings.mk: %s\n",strerror(errno));
		close(fd);
		free(buf);
		return;
	}
	buf[n] = 0;
	char *c = strchr(buf,'#');
	while (c) {
		do {
			*c++ = '\n';
		} while ((*c != '\n') && (*c != 0));
		c = strchr(c,'#');
	}
	char *k, *nl;
	k = strstr(buf,"PORT=");
	if (k) {
		nl = strchr(k,'\n');
		if (nl) {
			*nl = 0;
			Port = strdup(k+5);
			printf("using port %s\n",Port);
			*nl = '\n';
		}
	}
	k = strstr(buf,"NVS_ADDR=");
	if (k) {
		nl = strchr(k,'\n');
		if (nl) {
			*nl = 0;
			nvsaddr(k+9);
			*nl = '\n';
		}
	}
	if (IdfPath == 0) {
		k = strstr(buf,"IDF_PATH=");
		if (k) {
			nl = strchr(k,'\n');
			if (nl) {
				*nl = 0;
				set_idf(k+9);
				*nl = '\n';
			}
		}
	}
	/* Partition tools between idf32 and idf8266 are incompatible.
	 * Automatically choosing an IDF causes issues. Need information
	 * which IDF to used.
	 * TODO: find a better/convenient solution
	if (IdfPath == 0) {
		k = strstr(buf,"IDF_ESP32=");
		if (k) {
			nl = strchr(k,'\n');
			if (nl) {
				*nl = 0;
				set_idf(k+10);
				*nl = '\n';
			}
		}
	}
	if (IdfPath == 0) {
		k = strstr(buf,"IDF_ESP8266=");
		if (k) {
			nl = strchr(k,'\n');
			if (nl) {
				*nl = 0;
				set_idf(k+12);
				*nl = '\n';
			}
		}
	}
	*/
	k = strstr(buf,"TMPDIR=");
	if (k) {
		nl = strchr(k,'\n');
		if (nl) {
			*nl = 0;
			if (0 == isDir(k+7))
				Tmpdir = strdup(k+7);
			*nl = '\n';
		}
	}
}


static char *completion_generator(const char *text, int state)
{
	if (Software) {

	} else {

	}
	printf("completion_generator(%s,%d)\n",text,state);
	return 0;
}


/*
static char **completion_function(const char *text, int start, int end)
{
	rl_attempted_completion_over = 1;
	return completion_matches((char*)text,completion_generator);
}
*/


int main(int argc, char **argv)
{
	HwCfg.clear();	// reset to defaults
	HwCfg.set_magic(0xae54edcb);
	NodeCfg.clear();
	NodeCfg.set_magic(0xae54edc0);
	if (char *p = getenv("IDF_PATH"))
		set_idf(p);
	readSettings();
	if (Port == 0)
		Port = strdup("/dev/ttyUSB0");
	if (Tmpdir == 0)
		Tmpdir = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
	if (IdfPath == 0)
		printf("IDF_PATH is not set. Please set manually using the idf command.\n");
	for (int x = 1; x < argc; ++x) {
		if (!strcmp(argv[x],"-h")) {
			printf("synopsis:\n"
				"atriumcfg [<filename>|<option>]\n"
				"valid options are:\n"
				"-h: display this help\n"
				"-V: display version\n"
			      );
			exit(0);
		}
		if (!strcmp(argv[x],"-V")) {
			printf("atriumcfg, version %s\n",VERSION);
			exit(0);
		}
		if (read_config(argv[x])) {
			printf("failed to load %s\n",argv[x]);
		} else {
			printf("loaded %s\n",argv[x]);
		}
	}
	//rl_attempted_completion_function = completion_function;
	for (;;) {
		char *line = readline("> ");
		if ((line == 0) || (line[0] == '#'))
			continue;
		add_history(line);
		char *cmd = strtok(line," \t");
		if (cmd == 0) 
			continue;
		char *arg = strtok(0," \t");
		bool found = false;
		for (int i = 0; i < sizeof(Functions)/sizeof(Functions[0]); ++i) {
			if (!strcmp(Functions[i].name,cmd)) {
				int r = Functions[i].func(arg);
				printf("%s\n",r ? "Error" : "OK");
				found = true;
				break;
			}
		}
		free(line);
		if (!found)
			printf("unknown command\n");
	}
	return 0;
}
