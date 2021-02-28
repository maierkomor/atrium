/*
 *  Copyright (C) 2017-2021, Thomas Maier-Komor
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

#include "actions.h"
#include "binformats.h"
#include "dataflow.h"
#include "event.h"
#ifdef CONFIG_SIGNAL_PROC
#include "func.h"
#endif
#include "globals.h"
#include "ujson.h"
#include "log.h"
#include "mqtt.h"
#include "profiling.h"
#include "settings.h"
#include "terminal.h"
#include "timefuse.h"
#include "versions.h"

#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <sstream>

#include <lwip/err.h>
#include <lwip/dns.h>
#include <lwip/inet.h>

#ifdef CONFIG_IDF_TARGET_ESP32
#include <soc/rtc.h>
#include <rom/md5_hash.h>
#elif defined CONFIG_IDF_TARGET_ESP8266
#include <esp_system.h>
#include <esp_wps.h>
extern "C" {
#include <crypto/common.h>
#include <crypto/md5_i.h>
void esp_schedule(void);
void esp_yield(void);
}
#endif

#ifdef CONFIG_IDF_TARGET_ESP8266
#include <apps/sntp/sntp.h>
#if IDF_VERSION > 32
#include <driver/rtc.h>	// post v3.2
#endif
#elif IDF_VERSION >= 32
#include <lwip/apps/sntp.h>	// >= v3.2
#else
#include <apps/sntp/sntp.h>	// <= v3.1
#endif

#ifdef CONFIG_MDNS
#include <mdns.h>	// requires IPv6 to be enabled on CONFIG_IDF_TARGET_ESP8266
#endif
#include <nvs.h>
#include <nvs_flash.h>
#include <tcpip_adapter.h>
#include <string.h>

#include "settings.h"
#include "dht.h"
#include "globals.h"
#include "wifi.h"


using namespace std;

extern "C" const char Version[] = VERSION;
static char TAG[] = "cfg", cfg_err[] = "cfg_err";

#ifdef CONFIG_APP_PARAMS
static int cfg_set_param(const char *name, const char *value);
#endif

/*
static int set_cpu_freq(Terminal &t, const char *v)
{
	if (v == 0) {
		t.printf("%lu\n",Config.cpu_freq());
		return 0;
	}
	if (!strcmp(v,"-c")) {
		Config.clear_cpu_freq();
		return 0;
	}
	long l = strtol(v,0,0);
#ifdef CONFIG_IDF_TARGET_ESP8266
	if ((l != 80) && (l != 160))
		return 1;
#elif defined CONFIG_IDF_TARGET_ESP32
	if ((l != 80) && (l != 160) && (l != 240))
		return 1;
#else
#error unknwon target
#endif
	Config.set_cpu_freq(l);
	return 0;
}
*/


/*
static int set_dns_server(const char *v)
{
	if (v == 0) {
#if 0
		Config.clear_dns_servers();
#else
		Config.clear_dns_server();
#endif
		return 0;
	}
#if 0
	unsigned a[4];
	int n = sscanf(v,"%u.%u.%u.%u",a,a+1,a+2,a+3);
	if ((4 != n) || (a[0] > 255) || (a[1] > 255) || (a[2] > 255) || (a[3] > 255))
		return 1;
	uint32_t ip = a[0] | (a[1]<<8) | (a[2]<<16) | (a[3]<<24);
	Config.add_dns_servers(ip);
#else
	Config.set_dns_server(v);
#endif
	return 0;
}
*/


#ifdef CONFIG_LIGHTCTRL
static int set_threshold_off(Terminal &t, const char *v)
{
	if (v == 0) {
		t.printf("%lu\n",Config.threshold_off());
		return 0;
	}
	if (!strcmp(v,"-c")) {
		Config.clear_threshold_off();
		return 0;
	}
	long l = strtol(v,0,0);
	if ((l < 0) || (l > 1023))
		return 1;
	Config.set_threshold_off(l);
	return 0;
}


static int set_threshold_on(Terminal &t, const char *v)
{
	if (v == 0) {
		t.printf("%lu\n",Config.threshold_on());
		return 0;
	}
	if (!strcmp(v,"-c")) {
		Config.clear_threshold_on();
		return 0;
	}
	long l = strtol(v,0,0);
	if ((l < 0) || (l > 1023))
		return 1;
	Config.set_threshold_on(l);
	return 0;
}


