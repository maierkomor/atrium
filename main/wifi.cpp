/*
 *  Copyright (C) 2018-2022, Thomas Maier-Komor
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
#include "log.h"
#include "netsvc.h"
#include "settings.h"
#include "swcfg.h"
#include "wifi.h"

#include <esp_err.h>
#include <esp_wifi.h>
#include <esp_smartconfig.h>
#include <smartconfig_ack.h>
#include <esp_wps.h>
#if IDF_VERSION < 40
#include <esp_event_loop.h>
#endif
#if IDF_VERSION >= 44
#include <esp_event_legacy.h>
#endif

#include <lwip/ip_addr.h>

#include <string.h>
#include <sys/socket.h>

#define PINSTR "%c%c%c%c%c%c%c%c"
#define PIN2STR(a) a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]

#define STATUS_WIFI_UP		(1<<0)
#define STATUS_STATION_UP	(1<<1)
#define STATUS_SOFTAP_UP	(1<<2)
#define STATUS_WPS_TERM		(1<<3)
#define STATUS_WIFI_FAIL	(1<<4)

sta_mode_t StationMode;

#define TAG MODULE_WLAN
static bool WifiStarted = false;
static uint8_t Status = 0;
static uptime_t StationDownTS = 0;
event_t StationDownEv = 0, StationUpEv = 0;
static event_t SysWifiEv = 0;

extern "C" {
esp_err_t system_event_ap_start_handle_default(system_event_t *);	// IDF
esp_err_t system_event_ap_stop_handle_default(system_event_t *);	// IDF
esp_err_t system_event_sta_start_handle_default(system_event_t *);	// IDF
esp_err_t system_event_sta_connected_handle_default(system_event_t *);	// IDF
esp_err_t system_event_sta_disconnected_handle_default(system_event_t *);	// IDF
}

#if IDF_VERSION >= 40
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	static uint8_t WifiRetry = 0;
//	log_dbug(TAG,"esp event %s/%x",event_base,event_id);
	if (event_base == WIFI_EVENT) {
		if (event_id == WIFI_EVENT_STA_START) {
			esp_wifi_connect();
			WifiRetry = 0;
			StationMode = station_starting;
		} else if (event_id == WIFI_EVENT_STA_CONNECTED) {
#if defined CONFIG_LWIP_IPV6 || defined ESP32
			if (esp_err_t e = tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA))
				log_warn(TAG,"create IPv6 linklocal on station: %s",esp_err_to_name(e));
#endif
		} else if (event_id == WIFI_EVENT_STA_STOP) {
			if (0 == StationDownTS)
				StationDownTS = uptime();
			Status &= ~STATUS_WIFI_UP;
			StationMode = station_stopped;
		} else if (event_id == WIFI_EVENT_STA_CONNECTED) {
			log_info(TAG,"station connected");
		} else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
			if (WifiRetry < 255) {
				esp_wifi_connect();
				WifiRetry++;
				log_info(TAG,"retry to connect to AP");
			} else {
				log_info(TAG,"WiFi fail");
				Status |= STATUS_WIFI_FAIL;
			}
			StationMode = station_stopped;
		} else if (event_id == WIFI_EVENT_WIFI_READY) {
			log_info(TAG,"ready");
		} else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
			log_info(TAG,"station disconnected");
		} else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
			log_info(TAG,"AP station disconnected");
		} else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
			log_info(TAG,"AP station connected");
		} else if (event_id == WIFI_EVENT_SCAN_DONE) {
			log_info(TAG,"scan done");
		} else 
			log_info(TAG,"unhandled WiFi event %x",event_id);
	} else if (event_base == IP_EVENT) {
		if (event_id == IP_EVENT_STA_GOT_IP) {
			ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
			ip4_addr_t ip;
			ip.addr = event->ip_info.ip.addr;
			log_info(TAG, "got IPv4 %s", ip4addr_ntoa(&ip));
			WifiRetry = 0;
			Status |= STATUS_WIFI_UP | STATUS_STATION_UP;
			StationMode = station_connected;
			event_trigger(StationUpEv);
		} else if (event_id == IP_EVENT_GOT_IP6) {
			ip_event_got_ip6_t* event = (ip_event_got_ip6_t*) event_data;
			ip6_addr_t ip;
			memcpy(&ip,event->ip6_info.ip.addr,sizeof(ip));
			if (ip6_addr_islinklocal(&ip))
				memcpy(&IP6LL,&ip,sizeof(IP6LL));
			else
				memcpy(&IP6G,&ip,sizeof(IP6LL));
			log_info(TAG, "got IPv6 %s", ip6addr_ntoa(&ip));
			WifiRetry = 0;
			Status |= STATUS_WIFI_UP | STATUS_STATION_UP;
			StationMode = station_connected;
			event_trigger(StationUpEv);
		} else if (event_id == IP_EVENT_STA_LOST_IP) {
			log_info(TAG, "lost IP");
			if (0 == StationDownTS)
				StationDownTS = uptime();
			Status &= ~STATUS_STATION_UP;
			StationMode = station_stopped;
		} else if (event_id == IP_EVENT_AP_STAIPASSIGNED) {
			log_info(TAG, "AP assigend IP to station");
		} else 
			log_info(TAG,"unhandled IP event %x",event_id);
	} else
		log_info(TAG,"unhandled esp event %x/%x",event_base,event_id);
}

#else	// IDF_VERSION < 40

static esp_err_t wifi_event_handler(system_event_t *event)
{
	switch (event->event_id) {
	case SYSTEM_EVENT_WIFI_READY:
		log_info(TAG,"ready");
		break;
	case SYSTEM_EVENT_STA_START:
		log_info(TAG,"station start");
		esp_wifi_connect();
		StationMode = station_starting;
		system_event_sta_start_handle_default(event);	// IDF
		break;
	case SYSTEM_EVENT_AP_START:
		log_info(TAG,"AP start");
		Status |= STATUS_WIFI_UP | STATUS_SOFTAP_UP;
		system_event_ap_start_handle_default(event);	// IDF
		break;
	case SYSTEM_EVENT_AP_STOP:
		log_info(TAG,"AP stop");
		Status &= ~(STATUS_WIFI_UP | STATUS_SOFTAP_UP);
		system_event_ap_stop_handle_default(event);	// IDF
		break;
	case SYSTEM_EVENT_STA_STOP:
	case SYSTEM_EVENT_STA_LOST_IP:
		log_info(TAG,"station stop");
		Status &= ~(STATUS_WIFI_UP | STATUS_STATION_UP);
		StationMode = station_stopped;
		if (0 == StationDownTS)
			StationDownTS = uptime();
		event_trigger(StationDownEv);
		break;
	case SYSTEM_EVENT_STA_CONNECTED:
		log_info(TAG,"station " MACSTR " connected",MAC2STR(event->event_info.sta_connected.mac));
		system_event_sta_connected_handle_default(event);	// IDF
#if defined CONFIG_LWIP_IPV6 || defined ESP32
		if (esp_err_t e = tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA))
			log_warn(TAG,"create IPv6 linklocal on station: %s",esp_err_to_name(e));
#endif
		break;
	case SYSTEM_EVENT_AP_STACONNECTED:
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
		log_info(TAG,"station " MACSTR " connected to esp",MAC2STR(event->event_info.sta_connected.mac));
#endif
		// TODO? station disconnects from esp
		break;
	case SYSTEM_EVENT_AP_STADISCONNECTED:
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
		log_info(TAG,"station " MACSTR " disconnected from esp",MAC2STR(event->event_info.sta_disconnected.mac));
#endif
		// TODO? station disconnects from esp
		break;
#if defined CONFIG_LWIP_IPV6 || defined ESP32
	case SYSTEM_EVENT_GOT_IP6:
#if LWIP_IPV6
		{
			ip6_addr_t ip;
			assert(sizeof(ip) == sizeof(event->event_info.got_ip6.ip6_info.ip));
			memcpy(&ip,event->event_info.got_ip6.ip6_info.ip.addr,sizeof(ip));
			log_info(TAG,"station got IP %s",ip6addr_ntoa(&ip));
			if (ip6_addr_islinklocal(&ip))
				memcpy(&IP6LL,&event->event_info.got_ip6.ip6_info.ip,sizeof(IP6LL));
			else
				memcpy(&IP6G,&event->event_info.got_ip6.ip6_info.ip,sizeof(IP6LL));
		}
#endif
		break;
#endif
	case SYSTEM_EVENT_STA_GOT_IP:
		{
			Status |= STATUS_STATION_UP | STATUS_WIFI_UP;
			StationMode = station_connected;
			ip4_addr_t ip;
			ip.addr = event->event_info.got_ip.ip_info.ip.addr;
			log_info(TAG,"station got IP %s",ip4addr_ntoa(&ip));
			void *arg = malloc(sizeof(event->event_info.got_ip.ip_info.ip.addr));
			memcpy(arg,&event->event_info.got_ip.ip_info.ip.addr,sizeof(event->event_info.got_ip.ip_info.ip.addr));
			event_trigger_arg(StationUpEv,arg);
			StationDownTS = 0;
		}
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		log_info(TAG,"disconnected");
		system_event_sta_disconnected_handle_default(event);	// IDF
		Status &= ~(STATUS_STATION_UP | STATUS_WIFI_UP);
		esp_wifi_connect();
		if (StationMode != station_starting)
			StationMode = station_disconnected;
		if (Config.has_station() && Config.station().activate())
			wifi_start_station(Config.station().ssid().c_str(),Config.station().pass().c_str());
		else if (esp_err_t e = esp_wifi_set_mode(WIFI_MODE_APSTA))
			log_warn(TAG,"wifi set mode ap+sta: %s",esp_err_to_name(e));
		/*
		} else {
			wifi_mode_t m = WIFI_MODE_NULL;
			if (esp_err_t e = esp_wifi_get_mode(&m)) {
				log_warn(TAG,"wifi_get_mode(): %s",esp_err_to_name(e));
				m = WIFI_MODE_APSTA;
			}
			if (m == WIFI_MODE_STA)
				m = WIFI_MODE_APSTA;
			else
				m = WIFI_MODE_NULL;
			if (esp_err_t e = esp_wifi_set_mode(m))
				log_warn(TAG,"wifi set mode %d: 0x%x",m,e);
		}
		*/
		if (0 == StationDownTS) {
			StationDownTS = uptime();
		} else if (unsigned fot = Config.station2ap_time() * 1000) {
			if ((uptime() - StationDownTS > fot) && !wifi_softap_isup()) {
				log_info(TAG,"station down, failover to AP");
				esp_wifi_set_mode(WIFI_MODE_APSTA);
			}
		}
		break;
	case SYSTEM_EVENT_AP_STAIPASSIGNED:
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
		log_info(TAG,"AP: assigned IP");
