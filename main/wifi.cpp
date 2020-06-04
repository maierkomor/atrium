/*
 *  Copyright (C) 2018-2020, Thomas Maier-Komor
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

#include "alive.h"
#include "globals.h"
#include "log.h"
#include "settings.h"
#include "support.h"
#include "wifi.h"

#include <esp_err.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_smartconfig.h>
#include <smartconfig_ack.h>
#include <esp_wps.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#ifdef ESP8266
#include <apps/sntp/sntp.h>
#elif IDF_VERSION >= 32
#include <lwip/apps/sntp.h>	// >= v3.2
#else
#include <apps/sntp/sntp.h>	// <= v3.1
#endif

#include <string.h>
#include <sys/socket.h>

#define PINSTR "%c%c%c%c%c%c%c%c"
#define PIN2STR(a) a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]

EventGroupHandle_t WifiEvents;

sta_mode_t StationMode;

static const char TAG[] = "net";
static bool WifiStarted = false;
static uptime_t StationDownTS = 0;


static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
	switch (event->event_id) {
	case SYSTEM_EVENT_STA_START:
		log_info(TAG,"station start");
		esp_wifi_connect();
		StationMode = station_starting;
		break;
	case SYSTEM_EVENT_AP_START:
		log_info(TAG,"system event ap start");
		xEventGroupSetBits(WifiEvents, WIFI_UP);
		xEventGroupSetBits(WifiEvents, SOFTAP_UP);
		break;
	case SYSTEM_EVENT_AP_STOP:
		log_info(TAG,"system event ap stop");
		xEventGroupClearBits(WifiEvents, SOFTAP_UP);
		if (xEventGroupGetBits(WifiEvents) == WIFI_UP)
			xEventGroupClearBits(WifiEvents, WIFI_UP);
		break;
	case SYSTEM_EVENT_STA_STOP:
	case SYSTEM_EVENT_STA_LOST_IP:
		log_info(TAG,"station stopped");
		xEventGroupClearBits(WifiEvents, STATION_UP);
		if (xEventGroupGetBits(WifiEvents) == WIFI_UP)
			xEventGroupClearBits(WifiEvents, WIFI_UP);
#ifdef CONFIG_SNTP
		sntp_stop();
#endif
		StationMode = station_stopped;
		if (0 == StationDownTS)
			StationDownTS = uptime();
		break;
	case SYSTEM_EVENT_STA_CONNECTED:
		log_info(TAG,"station " MACSTR " connected",MAC2STR(event->event_info.sta_connected.mac));
		// TODO? station connects from esp
		break;
	case SYSTEM_EVENT_AP_STACONNECTED:
		log_info(TAG,"station " MACSTR " connected to esp",MAC2STR(event->event_info.sta_connected.mac));
		// TODO? station disconnects from esp
		break;
	case SYSTEM_EVENT_AP_STADISCONNECTED:
		log_info(TAG,"station " MACSTR " disconnected from esp",MAC2STR(event->event_info.sta_disconnected.mac));
		// TODO? station disconnects from esp
		break;
	case SYSTEM_EVENT_STA_GOT_IP:
		log_info(TAG,"station got IP %s",ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
		xEventGroupSetBits(WifiEvents, STATION_UP);
		xEventGroupSetBits(WifiEvents, WIFI_UP);
		if (Config.has_nodename())
			setHostname(Config.nodename().c_str());
		StationMode = station_connected;
		sntp_start();
		// mqtt handles restart itself
		StationDownTS = 0;
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		log_info(TAG,"disconnected");
		xEventGroupClearBits(WifiEvents, STATION_UP);
		if (xEventGroupGetBits(WifiEvents) == WIFI_UP)
			xEventGroupClearBits(WifiEvents, WIFI_UP);
		esp_wifi_connect();
		if (StationMode != station_starting)
			StationMode = station_disconnected;
		if (Config.has_station() && Config.station().activate())
			wifi_start_station(Config.station().ssid().c_str(),Config.station().pass().c_str());
		else if (esp_err_t e = esp_wifi_set_mode(WIFI_MODE_APSTA))
			log_error(TAG,"wifi set mode ap+sta: %s",esp_err_to_name(e));
		/*
		} else {
			wifi_mode_t m = WIFI_MODE_NULL;
			if (esp_err_t e = esp_wifi_get_mode(&m)) {
				log_error(TAG,"wifi_get_mode(): %s",esp_err_to_name(e));
				m = WIFI_MODE_APSTA;
			}
			if (m == WIFI_MODE_STA)
				m = WIFI_MODE_APSTA;
			else
				m = WIFI_MODE_NULL;
			if (esp_err_t e = esp_wifi_set_mode(m))
				log_error(TAG,"wifi set mode %d: 0x%x",m,e);
		}
		*/
		if (0 == StationDownTS) {
			StationDownTS = uptime();
		} else if (unsigned fot = Config.station2ap_time() * 1000) {
			if ((uptime() - StationDownTS > fot) && !wifi_softap_isup()) {
				log_info(TAG,"wifi down failover - activating AP");
				esp_wifi_set_mode(WIFI_MODE_APSTA);
			}
		}
		break;
	case SYSTEM_EVENT_AP_STAIPASSIGNED:
		log_info(TAG,"ap: station IP assigned");
		break;
	case SYSTEM_EVENT_AP_PROBEREQRECVED:
		log_info(TAG,"ap: probe request received");
		break;