static int set_dim_step(Terminal &t, const char *v)
{
	if (v == 0) {
		t.printf("%lu\n",Config.dim_step());
		return 0;
	}
	if (!strcmp(v,"-c")) {
		Config.clear_dim_step();
		return 0;
	}
	long l = strtol(v,0,0);
	if ((l < 0) || (l > 1023))
		return 1;
	Config.set_dim_step(l);
	return 0;
}
#endif


#if 0
static int set_station2ap_time(Terminal &t, const char *v)
{
	if (v == 0) {
		t.printf("%lu\n",Config.station2ap_time());
		return 0;
	}
	if (!strcmp(v,"-c")) {
		Config.clear_station2ap_time();
		return 0;
	}
	long l = strtol(v,0,0);
	if (l < 0)
		return 1;
	Config.set_station2ap_time(l);
	return 0;
}


template <typename S>
int set_string_option(Terminal &t, S *s, const char *v)
{
	if (v == 0)
		t.println(s->c_str());
	else if (!strcmp(v,"-c"))
		s->clear();
	else
		*s = v;
	return 0;
}


int set_bool_option(Terminal &t, bool *b, const char *v, bool d)
{
	if (v == 0)
		t.printf(b ? "true\n" : "false\n");
	else if (!strcmp(v,"-c"))
		*b = d;
	else if (!strcmp(v,"false"))
		*b = false;
	else if (!strcmp(v,"0"))
		*b = false;
	else if (!strcmp(v,"true"))
		*b = true;
	else if (!strcmp(v,"1"))
		*b = true;
	else
		return 1;
	return 0;
}


static int set_ap_ssid(Terminal &t, const char *value)
{
	return set_string_option(t,Config.mutable_softap()->mutable_ssid(),value);
}


static int set_ap_pass(Terminal &t, const char *value)
{
	return set_string_option(t,Config.mutable_softap()->mutable_pass(),value);
}


static int set_station_ssid(Terminal &t, const char *value)
{
	return set_string_option(t,Config.mutable_station()->mutable_ssid(),value);
}


static int set_station_pass(Terminal &t, const char *value)
{
	return set_string_option(t,Config.mutable_station()->mutable_pass(),value);
}


static int set_syslog_host(Terminal &t, const char *value)
{
	return set_string_option(t,Config.mutable_syslog_host(),value);
}


static int set_dns_server(Terminal &t, const char *value)
{
	return set_string_option(t,Config.mutable_dns_server(),value);
}


static int set_mqtt_uri(Terminal &t, const char *value)
{
	return set_string_option(t,Config.mutable_mqtt()->mutable_uri(),value);
}


static int set_nodename(Terminal &t, const char *value)
{
	return set_string_option(t,Config.mutable_nodename(),value);
}


static int set_syslog(Terminal &t, const char *value)
{
	return set_string_option(t,Config.mutable_syslog_host(),value);
}


int set_timezone(Terminal &t, const char *value)
{
	int r = set_string_option(t,Config.mutable_timezone(),value);
	setenv("TZ",Config.timezone().c_str(),1);
	return r;
}


static int set_sntp_server(Terminal &t, const char *value)
{
	return set_string_option(t,Config.mutable_sntp_server(),value);
}


static int set_domainname(Terminal &t, const char *value)
{
	return set_string_option(t,Config.mutable_domainname(),value);
}


static int set_ap_activate(Terminal &t, const char *value)
{
	return set_bool_option(t,Config.mutable_softap()->mutable_activate(),value,false);
}


static int set_station_activate(Terminal &t, const char *value)
{
	return set_bool_option(t,Config.mutable_station()->mutable_activate(),value,false);
}


static int set_mqtt_enable(Terminal &t, const char *value)
{
	return set_bool_option(t,Config.mutable_mqtt()->mutable_enable(),value,false);
}
#endif


static int set_password(Terminal &t, const char *p)
{
	return setPassword(p);
}


