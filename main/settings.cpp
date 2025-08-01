/*
 *  Copyright (C) 2017-2025, Thomas Maier-Komor
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
#include "event.h"
#include "globals.h"
#include "hwcfg.h"
#include "env.h"
#include "log.h"
#include "nvm.h"
#include "netsvc.h"
#include "profiling.h"
#include "settings.h"
#include "usntp.h"
#include "swcfg.h"
#include "syslog.h"
#include "terminal.h"
#include "timefuse.h"
#include "udns.h"

#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#if IDF_VERSION >= 50
#include <rom/ets_sys.h>
#endif

#include <lwip/err.h>
#include <lwip/dns.h>
#include <lwip/inet.h>

#ifdef CONFIG_IDF_TARGET_ESP8266
#include <esp_system.h>
#include <esp_wps.h>
extern "C" {
#include <rom/md5_hash.h>
void esp_schedule(void);
void esp_yield(void);
}
#endif


#include <stdlib.h>
#include <string.h>
#include <sstream>

#include "settings.h"
#include "dht.h"
#include "globals.h"
#include "wifi.h"

#if IDF_VERSION >= 44
#include <esp_sntp.h>
#include <md/esp_md.h>
#define MD5Init(ctx) esp_md5_init(ctx)
#define MD5Update(ctx,buf,n) esp_md5_update(ctx,buf,n)
#define MD5Final(dig,ctx) esp_md5_finish(ctx,dig)
#else
#include <rom/md5_hash.h>
#endif

using namespace std;

#define TAG MODULE_CFG
static const char cfg_err[] = "cfg_err";

#ifdef CONFIG_APP_PARAMS
static int cfg_set_param(const char *name, const char *value);
#endif


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


int set_timezone(Terminal &t, const char *value)
{
	int r = set_string_option(t,Config.mutable_timezone(),value);
	setenv("TZ",Config.timezone().c_str(),1);
	return r;
}


static int set_domainname(Terminal &t, const char *value)
{
	setdomainname(value,0);
	return set_string_option(t,Config.mutable_domainname(),value);
}


static int set_password(Terminal &t, const char *p)
{
	return setPassword(p);
}


pair<const char *, int (*)(Terminal &t, const char *)> SetFns[] = {
	{"password", set_password},
	{"domainname", set_domainname},
	{"timezone", set_timezone},
	{0,0},
};


const char *update_setting(Terminal &t, const char *name, const char *value)
{
	for (size_t i = 0; SetFns[i].first; ++i) {
		if (0 == strcmp(name,SetFns[i].first))
			return SetFns[i].second(t,value) ? "Failed." : 0;
	}
	if (0 < Config.setByName(name,value))
		return 0;
#ifdef CONFIG_APP_PARAMS 
	return cfg_set_param(name,value) ? "Failed." : 0;
#else
	return "Failed.";
#endif
}


int cfg_set_hostname(const char *hn)
{
	if ((hn == 0) || (0 != sethostname(hn,0)))
		return 1;
	EnvString *n = static_cast<EnvString*>(RTData->get("node"));
	if (n == 0) {
		n = RTData->add("node",hn);
	} else {
		n->set(hn);
	}
#ifdef CONFIG_MDNS
	static bool MDNS_up = false;
	if (MDNS_up)
		mdns_free();
	if (esp_err_t e = mdns_init()) {
		log_warn(TAG,"mdns init: %s",esp_err_to_name(e));
		return 1;
	}
	if (esp_err_t e = mdns_hostname_set(hn)) {
		log_warn(TAG,"set mdns hostname: %s",esp_err_to_name(e));
		return 1;
	}
	if (esp_err_t e = mdns_instance_name_set(hn)) {
		log_warn(TAG,"set mdns instance: %s",esp_err_to_name(e));
		return 1;
	}
	MDNS_up = true;
#endif
	return 0;
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
//	PROFILE_FUNCTION();
	if (Config.pass_hash().empty()) {
		return (p[0] == 0);
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


void set_cfg_err(uint8_t v)
{
	nvm_store_u8(cfg_err,v);
}


static void initNodename()
{
	uint8_t mac[6];
	esp_err_t e;
#if IDF_VERSION >= 50
	e = ESP_ERR_NOT_FOUND;
	if (esp_netif_t *nif = esp_netif_next_unsafe(0))
		e = esp_netif_get_mac(nif,mac);
#else
	e = esp_wifi_get_mac(WIFI_IF_STA,mac);
	if (ESP_OK != e)
		e = esp_wifi_get_mac(WIFI_IF_AP,mac);
#endif
	if (ESP_OK != e) {
		log_warn(TAG,"no MAC address: %s",esp_err_to_name(e));
		uint32_t r = esp_random();
		mac[0] = r & 0xff;
		mac[1] = (r >> 8) & 0xff;
		mac[2] = (r >> 16) & 0xff;
	}
	char hostname[16];
	int n = snprintf(hostname,sizeof(hostname),"node%02x%02x%02x",mac[2],mac[1],mac[0]);
	Config.set_nodename(hostname);
	sethostname(hostname,n);
}


void cfg_init_defaults()
{
	log_info(TAG,"init defaults");
	Config.clear();
	Config.set_magic(0xae54edc0);
	Config.set_actions_enable(true);
	Config.set_sntp_server("0.pool.ntp.org");
	Config.mutable_softap()->set_activate(true);
	initNodename();
	Config.mutable_softap()->set_ssid(Config.nodename().c_str());
	setenv("TZ","C",1);
}


void cfg_clear_nodecfg()
{
	Config.clear();
	Config.set_magic(0xAE54EDC0);
}


void cfg_factory_reset(void *)
{
	nvm_erase_all();
#if IDF_VERSION < 50
	esp_wifi_restore();
#endif
	esp_restart();
}


int cfg_store_hwcfg()
{
	int r;
	size_t s = HWConf.calcSize();
	if (uint8_t *buf = (uint8_t *) malloc(s)) {
		HWConf.toMemory(buf,s);
		r = nvm_store_blob("hw.cfg",buf,s);
		free(buf);
	} else {
		r = ESP_ERR_NO_MEM;
	}
	return r;
}


int cfg_store_nodecfg()
{
	int r;
	size_t s = Config.calcSize();
	if (uint8_t *buf = (uint8_t *) malloc(s)) {
		Config.toMemory(buf,s);
		r = nvm_store_blob("node.cfg",buf,s);
		free(buf);
	} else {
		r = ESP_ERR_NO_MEM;
	}
	return r;
}


int cfg_read_nodecfg()
{
	PROFILE_FUNCTION();
	const char *name = "node.cfg";
	size_t s = nvm_blob_size(name);
	if (0 == s)
		return 0;
	uint8_t *buf = (uint8_t*)malloc(s);
	if (0 == buf)
		return ESP_ERR_NO_MEM;
	if (int e = nvm_read_blob(name,buf,&s)) {
		log_warn(TAG,"error reading %s: %s",name,esp_err_to_name(e));
		return e;
	}
	Config.clear();
	int r = Config.fromMemory(buf,s);
	free(buf);
	if (r < 0) {
		log_warn(TAG,"parse %s: %d",name,r);
		return r;
	}
	log_info(TAG,"%s: %u bytes",name,s);
	if (!Config.has_nodename())
		initNodename();
	const auto &nn = Config.nodename();
	RTData->add("node",nn.c_str());
	cfg_set_hostname(nn.c_str());
#ifdef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	for (const auto &m : Config.debugs())
		log_module_enable(m.c_str());
#else
	log_module_enable(Config.debugs());
#endif
	return 0;
}


int cfg_read_hwcfg()
{
	PROFILE_FUNCTION();
	const char *name = "hw.cfg";
	size_t s = nvm_blob_size(name);
	if (0 == s)
		return 0;
	uint8_t *buf = (uint8_t*)malloc(s);
	if (int e = nvm_read_blob(name,buf,&s)) {
		log_warn(TAG,"hw.cfg: %s",esp_err_to_name(e));
		return e;
	}
	HWConf.clear();
	int r = HWConf.fromMemory(buf,s);
	free(buf);
	if (r <= 0) {
		log_warn(TAG,"parsing %s: %d",name,r);
		return r;
	}
	log_info(TAG,"%s: %u bytes",name,s);
	return 0;
}


void cfg_init_hwcfg()
{
	uint8_t setup = nvm_read_u8("hwconf",0);
	if (setup) {
		log_warn(TAG,"%s: %u: last boot failed - skipping hwconf",cfg_err,setup);
	} else {
		nvm_store_u8("hwconf",1);
		cfg_read_hwcfg();
	}
}


int set_cpu_freq(unsigned mhz)
{
#ifdef ESP32
	if ((80 == mhz) || (160 == mhz) || (240 == mhz) || (320 == mhz) || (480 == mhz)) {
#ifdef CONFIG_IDF_TARGET_ESP32
		// for whatever reason ESP32 has only the _rom version
		// which the other ESP32-* don't have...
		ets_update_cpu_frequency_rom(mhz);
#else
		ets_update_cpu_frequency(mhz);
#endif
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
#error missing implementation
#endif
}


extern "C"
void dns_setserver(uint8_t idx, const ip_addr_t *addr);
void initDns()
{
#ifdef CONFIG_UDNS
	for (const auto &dns : Config.dns_server())
		udns_add_nameserver(dns.c_str());
#else
	unsigned x = 0;
	for (const auto &dns : Config.dns_server()) {
		ip_addr_t a;
		if (inet_aton(dns.c_str(),&a)) {
			log_info(TAG,"DNS %s",dns.c_str());
			dns_setserver(x++,&a);
		} else {
			log_warn(TAG,"invalid DNS IP '%s'",dns);
		}
	}
#endif
}


void sntp_setup()
{
	sntp_bc_init();
#ifdef CONFIG_LWIP_IGMP
	sntp_mc_init();
#endif
	if (Config.has_sntp_server())
		sntp_set_server(Config.sntp_server().c_str());
}


void cfg_activate()
{
	PROFILE_FUNCTION();
	log_info(TAG,"activate config");

	if (Config.has_timezone())
		setenv("TZ",Config.timezone().c_str(),1);
	if (!Config.has_nodename()) 
		initNodename();
	if (Config.has_domainname()) {
		const auto dn = Config.domainname();
		setdomainname(dn.c_str(),dn.size());
	}
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
		/* just too flaky to auto-trigger...
#ifdef CONFIG_WPS
		wifi_wps_start();
#elif defined CONFIG_SMARTCONFIG
		smartconfig_start();
#endif
		}
		*/
		wifi_start_softap(Config.nodename().c_str(),"");
	}

	if (wifi_station_isup() && Config.has_sntp_server())
		sntp_set_server(Config.sntp_server().c_str());
	if (Config.has_cpu_freq())
		set_cpu_freq(Config.cpu_freq());
}