#ifdef CONFIG_WPS
	case SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
		log_info(TAG,"WPS success");
		esp_wifi_wps_disable();
		if (esp_err_t e = esp_wifi_connect()) {
			log_error(TAG,"connect returned %s",esp_err_to_name(e));
		} else {
			wifi_config_t wifi_config;
			esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);
			WifiConfig *conf = Config.mutable_station();
			conf->set_ssid((char*)wifi_config.sta.ssid);
			conf->set_pass((char*)wifi_config.sta.password);
			conf->set_activate(true);
			log_info(TAG,"connected to: ssid=%s, pass=%s",wifi_config.sta.ssid,wifi_config.sta.password);
		}
		xEventGroupSetBits(WifiEvents,WPS_TERM);
		break;
	case SYSTEM_EVENT_STA_WPS_ER_FAILED:
		log_info(TAG,"WPS failed");
		if (esp_err_t e = esp_wifi_wps_disable()) 
			log_error(TAG,"WPS disable returned %s",esp_err_to_name(e));
		xEventGroupSetBits(WifiEvents,WPS_TERM);
		break;
	case SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
		log_info(TAG,"WPS timeout");
		if (esp_err_t e = esp_wifi_wps_disable()) 
			log_error(TAG,"wps disable returned %s",esp_err_to_name(e));
		xEventGroupSetBits(WifiEvents,WPS_TERM);
		break;
	case SYSTEM_EVENT_STA_WPS_ER_PIN:
		log_info(TAG,"WPS PIN: " PINSTR, PIN2STR(event->event_info.sta_er_pin.pin_code));
		xEventGroupSetBits(WifiEvents,WPS_TERM);
		break;
#endif
	default:
		log_warn(TAG,"unhandled wifi event %x",event->event_id);
		break;
	}
	return ESP_OK;
}


#ifdef CONFIG_SMARTCONFIG
static bool  SCstarted = false;

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
			if (esp_err_t e = esp_wifi_set_config(ESP_IF_WIFI_STA, cfg))
				log_warn(TAG,"sc wifi set config failed: %s",esp_err_to_name(e));
			if (esp_err_t e = esp_wifi_connect())
				log_warn(TAG,"sc connect failed: %s",esp_err_to_name(e));
		} else
			log_info(TAG,"smartconfig link, no data");
		break;
	case SC_STATUS_LINK_OVER:
		log_info(TAG,"smart config link over");
		if (pdata) {
#ifdef ESP8266
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
	default:
		break;
	}
}