pair<const char *, int (*)(Terminal &t, const char *)> SetFns[] = {
	{"password", set_password},
#if 0
	{"cpu_freq", set_cpu_freq},
	{"timezone", set_timezone},
	{"domainname", set_domainname},
	{"ap_activate", set_ap_activate},
	{"ap_pass", set_ap_pass},
	{"ap_ssid", set_ap_ssid},
	{"dns_server", set_dns_server},
	{"syslog_host", set_syslog_host},
	{"mqtt_enable", set_mqtt_enable},
	{"mqtt_uri", set_mqtt_uri},
	{"nodename", set_nodename},
	{"sntp_server", set_sntp_server},
	{"station2ap_time", set_station2ap_time},
	{"station_activate", set_station_activate},
	{"station_pass", set_station_pass},
	{"station_ssid", set_station_ssid},
	{"syslog", set_syslog},
#ifdef CONFIG_LIGHTCTRL
	{"dim_step", set_dim_step},
	{"threshold_off", set_threshold_off},
	{"threshold_on", set_threshold_on},
#endif
#endif
	{0,0},
};


int update_setting(Terminal &t, const char *name, const char *value)
{
	for (size_t i = 0; SetFns[i].first; ++i) {
		if (0 == strcmp(name,SetFns[i].first))
			return SetFns[i].second(t,value);
	}
	if (0 < Config.setByName(name,value))
		return 0;
#ifdef CONFIG_APP_PARAMS 
	return cfg_set_param(name,value);
#else
	return 1;
#endif
}


void list_settings(Terminal &t)
{
	for (size_t i = 0; SetFns[i].first; ++i)
		t.printf("%s\n",SetFns[i].first);
}


int cfg_set_hostname(const char *hn)
{
	static JsonString *nodename = 0;
	log_info(TAG,"setting hostname to %s",hn);
	if (nodename == 0)
		nodename = RTData->add("node",hn);
	else
		nodename->set(hn);
#ifdef CONFIG_MDNS
	static bool MDNS_up = false;
	if (MDNS_up)
		mdns_free();
	if (esp_err_t e = mdns_init()) {
		log_error(TAG,"mdns init failed: %s",esp_err_to_name(e));
		return 1;
	}
	if (esp_err_t e = mdns_hostname_set(hn)) {
		log_error(TAG,"set mdns hostname failed: %s",esp_err_to_name(e));
		return 1;
	}
	if (esp_err_t e = mdns_instance_name_set(hn)) {
		log_error(TAG,"unable to set mdns instance name: %s",esp_err_to_name(e));
		return 1;
	}
	MDNS_up = true;
#endif
	return 0;
}


void cfg_set_station(const uint8_t *ssid, const uint8_t *pass)
{
	WifiConfig *conf = Config.mutable_station();
	conf->set_ssid((char*)ssid);
	conf->set_pass((char*)pass);
	conf->set_activate(true);
}


const char *cfg_get_domainname()
{
	const char *dn = Config.domainname().c_str();
	if (dn[0] == 0)
		return 0;
	return dn;
}

char *format_hash(char *buf, const uint8_t *hash)
{
	char *at = buf;
	for (int i = 0; i < 8; ++i)
		at += sprintf(at," %02x",*hash++);
	*at++ = ' ';
	for (int i = 8; i < 16; ++i)
		at += sprintf(at," %02x",*hash++);
	*at = 0;
	return buf;
}


int setPassword(const char *p)
{
	//log_info(TAG,"setPassword('%s')",p);
	if ((p == 0) || (p[0] == 0) || (0 == strcmp(p,"-c"))) {
		Config.clear_pass_hash();
		return 0;
	}
	MD5Context ctx;
	MD5Init(&ctx);
	MD5Update(&ctx,(uint8_t*)p,strlen(p));
	uint8_t md5[16];
	MD5Final(md5,&ctx);
	Config.set_pass_hash(md5,sizeof(md5));
	//char buf[64];
	//log_info(TAG,"hash: %s",format_hash(buf,md5));
	return 0;
}


bool verifyPassword(const char *p)
{
//	TimeDelta td(__FUNCTION__);
	if (Config.pass_hash().empty()) {
		if (p[0] == 0)
			return true;
		return false;
	}
	if (*p == 0)
		return false;
	MD5Context ctx;
	MD5Init(&ctx);
	MD5Update(&ctx,(uint8_t*)p,strlen(p));
	uint8_t md5[16];
	MD5Final(md5,&ctx);
	//char buf[256];
	//log_info(TAG,"hash input %s",format_hash(buf,md5));
	//log_info(TAG,"hash nvram %s",format_hash(buf,(uint8_t*)Config.pass_hash().c_str()));
	int r = memcmp(Config.pass_hash().data(),md5,sizeof(md5));
	if (r)
		vTaskDelay(2000);
	return r == 0;
}