#endif
		break;
	case SYSTEM_EVENT_AP_PROBEREQRECVED:
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
		log_info(TAG,"AP: probe request");
#endif
		break;
#ifdef CONFIG_WPS
	case SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
		log_info(TAG,"WPS success");
		esp_wifi_wps_disable();
		if (esp_err_t e = esp_wifi_connect()) {
			log_warn(TAG,"connect returned %s",esp_err_to_name(e));
		} else {
			wifi_config_t wifi_config;
			esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
			WifiConfig *conf = Config.mutable_station();
			conf->set_ssid((char*)wifi_config.sta.ssid);
			conf->set_pass((char*)wifi_config.sta.password);
			conf->set_activate(true);
			log_info(TAG,"connected to: ssid=%s",wifi_config.sta.ssid);
		}
		Status |= STATUS_WPS_TERM;
		break;
	case SYSTEM_EVENT_STA_WPS_ER_FAILED:
		log_info(TAG,"WPS failed");
		if (esp_err_t e = esp_wifi_wps_disable()) 
			log_warn(TAG,"WPS disable: %s",esp_err_to_name(e));
		break;
	case SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
		log_info(TAG,"WPS timeout");
		if (esp_err_t e = esp_wifi_wps_disable()) 
			log_warn(TAG,"wps disable: %s",esp_err_to_name(e));
		Status |= STATUS_WPS_TERM;
		break;
	case SYSTEM_EVENT_STA_WPS_ER_PIN:
//		log_info(TAG,"WPS PIN: " PINSTR, PIN2STR(event->event_info.sta_er_pin.pin_code));
		log_info(TAG,"WPS PIN: %.8s",event->event_info.sta_er_pin.pin_code);
		Status |= STATUS_WPS_TERM;
		break;
#endif
	default:
		log_warn(TAG,"unhandled wifi event %x",event->event_id);
		break;
	}
	return ESP_OK;
}
#endif // IDF_VERSION >= 40


#ifdef CONFIG_SMARTCONFIG
static bool  SCstarted = false;

#if IDF_VERSION >= 40
int smartconfig_start()
{
	smartconfig_start_config_t cfg;
	bzero(&cfg,sizeof(cfg));
	esp_smartconfig_start(&cfg);
	return 0;
}
#else
static void sc_callback(smartconfig_status_t status, void *pdata)
{
	switch (status) {
	case SC_STATUS_WAIT:
		log_info(TAG,"smart config wait");
		break;
	case SC_STATUS_FIND_CHANNEL:
		log_info(TAG,"smart config find channel");
		break;
	case SC_STATUS_GETTING_SSID_PSWD:
		log_info(TAG,"smart config get ssid/pass");
		break;
	case SC_STATUS_LINK:
		if (pdata) {
			wifi_config_t *cfg = (wifi_config_t *) pdata;
			log_info(TAG,"smartconfig link ssid: %s, pass: %s"
				, cfg->sta.ssid
				, cfg->sta.password);
			WifiConfig *conf = Config.mutable_station();
			conf->set_ssid((char*)cfg->sta.ssid);
			conf->set_pass((char*)cfg->sta.password);
			conf->set_activate(true);
			if (esp_err_t e = esp_wifi_disconnect())
				log_warn(TAG,"sc wifi disconnect failed: %s",esp_err_to_name(e));
			if (esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, cfg))
				log_warn(TAG,"sc wifi set config failed: %s",esp_err_to_name(e));
			if (esp_err_t e = esp_wifi_connect())
				log_warn(TAG,"sc connect failed: %s",esp_err_to_name(e));
		} else
			log_info(TAG,"smartconfig link, no data");
		break;
	case SC_STATUS_LINK_OVER:
		log_info(TAG,"smart config link over");
		if (pdata) {
#ifdef CONFIG_IDF_TARGET_ESP8266
			sc_callback_data_t *sc_callback_data = (sc_callback_data_t *)pdata;
			switch (sc_callback_data->type) {
			case SC_ACK_TYPE_ESPTOUCH:
				log_info(TAG,"ip: %d.%d.%d.%d", sc_callback_data->ip[0], sc_callback_data->ip[1], sc_callback_data->ip[2], sc_callback_data->ip[3]);
				break;
			case SC_ACK_TYPE_AIRKISS:
				log_info(TAG,"smart config link over: airkiss");
				break;
			default:
				log_info(TAG,"smartconfig: link over type error");
				break;
			}
#else
			const uint8_t *ip = (const uint8_t*)pdata;
			log_info(TAG,"IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
#endif
		}
		esp_smartconfig_stop();
		SCstarted = false;
		break;
#if IDF_VERSION >= 40
	case SC_EVENT_GOT_SSID_PSWD:
		{
			log_info(TAG,"SC: ssid+pass");
			smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)pdata;
			wifi_config_t wifi_config;
			uint8_t ssid[33], password[65], rvd_data[33];
			bzero(&wifi_config, sizeof(wifi_config_t));
			memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
			memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
			wifi_config.sta.bssid_set = evt->bssid_set;
			if (wifi_config.sta.bssid_set == true)
				memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
			memcpy(ssid, evt->ssid, sizeof(evt->ssid));
			memcpy(password, evt->password, sizeof(evt->password));
			log_dbug(TAG, "ssid: %s", ssid);
			log_dbug(TAG, "pass: %s", password);
			if (evt->type == SC_TYPE_ESPTOUCH_V2) {
				if (esp_err_t e = esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)))
					log_warn(TAG,"SC get rvd: %s",esp_err_to_name(e));
				log_hex(TAG,rv_data,sizeof(rv_data),"rvd:");
			}
			if (esp_err_t e = esp_wifi_disconnect())
				log_warn(TAG,"wifi disconnect: %s",esp_err_to_name(e));
			if (esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &wifi_config))
				log_warn(TAG,"wifi set config: %s",esp_err_to_name(e));
			esp_wifi_connect();
		}
		break;