int smartconfig_start()
{
	if (SCstarted == true) {
		log_error(TAG,"smartconfig already started");
		return 1;
	}
	if (Config.has_station() && !Config.station().ssid().empty()) {
		log_error(TAG,"smartconfig can only be started if station is not configured");
		return 1;
	}
	if (esp_err_t e = esp_wifi_set_mode(WIFI_MODE_STA))
		log_error(TAG,"wifi set station mode failure %s",esp_err_to_name(e));
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


void smartconfig_stop()
{
	if (SCstarted == false) {
		log_error(TAG,"illegal request to stop smartconfig while it is not running");
		return;
	}
	esp_smartconfig_stop();
	SCstarted = false;
}

bool smartconfig_running()
{
	return SCstarted;
}
#else
#define start_smart_config()
#endif


bool wifi_station_isup()
{
	return (xEventGroupGetBits(WifiEvents) & STATION_UP) == STATION_UP;
}


bool wifi_softap_isup()
{
	return (xEventGroupGetBits(WifiEvents) & SOFTAP_UP) == SOFTAP_UP;
}


bool eth_isup()
{
	return false;
}


uint32_t resolve_hostname(const char *h)
{
	if (!Config.has_domainname() || (0 != strchr(h,'.')))
		return resolve_fqhn(h);
	size_t hl = strlen(h);
	size_t dl = Config.domainname().size();
	char buf[strlen(h)+dl+2];
	memcpy(buf,h,hl);
	buf[hl] = '.';
	memcpy(buf+hl+1,Config.domainname().c_str(),dl+1);
	return resolve_fqhn(buf);
}

bool wifi_start_station(const char *ssid, const char *pass)
{
	//if (station_starting == StationMode)
		//return true;
	log_info(TAG,"wifi_start_station(%s,%s)",ssid,pass);
	wifi_mode_t m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_get_mode(&m))
		log_error(TAG,"wifi_get_mode(): %s",esp_err_to_name(e));
	wifi_mode_t nm = WIFI_MODE_STA;
	if ((m == WIFI_MODE_AP) || (m == WIFI_MODE_APSTA))
		nm = WIFI_MODE_APSTA;
	if (m != nm)  {
		if (esp_err_t e = esp_wifi_set_mode(nm))
			log_error(TAG,"wifi set mode %s",esp_err_to_name(e));
	}
	const WifiConfig &station = Config.station();
	if (station.has_mac() && (station.mac().size() == 6)) {
		if (esp_err_t e = esp_wifi_set_mac(ESP_IF_WIFI_STA,(const uint8_t *)station.mac().data()))
			log_warn(TAG,"error setting station mac: %s",esp_err_to_name(e));
	}
	wifi_config_t wifi_config;
	strcpy((char*)wifi_config.sta.ssid,ssid);
	strcpy((char*)wifi_config.sta.password,pass);
	wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
	wifi_config.sta.threshold.rssi = -127;
	wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	if (ESP_OK != esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config)) {
		log_warn(TAG,"unable to configure station");
		return false;
	}
	if (station.has_addr4() && station.has_netmask4() && station.has_gateway4()) {
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
		ipi.gw.addr = station.gateway4();
		tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);
		tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA,&ipi);
		initDns();
	} else if (station.has_addr4() || station.has_netmask4() || station.has_gateway4()) {
		log_warn(TAG,"static IP only works if address, netmask, and gateway are set");
	}
	if (ESP_OK != esp_wifi_start() ) {
		log_error(TAG,"error starting wifi");
		return false;
	}
	return true;
}


bool wifi_stop_station()
{
	if (!WifiStarted) {
		log_warn(TAG,"request to stop wifi station, while wifi offline");
		return false;
	}
	wifi_mode_t m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_get_mode(&m))
		log_error(TAG,"error getting wifi mode: %s",esp_err_to_name(e));
	if (m == WIFI_MODE_APSTA)
		m = WIFI_MODE_AP;
	else if (m == WIFI_MODE_STA)
		m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_set_mode(m))
		log_error(TAG,"error setting wifi mode %d: 0x%x",m,e);
	if (m == 0) {
		WifiStarted = false;
		return (ESP_OK == esp_wifi_stop());
	}
	log_info(TAG,"stopped station");
	return true;
}