bool isValidBaudrate(long l)
{
	switch (l) {
	case 9600:
	case 19200:
	case 38400:
	case 57600:
	case 115200:
		return true;
	default:
		return false;
	}
}


static void set_cfg_err(uint8_t v)
{
	if (NVS == 0)
		log_error(TAG,"cannot set nvs/%s: nvs not mounted",cfg_err);
	else if (esp_err_t e = nvs_set_u8(NVS,cfg_err,v))
		log_error(TAG,"error clearing cfg_ok: %s",esp_err_to_name(e));
	else
		nvs_commit(NVS);
}


static void initNodename()
{
	uint8_t mac[6];
	esp_err_t e = esp_wifi_get_mac(WIFI_IF_STA,mac);
	if (ESP_OK != e)
		e = esp_wifi_get_mac(WIFI_IF_AP,mac);
	if (ESP_OK != e) {
		log_warn(TAG,"unable to determine mac for setting up hostname, using random data");
		uint32_t r = esp_random();
		mac[0] = r & 0xff;
		mac[1] = (r >> 8) & 0xff;
		mac[2] = (r >> 16) & 0xff;
	}
	char hostname[16];
	snprintf(hostname,sizeof(hostname),"node%02x%02x%02x",mac[2],mac[1],mac[0]);
	Config.set_nodename(hostname);
}


void cfg_init_defaults()
{
	log_info(TAG,"setting up default config");
	Config.clear();
	Config.set_magic(0xae54edc0);
	Config.set_actions_enable(true);
	Config.set_sntp_server("0.pool.ntp.org");
	Config.mutable_softap()->set_activate(true);
	initNodename();
	Config.mutable_softap()->set_ssid(Config.nodename().c_str());
}


void cfg_clear_nodecfg()
{
	Config.clear();
	Config.set_magic(0xAE54EDC0);
}


int cfg_erase_nvs()
{
	if (esp_err_t e = nvs_erase_all(NVS)) {
		log_error(TAG,"error erasing nvs/%s: %s",TAG,esp_err_to_name(e));
		return 1;
	}
	return 0;
}


void cfg_factory_reset(void *)
{
	if (esp_err_t e = nvs_erase_all(NVS))
		log_error(TAG,"error erasing nvs/%s: %s",TAG,esp_err_to_name(e));
	esp_wifi_restore();
	esp_restart();
}


int writeNVM(const char *name, const uint8_t *buf, size_t s)
{
	if (esp_err_t e = nvs_set_blob(NVS,name,buf,s))
		log_error(TAG,"cannot write %s (%u bytes): %s",name,s,esp_err_to_name(e));
	else if (esp_err_t e = nvs_commit(NVS))
		log_error(TAG,"cannot commit %s: %s",name,esp_err_to_name(e));
	else
		return 0;
	return 1;
}


int cfg_store_hwcfg()
{
	HWConf.set_magic(0xAE54EDCB);
	size_t s = HWConf.calcSize();
	uint8_t buf[s];
	HWConf.toMemory(buf,s);
	return writeNVM("hw.cfg",buf,s);
}


int cfg_store_nodecfg()
{
	size_t s = Config.calcSize();
	uint8_t buf[s];
	Config.toMemory(buf,s);
	return writeNVM("node.cfg",buf,s);
}


int readNVconfig(const char *name, uint8_t **buf, size_t *len)
{
	size_t s = 0;
	if (esp_err_t e = nvs_get_blob(NVS,name,0,&s)) {
		return e;
	}
	uint8_t *b = (uint8_t*)malloc(s);
	if (b == 0)
		return ENOMEM;
	if (esp_err_t e = nvs_get_blob(NVS,name,b,&s)) {
		free(buf);
		return e;
	}
	*buf = b;
	*len = s;
	return 0;
}


int cfg_read_nodecfg()
{
	const char *name = "node.cfg";
	size_t s = 0;
	uint8_t *buf = 0;
	if (int e = readNVconfig(name,&buf,&s)) {
		log_error(TAG,"error reading %s: %s",name,esp_err_to_name(e));
		return e;
	}
	Config.clear();
	int r = Config.fromMemory(buf,s);
	free(buf);
	if (r < 0) {
		log_error(TAG,"error parsing %s: %d",name,r);
		return r;
	}
	for (int i = 0; i < Config.debugs_size(); ++i)
		log_module_enable(Config.debugs(i).c_str());
	log_info(TAG,"%s: parsed %u bytes",name,s);
	return 0;
}


