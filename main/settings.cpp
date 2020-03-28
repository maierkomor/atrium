/*
 *  Copyright (C) 2017-2020, Thomas Maier-Komor
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

#include "settings.h"
#include <sdkconfig.h>
#include "globals.h"
#include "log.h"
#include "mqtt.h"
#include "profiling.h"
#include "version.h"

#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <sstream>

#include <lwip/err.h>
#include <lwip/dns.h>
#include <lwip/inet.h>

#ifdef ESP32
#include <soc/rtc.h>
#include <rom/md5_hash.h>
#elif defined ESP8266
#include <esp_system.h>
extern "C" {
#include <crypto/common.h>
#include <crypto/md5_i.h>
void esp_schedule(void);
void esp_yield(void);
}
#endif

#ifdef ESP8266
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
#include <mdns.h>	// requires IPv6 to be enabled on ESP8266
#endif
#include <nvs.h>
#include <nvs_flash.h>
#include <tcpip_adapter.h>
#include <string.h>

#include "settings.h"
#include "dht.h"
#include "globals.h"
#include "clock.h"
#include "wifi.h"


using namespace std;

extern "C" const char Version[] = VERSION;
static char TAG[] = "cfg", cfg_err[] = "cfg_err";
static nvs_handle NVS = 0;


static int set_ap_ssid(const char *v)
{
	if (v == 0)
		Config.mutable_softap()->clear_ssid();
	else
		Config.mutable_softap()->set_ssid(v);
	return 0;
}


static int set_ap_pass(const char *v)
{
	if (v == 0)
		Config.mutable_softap()->clear_pass();
	else if (strlen(v) < 8)
		return 1;
	else
		Config.mutable_softap()->set_pass(v);
	return 0;
}


static int set_ap_activate(const char *v)
{
	if (v == 0) {
		Config.mutable_softap()->set_activate(false);
		return 0;
	}
	if (0 == strcmp(v,"1"))
		Config.mutable_softap()->set_activate(true);
	else if (0 == strcmp(v,"0"))
		Config.mutable_softap()->set_activate(false);
	else
		return 1;
	return 0;
}


static int set_cpu_freq(const char *v)
{
	if (v == 0) {
		Config.clear_cpu_freq();
		return 0;
	}
	long l = strtol(v,0,0);
	if ((l != 40) && (l != 80))
		return 1;
	Config.set_cpu_freq(l);
	return 0;
}


static int set_max_on_time(const char *v)
{
	if (v == 0)
		Config.clear_max_on_time();
	long l = strtol(v,0,0);
	if ((l < 0) || (l > 60*24))
		return 1;
	if (l == 0)
		Config.clear_max_on_time();
	else
		Config.set_max_on_time(l);
	return 0;
}


static int set_nodename(const char *v)
{
	if (v == 0)
		Config.clear_nodename();
	else
		Config.set_nodename(v);
	return 0;
}


static int set_domainname(const char *v)
{
	if (v == 0)
		Config.clear_domainname();
	else
		Config.set_domainname(v);
	return 0;
}


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


static int set_sntp_server(const char *v)
{
	if (v == 0)
		Config.clear_sntp_server();
	else
		Config.set_sntp_server(v);
	return 0;
}


static int set_station_ssid(const char *v)
{
	if (v == 0) {
		Config.mutable_station()->clear_ssid();
		Config.mutable_station()->clear_pass();
	} else if (Config.station().ssid() != v) {
		Config.mutable_station()->set_ssid(v);
		Config.mutable_station()->clear_pass();
	}
	return 0;
}


static int set_station_pass(const char *v)
{
	if (v == 0) {
		Config.mutable_station()->clear_pass();
		return 0;
	}
	else if (strlen(v) < 8)
		return 1;
	else
		Config.mutable_station()->set_pass(v);
	return 0;
}


static int set_station_activate(const char *v)
{
	if (0 == v) {
		Config.mutable_station()->set_activate(false);
		return 0;
	}
	if (0 == strcmp(v,"1"))
		Config.mutable_station()->set_activate(true);
	else if (0 == strcmp(v,"0"))
		Config.mutable_station()->set_activate(false);
	else
		return 1;
	return 0;
}


static int set_syslog(const char *v)
{
	if (v == 0)
		Config.clear_syslog_host();
	else
		Config.set_syslog_host(v);
	return 0;
}


#ifdef CONFIG_LIGHTCTRL
static int set_threshold_off(const char *v)
{
	if (v == 0) {
		Config.clear_threshold_off();
		return 0;
	}
	long l = strtol(v,0,0);
	if ((l < 0) || (l > 1023))
		return 1;
	Config.set_threshold_off(l);
	return 0;
}


static int set_threshold_on(const char *v)
{
	if (v == 0) {
		Config.clear_threshold_on();
		return 0;
	}
	long l = strtol(v,0,0);
	if ((l < 0) || (l > 1023))
		return 1;
	Config.set_threshold_on(l);
	return 0;
}


static int set_dim_step(const char *v)
{
	if (v == 0) {
		Config.clear_threshold_on();
		return 0;
	}
	long l = strtol(v,0,0);
	if ((l < 0) || (l > 1023))
		return 1;
	Config.set_dim_step(l);
	return 0;
}
#endif


static int set_station2ap_time(const char *v)
{
	if (v == 0) {
		Config.clear_station2ap_time();
		return 0;
	}
	long l = strtol(v,0,0);
	if (l < 0)
		return 1;
	Config.set_station2ap_time(l);
	return 0;
}


int set_timezone(const char *v)
{
	if (v == 0)
		Config.clear_timezone();
	else
		Config.set_timezone(v);
	return 0;
}


#ifdef CONFIG_MQTT
static int set_mqtt_uri(const char *v)
{
	if (v == 0)
		Config.mutable_mqtt()->clear_uri();
	else
		Config.mutable_mqtt()->set_uri(v);
	return 0;
}


static int set_mqtt_enable(const char *v)
{
	if (v == 0)
		Config.mutable_mqtt()->set_enable(false);
	else if (!strcmp(v,"false"))
		Config.mutable_mqtt()->set_enable(false);
	else if (!strcmp(v,"0"))
		Config.mutable_mqtt()->set_enable(false);
	else if (!strcmp(v,"true"))
		Config.mutable_mqtt()->set_enable(true);
	else if (!strcmp(v,"1"))
		Config.mutable_mqtt()->set_enable(true);
	return 0;
}
#endif // CONFIG_MQTT


pair<const char *, int (*)(const char *)> SetFns[] = {
	{"ap_ssid", set_ap_ssid},
	{"ap_pass", set_ap_pass},
	{"ap_activate", set_ap_activate},
	{"cpu_freq", set_cpu_freq},
	//{"debug", set_debug},
	{"max_on_time", set_max_on_time},
#ifdef CONFIG_MQTT
	{"mqtt_uri", set_mqtt_uri},
	{"mqtt_enable", set_mqtt_enable},
#endif
	{"nodename", set_nodename},
	{"domainname", set_domainname},
	{"password", setPassword},
	{"dns_server", set_dns_server},
	{"sntp_server", set_sntp_server},
	{"station_ssid", set_station_ssid},
	{"station_pass", set_station_pass},
	{"station_activate", set_station_activate},
	{"syslog", set_syslog},
#ifdef CONFIG_LIGHTCTRL
	{"threshold_off", set_threshold_off},
	{"threshold_on", set_threshold_on},
	{"dim_step", set_dim_step},
#endif
	{"timezone", set_timezone},
	{"station2ap_time", set_station2ap_time},
	{0,0},
};


int change_setting(const char *name, const char *value)
{
	for (size_t i = 0; SetFns[i].first; ++i) {
		if (0 == strcmp(name,SetFns[i].first))
			return SetFns[i].second(value);
	}
	return -1;
}


uint8_t read_nvs_u8(const char *id, uint8_t d)
{
	uint8_t v;
	if (esp_err_t e = nvs_get_u8(NVS,id,&v)) {
		log_error(TAG,"error setting %s to %u: %s",id,(unsigned)v,esp_err_to_name(e));
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


int setHostname(const char *hn)
{
#ifdef CONFIG_MDNS
	static bool MDNS_up = false;
	log_info(TAG,"setting hostname to %s",hn);
	if (MDNS_up)
		mdns_free();
	else
		MDNS_up = true;
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
#endif
	RTData.set_node(hn);
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
	if ((p == 0) || (p[0] == 0)) {
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
	if (NVS == 0) {
		log_error(TAG,"cannot set nvs/%s: nvs not mounted",cfg_err);
		return;
	}
	if (esp_err_t e = nvs_set_u8(NVS,cfg_err,v))
		log_error(TAG,"error clearing cfg_ok: %s",esp_err_to_name(e));
	else
		nvs_commit(NVS);
}


void setupDefaults()
{
	log_info(TAG,"setting up default config");
	uint8_t mac[6];
	esp_err_t e = esp_wifi_get_mac(ESP_IF_WIFI_STA,mac);
	if (ESP_OK != e)
		e = esp_wifi_get_mac(ESP_IF_WIFI_AP,mac);
#ifdef ESP32
	if (ESP_OK != e) 
		e = esp_wifi_get_mac(ESP_IF_ETH,mac);
#endif
	if (ESP_OK != e) {
		log_error(TAG,"unable to determine mac for setting up hostname");
		memset(mac,0,sizeof(mac));
	}
	char hostname[64];
	snprintf(hostname,sizeof(hostname),"node%02x%02x%02x",mac[2],mac[1],mac[0]);
	Config.clear();
	setPassword("");
	Config.set_nodename(hostname);
	Config.mutable_softap()->set_activate(true);
	Config.mutable_softap()->set_ssid(hostname);
	Config.mutable_softap()->set_pass("");
	Config.mutable_station()->set_activate(false);
	Config.mutable_station()->set_ssid("");
	Config.mutable_station()->set_pass("");
	Config.set_actions_enable(true);
	Config.set_sntp_server("0.pool.ntp.org");
#ifdef CONFIG_MQTT
	Config.mutable_mqtt()->clear_uri();
	Config.mutable_mqtt()->set_enable(false);
#endif
}


void clearSettings()
{
	NodeConfig c;
	Config = c;
}


void eraseSettings()
{
	if (esp_err_t e = nvs_erase_all(NVS))
		log_error(TAG,"error erasing nvs/%s: %s",TAG,esp_err_to_name(e));
}


void factoryReset()
{
	if (esp_err_t e = nvs_erase_all(NVS))
		log_error(TAG,"error erasing nvs/%s: %s",TAG,esp_err_to_name(e));
	esp_wifi_restore();
	esp_restart();
}


void storeSettings()
{
	size_t s = Config.calcSize();
	uint8_t buf[s];
	Config.toMemory(buf,s);

	log_info(TAG,"storing settings");
	if (ESP_OK != nvs_set_blob(NVS,"node.cfg",buf,s)) {
		log_error(TAG,"cannot write node.cfg");
	} else if (ESP_OK != nvs_commit(NVS)) {
		log_error(TAG,"cannot commit node.cfg");
	}
}


uint8_t *readNVconfig(size_t *len)
{
	size_t s = 0;
	if (ESP_OK != nvs_get_blob(NVS,"node.cfg",0,&s)) {
		log_error(TAG,"cannot determine size of node.cfg");
		return 0;
	}
	uint8_t *buf = (uint8_t*)malloc(s);
	if (buf == 0) {
		log_error(TAG,"out of memory");
		return 0;
	}
	esp_err_t e = nvs_get_blob(NVS,"node.cfg",buf,&s);
	if (ESP_OK != e) {
		free(buf);
		log_error(TAG,"cannot get blob node.cfg: %s",esp_err_to_name(e));
		return 0;
	}
	*len = s;
	log_info(TAG,"loaded config from NVS");
	return buf;
}


bool readSettings()
{
	size_t s;
	uint8_t *buf = readNVconfig(&s);
	if (buf == 0)
		return false;
	Config.clear();
	int r = Config.fromMemory(buf,s);
	if (r < 0)
		log_error(TAG,"parsing hw.cfg returend %d",r);
	free(buf);
	return true;
}


int set_cpu_freq(unsigned mhz)
{
	rtc_cpu_freq_t f;
	if (mhz == 80)
		f = RTC_CPU_FREQ_80M;
	else if (mhz == 160)
		f = RTC_CPU_FREQ_160M;
#ifdef ESP32
	else if (mhz == 240)
		f = RTC_CPU_FREQ_240M;
	else if (rtc_clk_xtal_freq_get() == mhz)
		f = RTC_CPU_FREQ_XTAL;
#endif
	else
		return 1;
	rtc_clk_cpu_freq_set(f);
	return 0;
}


void initDns()
{
	ip_addr_t a;
#if defined CONFIG_LWIP_IPV6 || defined ESP32
	a.u_addr.ip4.addr = inet_addr(Config.dns_server().c_str());
	a.type = IPADDR_TYPE_V4;
	if (a.u_addr.ip4.addr != INADDR_NONE)
#else
		a.addr = inet_addr(Config.dns_server().c_str());
	log_info(TAG,"setting dns server %x",a.addr);
	if (a.addr != INADDR_NONE)
#endif
		dns_setserver(0,&a);
	else
		log_error(TAG,"invalid dns server '%s'",Config.dns_server().c_str());

}


void activateSettings()
{
	log_info(TAG,"activating config");

	if (Config.has_nodename())
		setHostname(Config.nodename().c_str());

	if (Config.has_softap() && Config.softap().has_ssid() && Config.softap().activate())
		wifi_start_softap(Config.softap().ssid().c_str(),Config.softap().has_pass() ? Config.softap().pass().c_str() : "");

	if (Config.has_station() && Config.station().has_ssid() && Config.station().has_pass() && Config.station().activate())
		wifi_start_station(Config.station().ssid().c_str(),Config.station().pass().c_str());
#ifdef CONFIG_SNTP
#if 0
	if (int n = Config.sntp().servers_size()) {
		sntp_stop();
		for (int i = 0; i < n; ++i)
			sntp_setservername(i,(char*)Config.sntp().servers(i).c_str());
	}
	if (Config.sntp().has_timezone())
		setenv("TZ",Config.sntp().timezone().c_str(),1);
	if (Config.sntp().enable())
		sntp_init();
#else
	if (Config.has_timezone())
		setenv("TZ",Config.timezone().c_str(),1);
	if (Config.has_sntp_server()) {
		sntp_stop();
		sntp_setservername(0,(char*)Config.sntp_server().c_str());
		sntp_init();
	}
#endif
#endif
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


void nvs_setup()
{
	log_info(TAG,"NVS init");
	if (esp_err_t err = nvs_flash_init()) {
		log_warn(TAG,"NVS init failed - erasing NVS");
		err = nvs_flash_erase();
		if (err)
			log_error(TAG,"nvs erase %s",esp_err_to_name(err));
		err = nvs_flash_init();
		if (err)
			log_error(TAG,"nvs init %s",esp_err_to_name(err));
	}
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
				log_info(TAG,"%s: %u",cfg_err,u8);
			setup = u8;
		}
		set_cfg_err(1);
	}

	if (setup == 0) {
		if (!readSettings()) {
			setupDefaults();
			set_cfg_err(2);
		}
		activateSettings();
		if (!Config.has_station()) {
#ifdef CONFIG_WPS
			wifi_wps_start();
#elif defined CONFIG_SMARTCONFIG
			smartconfig_start();
#endif
		}
	} else if (setup == 1) {
		setupDefaults();
		set_cfg_err(2);
		activateSettings();
	}
#if defined HGID && defined HGREV
	RTData.set_version(VERSION ", HgId " HGID ", Revision " HGREV);
#else
	RTData.set_version(VERSION);
#endif
	RTData.set_reset_reason((rstrsn_t)esp_reset_reason());
#ifdef CONFIG_OTA
	const esp_partition_t *u = esp_ota_get_next_update_partition(NULL);
	RTData.set_update_part(u->label);
	RTData.set_update_state("idle");
#endif
	set_cfg_err(0);
}


#ifdef NO_EXTRA_4K_HEAP		// with WPS
static const char WpsTag[] = "wps";
void wps_status_cb(wps_cb_status status)
{
	log_info(WpsTag,"cb status: %d", status);
	switch(status) {
	case WPS_CB_ST_SUCCESS:
		log_info(WpsTag,"sucess\n");
		wifi_wps_disable();
		wifi_station_connect();
		station_config sc;
		wifi_station_get_config(&sc);
		log_info(WpsTag,"ssid: %s, pass: %s\n",sc.ssid,sc.password);
		Config.mutable_station()->set_ssid((char*)sc.ssid);
		Config.mutable_station()->set_pass((char*)sc.password);
		Config.mutable_station()->set_activate(true);
		if (Config.has_softap() && !Config.accesspoint().activate())
			wifi_set_opmode_current(1);
		break;
	case WPS_CB_ST_FAILED:
		log_info(WpsTag,"FAILED\n");
		wifi_set_opmode_current(2);
		break;
	case WPS_CB_ST_TIMEOUT:
		log_info(WpsTag,"TIMEOUT\n");
		wifi_set_opmode_current(2);
		break;
	case WPS_CB_ST_WEP:
		log_info(WpsTag,"WEP\n");
		break;
	case WPS_CB_ST_UNK:
		log_info(WpsTag,"station unknown\n");
		wifi_wps_disable();
		wifi_set_opmode_current(2);
		break;
	default:
		abort();
	}
	esp_schedule();
	log_info(WpsTag,"continue\n");
}


void startWPS()
{
	log_info(WpsTag,"start");
	wifi_set_opmode_current(3);
	wifi_wps_disable();
	if (!wifi_wps_enable(WPS_TYPE_PBC)) {
		log_info(WpsTag,"enable failed");
		return;
	}
	if (!wifi_set_wps_cb((wps_st_cb_t) wps_status_cb)) {
		log_info(WpsTag,"cb failed");
		return;
	}
	if (!wifi_wps_start()) {
		log_info(WpsTag,"start failed");
		return;
	}

	log_info(WpsTag,"waiting for status update");
	// yield until WPS status update is available
	esp_yield();
}
#endif