void cfg_init_timers()
{
	for (const auto &t : Config.timefuses()) {
		if (!t.has_name() || !t.has_time())
			continue;
		unsigned c = t.config();
		timefuse_t tf = timefuse_create(t.name().c_str(),t.time(),c&1);
		if (c&2) 
			timefuse_start(tf);
	}
}


void cfg_activate_triggers()
{
	PROFILE_FUNCTION();
#ifdef CONFIG_THRESHOLDS
	for (const auto &t : Config.thresholds()) {
		const char *name = t.name().c_str();
		if (!t.has_low() || !t.has_high()) {
			log_warn(TAG,"both thresholds for %s need to be set",name);
			continue;
		}
		EnvElement *e = RTData->getByPath(name);
		if (e == 0) {
			log_warn(TAG,"cannot set threshold on unknown %s",name);
			continue;
		}
		EnvNumber *n = e->toNumber();
		const char *nn = name;
		if (n == 0) {
			e = RTData->find(name);
			if (e == 0)
				log_warn(TAG,"%s not found",name);
			else if (EnvObject *o = e->toObject()) {
				log_info(TAG,"object %s",name);
				if (EnvElement *c = o->getChild("raw")) {
					n = c->toNumber();
					nn = name;
				}
			}
		}
		if (n == 0)
			log_warn(TAG,"set thresholds on %s: not found",name);
		else if (n->setThresholds(t.low(),t.high(),nn))
			log_warn(TAG,"invalid thresholds [%f,%f]",t.low(),t.high());
		else
			log_info(TAG,"thresholds for %s [%f,%f]",name,t.low(),t.high());
	}
#endif
	for (const auto &t : Config.triggers()) {
		const char *en = t.event().c_str();
		event_t e = event_id(en);
		if (e == 0) {
			log_warn(TAG,"unknown event %s",en);
			continue;
		}
		for (const auto &action : t.action()) {
			const char *an = action.c_str();
			char *arg = 0;
			const char *cmd = an;
			if (char *sp = strchr(an,' ')) {
				arg = sp+1;
				char *tmp = (char *) alloca(arg-an);
				memcpy(tmp,an,sp-an);
				tmp[sp-an] = 0;
				cmd = tmp;
			}
			if (Action *a = action_get(cmd)) {
				log_dbug(TAG,"%s => %s(%s)",en,cmd,arg?arg:"");
				event_callback_arg(e,a,arg);
			} else {
				log_warn(TAG,"unknown action %s",an);
			}
		}
	}
}