static int cfg_copy(const char *to, const char *from)
{
	size_t s = 0;
	uint8_t *buf = 0;
	if (int e = readNVconfig(from,&buf,&s)) {
		log_error(TAG,"error reading %s: %s",from,esp_err_to_name(e));
		return e;
	}
	esp_err_t e = nvs_set_blob(NVS,to,buf,s);
	free(buf);
	if (e)
		log_error(TAG,"cannot write %s (%u bytes): %s",to,s,esp_err_to_name(e));
	else if (esp_err_t e = nvs_commit(NVS))
		log_error(TAG,"cannot commit %s: %s",to,esp_err_to_name(e));
	else
		return 0;
	return 1;

}


int cfg_backup_create()
{
	return cfg_copy("node.cfg.bak","node.cfg");
}


int cfg_backup_restore()
{
	return cfg_copy("node.cfg","node.cfg.bak");
}


int cfg_read_hwcfg()
{
	size_t s = 0;
	uint8_t *buf = 0;
	if (int e = readNVconfig("hw.cfg",&buf,&s)) {
		log_error(TAG,"error reading hw.cfg: %s",esp_err_to_name(e));
		return e;
	}
	HWConf.clear();
	int r = HWConf.fromMemory(buf,s);
	free(buf);
	if (r <= 0) {
		log_error(TAG,"error parsing hw.cfg: %d",r);
		return r;
	}
	log_info(TAG,"hw.cfg: parsed %u bytes",s);
	return 0;
}


int set_cpu_freq(unsigned mhz)
{
#ifdef CONFIG_IDF_TARGET_ESP32
	if ((mhz == 80) || (mhz == 160) || (mhz == 240)) {
		ets_update_cpu_frequency(mhz);
		return 0;
	}
	return 1;
#elif defined CONFIG_IDF_TARGET_ESP8266
	if (mhz == 80)
		esp_set_cpu_freq(ESP_CPU_FREQ_80M);
	else if (mhz == 160)
		esp_set_cpu_freq(ESP_CPU_FREQ_160M);
	else
		return 1;
	return 0;
#else
	return 1;
#endif
}


void initDns()
{
	ip_addr_t a;
	const char *dns = Config.dns_server().c_str();
#if defined CONFIG_LWIP_IPV6 || defined CONFIG_IDF_TARGET_ESP32
	a.u_addr.ip4.addr = inet_addr(dns);
	a.type = IPADDR_TYPE_V4;
	if (a.u_addr.ip4.addr != INADDR_NONE)
#else
		a.addr = inet_addr(dns);
	log_info(TAG,"setting dns server %d.%d.%d.%d"
			, (a.addr) & 0xff
			, (a.addr >> 8) & 0xff
			, (a.addr >> 16) & 0xff
			, (a.addr >> 24) & 0xff
			);
	if (a.addr != INADDR_NONE)
#endif
		dns_setserver(0,&a);
	else
		log_error(TAG,"invalid dns server '%s'",dns);

}


void sntp_start()
{
#ifdef CONFIG_SNTP
	if (!Config.has_sntp_server())
		return;
	if (Config.has_timezone())
		setenv("TZ",Config.timezone().c_str(),1);
	sntp_stop();
	const char *server = Config.sntp_server().c_str();
	bool set = false;
	if (strchr(server,'.')) {
		const char *s = Config.sntp_server().c_str();
		sntp_setservername(0,(char*)s);
		log_info(TAG,"sntp server %s",s);
		set = true;
	} else if (Config.has_domainname()) {
		static char *fqhn = 0;
		if (fqhn)
			free(fqhn);
		asprintf(&fqhn,"%s.%s",server,Config.domainname().c_str());
		if (fqhn) {
			set = true;
			sntp_setservername(0,fqhn);
			log_info(TAG,"sntp server %s",fqhn);
		}
	}
	if (set)
		sntp_init();
#endif
}