#endif
	default:
		break;
	}
}


int smartconfig_start()
{
	if (SCstarted == true) {
		log_warn(TAG,"smartconfig already started");
		return 1;
	}
	if (Config.has_station() && !Config.station().ssid().empty()) {
		log_warn(TAG,"smartconfig can only be started if station is not configured");
		return 1;
	}
	if (esp_err_t e = esp_wifi_set_mode(WIFI_MODE_STA))
		log_warn(TAG,"wifi set station mode failure %s",esp_err_to_name(e));
	esp_wifi_disconnect();
	if (esp_err_t e = esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS)) {
		log_warn(TAG,"smartconfig set type failure %s",esp_err_to_name(e));
		return 1;
	} else if (esp_err_t e = esp_smartconfig_start(sc_callback,1)) {
		log_warn(TAG,"smartconfig start failure %s",esp_err_to_name(e));
		return 1;
	}
	log_info(TAG,"smartconfig started");
	SCstarted = true;
	return 0;
}
#endif	// IDF_VERSION


void smartconfig_stop()
{
	if (SCstarted == false) {
		log_warn(TAG,"not started");
		return;
	}
	esp_smartconfig_stop();
	SCstarted = false;
}

bool smartconfig_running()
{
	return SCstarted;
}
#endif	// CONFIG_SMARTCONFIG