#ifdef CONFIG_APP_PARAMS
AppParam *cfg_get_param(const char *name)
{
	for (auto &p : *Config.mutable_app_params()) {
		if (p.key() == name)
			return &p;
	}
	return 0;
}
#endif


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
	PROFILE_FUNCTION();
	AppParam *p = cfg_get_param(name);
	int r = 0;
	if (p == 0)
		r = 1;
	else if (p->has_uValue()) {
		char *e;
		long l = strtol(value,&e,0);
		if ((e == value) || (l < 0))
			return 1;
		p->set_uValue(l);
	} else if (p->has_dValue()) {
		char *e;
		long l = strtol(value,&e,0);
		if (e == value)
			return 1;
		p->set_dValue(l);
	} else if (p->has_sValue()) {
		p->set_sValue(value);
	} else if (p->has_fValue()) {
		char *e = 0;
		float f = strtof(value,&e);
		if (e == value)
			return 1;
		p->set_fValue(f);
	} else {
		r = 1;
	}
	return r;
}
#endif


#ifdef CONFIG_APP_PARAMS
int cfg_get_uvalue(const char *name, unsigned *u, unsigned def)
{
	if (AppParam *p = cfg_get_param(name))  {
		*u = p->uValue();
		return 0;
	}
	*u = def;
	return 1;
}
#endif