void cfg_activate()
{
	log_info(TAG,"activating config");

#ifdef CONFIG_DMESG
	if (Config.has_dmesg_size())
		dmesg_resize(Config.dmesg_size());
#endif
	if (!Config.has_nodename()) 
		initNodename();
	cfg_set_hostname(Config.nodename().c_str());

	bool softap = false;
	if (Config.has_softap() && Config.softap().has_ssid() && Config.softap().activate()) {
		wifi_start_softap(Config.softap().ssid().c_str(),Config.softap().has_pass() ? Config.softap().pass().c_str() : "");
		softap = true;
	}

	if (Config.has_station()) {
		const WifiConfig &s = Config.station();
		if (s.has_ssid() && s.has_pass() && s.activate())
			wifi_start_station(s.ssid().c_str(),s.pass().c_str());
	} else if (!softap) {
		wifi_start_softap(Config.nodename().c_str(),"");
	}

	sntp_start();
	if (Config.has_cpu_freq())
		set_cpu_freq(Config.cpu_freq());
#if 0
	if (int n = Config.dns_servers_size())
		dns_setserver(n,(ip_addr_t*)Config.mutable_dns_servers()->data());
#else
	if (Config.has_dns_server())
		initDns();
#endif
}


#ifdef CONFIG_SIGNAL_PROC
void cfg_init_functions()
{
	for (const auto &f : Config.functions()) {
		if (!f.has_name() || !f.has_func())
			continue;
		const char *fn = f.name().c_str();
		const char *ft = f.func().c_str();
		Function *fun = FunctionFactory::create(ft,fn);
		if (fun == 0) {
			log_warn(TAG,"unknown function %s",ft);
			continue;
		}
		log_dbug(TAG,"function %s, type %s",fn,ft);
		unsigned x = 0;
		for (const auto &p : f.params()) {
			log_dbug(TAG,"param %s",p.c_str());
			fun->addParam(p.c_str());
			/*
			if (DataSignal *s = DataSignal::getSignal(p.c_str())) {
				fun->setParam(x,s);
			} else {
				fun->addParam(p.c_str());
			}
			*/
			++x;
		}
	}
}
#endif


void cfg_activate_actions()
{
#ifdef CONFIG_SIGNAL_PROC
	for (const auto &s : Config.signals()) {
		if (!s.has_name() || !s.has_type())
			continue;
		DataSignal *d = 0;
		switch (s.type()) {
		case st_int:
			d = new IntSignal(s.name().c_str());
			break;
		case st_float:
			d = new FloatSignal(s.name().c_str());
			break;
		default:
			continue;
		}
		if (s.has_iv())
			d->initFrom(s.iv().c_str());
		
	}
	fn_init_factories();
#endif
	for (const auto &t : Config.timefuses()) {
		if (!t.has_name() || !t.has_time())
			continue;
		unsigned c = t.config();
		timefuse_create(t.name().c_str(),t.time(),c&1);
		if (c&2) 
			timefuse_start(t.name().c_str());
	}
}


void cfg_activate_triggers()
{
	for (const auto &t : Config.triggers()) {
		Action *a = action_get(t.action().c_str());
		if (a == 0) {
			log_warn(TAG,"unknown action '%s'",t.action().c_str());
			continue;
		}
		event_t e = event_id(t.event().c_str());
		if (e == 0) {
			log_warn(TAG,"unknown event %s",t.event().c_str());
			continue;
		}
		log_dbug(TAG,"event %s triggers action %s",t.event().c_str(),t.action().c_str());
		event_callback(e,a);
	}
}


AppParam *cfg_get_param(const char *name)
{
#ifdef CONFIG_APP_PARAMS 
	for (auto &p : *Config.mutable_app_params()) {
		if (p.key() == name)
			return &p;
	}
#endif
	return 0;
}


#ifdef CONFIG_APP_PARAMS 
AppParam *cfg_add_param(const char *name)
{
	AppParam *x = cfg_get_param(name);
	if (x == 0) {
		x = Config.add_app_params();
		x->set_key(name);
	}
	return x;
}
#endif


#ifdef CONFIG_APP_PARAMS 
static int cfg_set_param(const char *name, const char *value)
{
	AppParam *p = cfg_get_param(name);
	if (p == 0)
		return 1;
	if (p->has_uValue()) {
		char *e;
		long l = strtol(value,&e,0);
		if ((e == value) || (l < 0))
			return 1;
		p->set_uValue(l);
		return 0;
	}
	if (p->has_dValue()) {
		char *e;
		long l = strtol(value,&e,0);
		if (e == value)
			return 1;
		p->set_dValue(l);
		return 0;
	}
	if (p->has_sValue()) {
		p->set_sValue(value);
		return 0;
	}
	if (p->has_fValue()) {
		char *e = 0;
		float f = strtof(value,&e);
		if (e == value)
			return 1;
		p->set_fValue(f);
		return 0;
	}
	return 1;
}
#endif