bool wifi_station_isup()
{
	return (Status & STATUS_STATION_UP) == STATUS_STATION_UP;
}


bool wifi_softap_isup()
{
	return (Status & STATUS_SOFTAP_UP) == STATUS_SOFTAP_UP;
}


bool eth_isup()
{
	return false;
}


bool wifi_start_station(const char *ssid, const char *pass)
{
	//if (station_starting == StationMode)
		//return true;
//	if (ESP_OK != esp_wifi_start())
//		log_warn(TAG,"error starting wifi");
	log_info(TAG,"wifi_start_station(%s,%s)",ssid,pass);
	wifi_mode_t m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_get_mode(&m))
		log_warn(TAG,"wifi_get_mode(): %s",esp_err_to_name(e));
	wifi_mode_t nm = WIFI_MODE_STA;
	if ((m == WIFI_MODE_AP) || (m == WIFI_MODE_APSTA))
		nm = WIFI_MODE_APSTA;
	if (m != nm)  {
		if (esp_err_t e = esp_wifi_set_mode(nm))
			log_warn(TAG,"wifi set mode %s",esp_err_to_name(e));
	}
	const WifiConfig &station = Config.station();
	if (station.has_mac() && (station.mac().size() == 6)) {
		if (esp_err_t e = esp_wifi_set_mac(WIFI_IF_STA,(const uint8_t *)station.mac().data()))
			log_warn(TAG,"error setting station mac: %s",esp_err_to_name(e));
	}
	wifi_config_t wifi_config;
	bzero(&wifi_config,sizeof(wifi_config));
	strcpy((char*)wifi_config.sta.ssid,ssid);
	strcpy((char*)wifi_config.sta.password,pass);
	wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
//	wifi_config.sta.threshold.rssi = -127;
	wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
#if IDF_VERSION >= 40
	wifi_config.sta.pmf_cfg.capable = true;
	wifi_config.sta.pmf_cfg.required = false;
#endif
	if (ESP_OK != esp_wifi_set_config(WIFI_IF_STA, &wifi_config)) {
		log_warn(TAG,"unable to configure station");
		return false;
	}
	if (station.has_addr4() && station.has_netmask4()) {
		tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);
		tcpip_adapter_ip_info_t ipi;
		bzero(&ipi,sizeof(ipi));
		ipi.ip.addr = station.addr4();
		uint32_t nm = 0;
		uint8_t nmb = station.netmask4();
		while (nmb--) {
			nm <<= 1;
			nm |= 1;
		}
		ipi.netmask.addr = (uint32_t)nm;
		if (station.has_gateway4())
			ipi.gw.addr = station.gateway4();
		tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);
		tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA,&ipi);
		initDns();
	} else if (station.has_addr4() || station.has_netmask4()) {
		log_warn(TAG,"static IP needs address and netmask");
	}
	if (esp_err_t e = esp_wifi_start()) {
		log_warn(TAG,"start: %s",esp_err_to_name(e));
		return false;
	}
	return true;
}


bool wifi_stop_station()
{
	wifi_mode_t m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_get_mode(&m))
		log_warn(TAG,"error getting wifi mode: %s",esp_err_to_name(e));
	if (m == WIFI_MODE_APSTA)
		m = WIFI_MODE_AP;
	else if (m == WIFI_MODE_STA)
		m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_set_mode(m))
		log_warn(TAG,"error setting wifi mode %d: 0x%x",m,e);
	if (m == 0) {
		WifiStarted = false;
		return (ESP_OK == esp_wifi_stop());
	}
	log_info(TAG,"station down");
	return true;
}