bool wifi_start_softap(const char *ssid, const char *pass)
{
	log_info(TAG, "start WiFi soft-ap with SSID '%s', pass '%s'",ssid,pass);
	wifi_mode_t m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_get_mode(&m))
		log_error(TAG,"error getting wifi mode: %s",esp_err_to_name(e));
	const WifiConfig &softap = Config.softap();
	if (softap.has_mac() && (softap.mac().size() == 6)) {
		if (esp_err_t e = esp_wifi_set_mac(ESP_IF_WIFI_AP,(const uint8_t *)softap.mac().data()))
			log_warn(TAG,"error setting softap mac: %s",esp_err_to_name(e));
	}
	if (m == WIFI_MODE_NULL) {
		if (esp_err_t e = esp_wifi_set_mode(WIFI_MODE_AP))
			log_error(TAG,"error setting station mode %s",esp_err_to_name(e));
	} else if (m == WIFI_MODE_STA) {
		if (esp_err_t e = esp_wifi_set_mode(WIFI_MODE_APSTA))
			log_error(TAG,"error setting station+ap mode %s",esp_err_to_name(e));
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
	if (esp_err_t e = esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config))
		log_error(TAG,"error setting wifi config %s",esp_err_to_name(e));
	if (!WifiStarted) {
		if (esp_err_t e = esp_wifi_start())
			log_error(TAG,"error starting wifi %s",esp_err_to_name(e));
		else
			WifiStarted = true;
	}
	xEventGroupSetBits(WifiEvents, SOFTAP_UP);
	if (xEventGroupGetBits(WifiEvents) == WIFI_UP)
		xEventGroupSetBits(WifiEvents, WIFI_UP);
	log_info(TAG,"started softap");
	return true;
}


bool wifi_stop_softap()
{
	if (!WifiStarted) {
		log_warn(TAG,"request to stop wifi station, while wifi offline");
		return  false;
	}
	wifi_mode_t m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_get_mode(&m))
		log_error(TAG,"error getting wifi mode: %s",esp_err_to_name(e));
	if (m == WIFI_MODE_APSTA)
		m = WIFI_MODE_AP;
	else if (m == WIFI_MODE_AP)
		m = WIFI_MODE_NULL;
	if (esp_err_t e = esp_wifi_set_mode(m))
		log_error(TAG,"error setting wifi mode %d: 0x%x",m,e);
	if (m == WIFI_MODE_NULL) {
		WifiStarted = false;
		return ESP_OK == esp_wifi_stop();
	}
	log_info(TAG,"stopped softap");
	xEventGroupClearBits(WifiEvents, SOFTAP_UP);
	return true;
}


void wifi_wait()
{
	xEventGroupWaitBits(WifiEvents,WIFI_UP,0,0,portMAX_DELAY);
}


void wifi_wait_station()
{
	xEventGroupWaitBits(WifiEvents,STATION_UP,0,0,portMAX_DELAY);
}


#ifdef CONFIG_WPS
void wifi_wps_start()
{
	log_info(TAG,"starting wps");
	wifi_mode_t m;
	esp_wifi_get_mode(&m);
	if (m != WIFI_MODE_STA) {
		if (esp_err_t e = esp_wifi_set_mode(WIFI_MODE_STA))
			log_warn(TAG,"set wifi mode %d: 0x%x",m,e);
		if (esp_err_t e = esp_wifi_start())
			log_warn(TAG,"error starting wifi: %s",esp_err_to_name(e));
	}
	esp_wps_config_t config;
	bzero(&config,sizeof(config));
	config.wps_type = WPS_TYPE_PBC;
#ifdef ESP32
	config.crypto_funcs = &g_wifi_default_wps_crypto_funcs;
#endif
	strcpy(config.factory_info.manufacturer,"make");
	strcpy(config.factory_info.model_number,"0");
	strcpy(config.factory_info.model_name,"proto0");
	strcpy(config.factory_info.device_name,"esp_proto");

	xEventGroupClearBits(WifiEvents,WPS_TERM);
	if (esp_err_t e = esp_wifi_wps_enable(&config)) {
		log_error(TAG,"wps enable failed with error %s",esp_err_to_name(e));
		return;
	}
	if (esp_err_t e = esp_wifi_wps_start(0)) {
		log_error(TAG,"wps start failed with error %s",esp_err_to_name(e));
		return;
	}
	log_info(TAG,"wps started, waiting to complete");
	xEventGroupWaitBits(WifiEvents,WPS_TERM,0,0,portMAX_DELAY);
}
#endif