int cfg_get_uvalue(const char *name, unsigned *u, unsigned def)
{
#ifdef CONFIG_APP_PARAMS 
	if (AppParam *p = cfg_get_param(name))  {
		*u = p->uValue();
		return 0;
	}
	*u = def;
#endif
	return 1;
}


int cfg_get_dvalue(const char *name, signed *u, signed def)
{
#ifdef CONFIG_APP_PARAMS 
	if (AppParam *p = cfg_get_param(name))  {
		*u = p->dValue();
		return 0;
	}
	*u = def;
#endif
	return 1;
}


int cfg_get_fvalue(const char *name, double *u, double def)
{
#ifdef CONFIG_APP_PARAMS 
	if (AppParam *p = cfg_get_param(name))  {
		*u = p->fValue();
		return 0;
	}
	*u = def;
#endif
	return 1;
}


#ifdef CONFIG_APP_PARAMS 
void cfg_set_uvalue(const char *name, unsigned u)
{
	cfg_add_param(name)->set_uValue(u);
}


void cfg_set_dvalue(const char *name, signed d)
{
	cfg_add_param(name)->set_dValue(d);
}


void cfg_set_fvalue(const char *name, double f)
{
	cfg_add_param(name)->set_fValue(f);
}
#endif


void nvs_setup()
{
	if (esp_err_t err = nvs_flash_init()) {
		log_error(TAG,"NVS init failed - erasing NVS");
		err = nvs_flash_erase();
		if (err)
			log_error(TAG,"nvs erase %s",esp_err_to_name(err));
		err = nvs_flash_init();
		if (err)
			log_error(TAG,"nvs init %s",esp_err_to_name(err));
	}
	if (esp_err_t err = nvs_open(TAG,NVS_READWRITE,&NVS))
		log_error(TAG,"NVS open failed: %s",esp_err_to_name(err));
	else 
		cfg_read_hwcfg();
}


uint8_t read_nvs_u8(const char *id, uint8_t d)
{
        uint8_t v;
        if (esp_err_t e = nvs_get_u8(NVS,id,&v)) {
                log_error(TAG,"error getting nvs/%s: %s",id,esp_err_to_name(e));
                return d;
        }
        return v;
}


void store_nvs_u8(const char *id, uint8_t v)
{
        if (esp_err_t e = nvs_set_u8(NVS,id,v))
                log_error(TAG,"error setting %s to %u: %s",id,(unsigned)v,esp_err_to_name(e));
        else if (esp_err_t e = nvs_commit(NVS))
                log_error(TAG,"error committing %s to %u: %s",id,(unsigned)v,esp_err_to_name(e));
}


void settings_setup()
{
	uint8_t setup = 0;
	log_info(TAG,"setup");
	if (ESP_OK == nvs_open(TAG,NVS_READWRITE,&NVS)) {
		uint8_t u8;
		if (0 == nvs_get_u8(NVS,cfg_err,&u8)) {
			if (u8)
				log_warn(TAG,"%s: %u: ignoring configuration",cfg_err,u8);
			else
				// settings available
				log_info(TAG,"%s: %u",cfg_err,u8);
			setup = u8;
		}
		set_cfg_err(1);
	}

	if (setup == 0) {
		if (cfg_read_nodecfg()) {
			cfg_init_defaults();
			set_cfg_err(2);
		}
		if (!Config.has_station()) {
#ifdef CONFIG_WPS
			wifi_wps_start();
#elif defined CONFIG_SMARTCONFIG
			smartconfig_start();
#endif
		}
	} else if (setup == 1) {
		cfg_init_defaults();
		set_cfg_err(2);
	}
#ifdef CONFIG_WPS
	action_add("wps!start",wifi_wps_start,0,"start WPS configuration");
#endif
	if (Config.actions_enable() & 0x2)
		action_add("cfg!factory_reset",cfg_factory_reset,0,"perform a factory reset");
#if defined HGID && defined HGREV
	RTData->add("version",VERSION ", HgId " HGID ", Revision " HGREV);
#else
	RTData->add("version",VERSION);
#endif
	RTData->add("reset_reason",strReset((rstrsn_t)esp_reset_reason()));
	set_cfg_err(0);
}