#ifdef CONFIG_APP_PARAMS
int cfg_get_dvalue(const char *name, signed *u, signed def)
{
	if (AppParam *p = cfg_get_param(name))  {
		*u = p->dValue();
		return 0;
	}
	*u = def;
	return 1;
}
#endif


#ifdef CONFIG_APP_PARAMS
int cfg_get_fvalue(const char *name, double *u, double def)
{
	if (AppParam *p = cfg_get_param(name))  {
		*u = p->fValue();
		return 0;
	}
	*u = def;
	return 1;
}
#endif


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


void settings_setup()
{
	uint8_t setup = nvm_read_u8(cfg_err,0);
	if (setup)
		log_warn(TAG,"%s: %u: ignore node.cfg",cfg_err,setup);
	else
		log_info(TAG,"%s: 0",cfg_err);
	set_cfg_err(1);
	if (setup == 0) {
		if (cfg_read_nodecfg()) {
			cfg_init_defaults();
			set_cfg_err(2);
		}
	} else if (setup == 1) {
		cfg_init_defaults();
		set_cfg_err(2);
	}
#ifdef CONFIG_SYSLOG
	dmesg_resize(Config.dmesg_size());		// update dmesg buffer size
#endif
#ifdef CONFIG_WPS
	action_add("wps!start",wifi_wps_start,0,"start WPS");
#endif
	if (Config.actions_enable() & 0x2)
		action_add("cfg!factory_reset",cfg_factory_reset,0,"perform a factory reset");
	set_cfg_err(0);
}