bool wifi_start_softap(const char *ssid, const char *pass)
{
	if (ssid == 0)
		ssid = Config.nodename().c_str();
	if (pass == 0)
		pass = "";
	log_info(TAG, "start AP: SSID '%s', pass '%s'",ssid,pass);
	wifi_mode_t m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_get_mode(&m))
		log_warn(TAG,"get wifi mode: %s",esp_err_to_name(e));
	const WifiConfig &softap = Config.softap();
	if (softap.has_mac() && (softap.mac().size() == 6)) {
		if (esp_err_t e = esp_wifi_set_mac(WIFI_IF_AP,(const uint8_t *)softap.mac().data()))
			log_warn(TAG,"error setting softap mac: %s",esp_err_to_name(e));
	}
	wifi_config_t wifi_config;
	memset(&wifi_config,0,sizeof(wifi_config_t));
	strcpy((char*)wifi_config.ap.ssid,ssid);
	strcpy((char*)wifi_config.ap.password,pass);
	wifi_config.ap.ssid_len = strlen(ssid);
	wifi_config.ap.channel = 0;
	wifi_config.ap.authmode = WIFI_AUTH_OPEN;
	wifi_config.ap.ssid_hidden = 0;
	wifi_config.ap.max_connection = 4;
	wifi_config.ap.beacon_interval = 300;
	if (esp_err_t e = esp_wifi_set_config(WIFI_IF_AP, &wifi_config))
		log_warn(TAG,"set wifi config %s",esp_err_to_name(e));
	if (m == WIFI_MODE_NULL) {
		if (esp_err_t e = esp_wifi_set_mode(WIFI_MODE_AP))
			log_warn(TAG,"set AP mode %s",esp_err_to_name(e));
	} else if (m == WIFI_MODE_STA) {
		if (esp_err_t e = esp_wifi_set_mode(WIFI_MODE_APSTA))
			log_warn(TAG,"set station+ap mode %s",esp_err_to_name(e));
	}
	if (!WifiStarted) {
		if (esp_err_t e = esp_wifi_start())
			log_warn(TAG,"start wifi %s",esp_err_to_name(e));
		else
			WifiStarted = true;
	}
	Status |= STATUS_WIFI_UP | STATUS_SOFTAP_UP;
	log_info(TAG,"started softap");
	return true;
}


bool wifi_stop_softap()
{
	if (!WifiStarted) {
		log_warn(TAG,"already offline");
		return  false;
	}
	wifi_mode_t m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_get_mode(&m))
		log_warn(TAG,"get wifi mode: %s",esp_err_to_name(e));
	if (m == WIFI_MODE_APSTA)
		m = WIFI_MODE_AP;
	else if (m == WIFI_MODE_AP)
		m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_set_mode(m))
		log_warn(TAG,"set wifi mode %d: 0x%x",m,e);
	if (m == WIFI_MODE_NULL) {
		WifiStarted = false;
		return ESP_OK == esp_wifi_stop();
	}
	log_info(TAG,"AP down");
	Status |= STATUS_SOFTAP_UP;
	return true;
}