extern "C"
bool wifi_setup()
{
	log_info(TAG,"init");
	tcpip_adapter_init();
	StationMode = station_disconnected;
	WifiEvents = xEventGroupCreate();
	xEventGroupClearBits(WifiEvents, 0xffffffff);
	if (esp_err_t e = esp_event_loop_init(wifi_event_handler,0)) {
		log_error(TAG,"esp_event_loop_init failed: %s",esp_err_to_name(e));
		return false;
	}
#if defined ESP8266 && IDF_VERSION > 32
	// for esp8266 post v3.2
	wifi_init_config_t cfg;
	cfg.event_handler = &esp_event_send;
	cfg.osi_funcs = NULL;
	cfg.ampdu_rx_enable = WIFI_AMPDU_RX_ENABLED;
	cfg.qos_enable = WIFI_QOS_ENABLED;
	cfg.rx_ampdu_buf_len = WIFI_AMPDU_RX_AMPDU_BUF_LEN;
	cfg.rx_ampdu_buf_num = WIFI_AMPDU_RX_AMPDU_BUF_NUM;
	cfg.amsdu_rx_enable = WIFI_AMSDU_RX_ENABLED;
	cfg.rx_ba_win = WIFI_AMPDU_RX_BA_WIN;
	cfg.rx_max_single_pkt_len = WIFI_RX_MAX_SINGLE_PKT_LEN;
	cfg.rx_buf_len = WIFI_HW_RX_BUFFER_LEN;
	cfg.rx_buf_num = CONFIG_ESP8266_WIFI_RX_BUFFER_NUM;
	cfg.left_continuous_rx_buf_num = CONFIG_ESP8266_WIFI_LEFT_CONTINUOUS_RX_BUFFER_NUM;
	cfg.rx_pkt_num = CONFIG_ESP8266_WIFI_RX_PKT_NUM;
	cfg.tx_buf_num = CONFIG_ESP8266_WIFI_TX_PKT_NUM;
	cfg.nvs_enable = WIFI_NVS_ENABLED;
	cfg.nano_enable = 0;
	cfg.magic = WIFI_INIT_CONFIG_MAGIC;
#else
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
#endif
	if (esp_err_t e = esp_wifi_init(&cfg)) {
		log_error(TAG,"esp_wifi_init failed: %s",esp_err_to_name(e));
		return false;
	}
	uint8_t mac[6];
	if (ESP_OK == esp_wifi_get_mac(ESP_IF_WIFI_AP,mac)) {
		log_info(TAG,"soft-ap MAC: %02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
	} else {
		log_info(TAG,"no soft-ap MAC");
	}
	if (ESP_OK == esp_wifi_get_mac(ESP_IF_WIFI_STA,mac)) {
		log_info(TAG,"station MAC: %02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
	} else {
		log_info(TAG,"no station MAC");
	}
	if (esp_err_t e = esp_wifi_set_storage(WIFI_STORAGE_RAM)) {
		log_error(TAG,"esp_wifi_set_storage failed: %s",esp_err_to_name(e));
		return false;
	}
	esp_wifi_set_mode(WIFI_MODE_NULL);
	if (esp_err_t e = esp_wifi_start()) {
		log_error(TAG,"error starting wifi: %s",esp_err_to_name(e));
		return false;
	}
	return true;
}