#ifdef CONFIG_WPS
void wifi_wps_start(void *)
{
//	if (WifiEvents == 0)
//		wifi_setup();
	log_info(TAG,"starting wps");
	wifi_mode_t m;
	esp_wifi_get_mode(&m);
	if (m != WIFI_MODE_STA) {
		if (esp_err_t e = esp_wifi_set_mode(WIFI_MODE_STA))
			log_warn(TAG,"set station mode: %d",e);
		if (esp_err_t e = esp_wifi_start())
			log_warn(TAG,"start wifi: %s",esp_err_to_name(e));
	}
	esp_wps_config_t config;
	bzero(&config,sizeof(config));
	config.wps_type = WPS_TYPE_PBC;
#ifdef CONFIG_IDF_TARGET_ESP32
#if IDF_VERSION < 44
	config.crypto_funcs = &g_wifi_default_wps_crypto_funcs;
#endif
#endif
	const SystemConfig &cfg = HWConf.system();
	if (cfg.has_manufacturer())
		strcpy(config.factory_info.manufacturer,cfg.manufacturer().c_str());
	else
		strcpy(config.factory_info.manufacturer,"no manufacturer");
	if (cfg.has_model_number())
		strcpy(config.factory_info.model_number,cfg.model_number().c_str());
	else
		strcpy(config.factory_info.model_number,"0");
	if (cfg.has_model_name())
		strcpy(config.factory_info.model_name,cfg.model_name().c_str());
	else
		strcpy(config.factory_info.model_name,"no model");
	if (Config.has_nodename())
		strcpy(config.factory_info.device_name,Config.nodename().c_str());
	else
		strcpy(config.factory_info.device_name,"unnamed");

	Status &= ~STATUS_WPS_TERM;
	if (esp_err_t e = esp_wifi_wps_enable(&config)) {
		log_warn(TAG,"wps enable failed with error %s",esp_err_to_name(e));
		return;
	}
	if (esp_err_t e = esp_wifi_wps_start(0)) {
		log_warn(TAG,"wps start failed with error %s",esp_err_to_name(e));
		return;
	}
	log_info(TAG,"wps started, waiting to complete");
}

#endif


#if IDF_VERSION < 40
extern "C"
esp_err_t esp_event_send(system_event_t *ev)
{
	log_dbug(TAG,"send wifi event");
	void *arg = malloc(sizeof(system_event_t));
	memcpy(arg,ev,sizeof(system_event_t));
	event_trigger_arg(SysWifiEv,arg);
	return 0;
}
#endif


extern "C"
void esp_event_process(void *arg)
{
	log_dbug(TAG,"process wifi event");
#if IDF_VERSION < 40
	wifi_event_handler((system_event_t *) arg);
#endif
}


int wifi_setup()
{
	if (Status & STATUS_WIFI_UP)
		return 1;
	Status = STATUS_WIFI_UP;
	log_info(TAG,"init");
#if LWIP_IPV6
	bzero(&IP6G,sizeof(IP6G));
	bzero(&IP6LL,sizeof(IP6LL));
#endif
	StationMode = station_disconnected;
	StationUpEv = event_id("wifi`station_up");
	StationDownEv = event_id("wifi`station_down");
	SysWifiEv = event_register("system`procwifi");
	Action *pw = action_add("system!procwifi",esp_event_process,0,0);
	event_callback(SysWifiEv,pw);
#if IDF_VERSION >= 40
	esp_netif_init();
	esp_event_loop_create_default();
	esp_netif_create_default_wifi_sta();
	esp_wifi_set_default_wifi_sta_handlers();
	if (esp_err_t e = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, 0))
		log_warn(TAG,"set wifi handler: %s",esp_err_to_name(e));
	if (esp_err_t e = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, 0))
		log_warn(TAG,"set got-IP handler: %s",esp_err_to_name(e));
	if (esp_err_t e = esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &event_handler, 0))
		log_warn(TAG,"set got-IP handler: %s",esp_err_to_name(e));
	if (esp_err_t e = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &event_handler, 0))
		log_warn(TAG,"set lost-IP handler: %s",esp_err_to_name(e));
	if (esp_err_t e = esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &event_handler, 0))
		log_warn(TAG,"set AP IP-assigned handler: %s",esp_err_to_name(e));
#else
	tcpip_adapter_init();
#endif

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	if (esp_err_t e = esp_wifi_init(&cfg)) {
		log_warn(TAG,"wifi init: %s",esp_err_to_name(e));
		return 1;
	}
	uint8_t mac[6];
	if (ESP_OK == esp_wifi_get_mac(WIFI_IF_AP,mac)) {
		log_info(TAG,"AP MAC: %02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
	} else {
		log_info(TAG,"no AP MAC");
	}
	if (ESP_OK == esp_wifi_get_mac(WIFI_IF_STA,mac)) {
		log_info(TAG,"station MAC: %02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
	} else {
		log_info(TAG,"no station MAC");
	}
	if (esp_err_t e = esp_wifi_set_storage(WIFI_STORAGE_RAM)) {
		log_warn(TAG,"esp_wifi_set_storage failed: %s",esp_err_to_name(e));
		return 1;
	}
	esp_wifi_set_mode(WIFI_MODE_NULL);
	return 0;
}
