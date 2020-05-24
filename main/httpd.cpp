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

#ifdef CONFIG_HTTP

#include "HttpServer.h"
#include "HttpReq.h"
#include "HttpResp.h"
#include "actions.h"
#include "globals.h"
#include "httpd.h"
#include "inetd.h"
#include "log.h"
#include "mem_term.h"
#include "romfs.h"
#include "profiling.h"
#include "settings.h"
#include "shell.h"
#include "strstream.h"
#include "support.h"
#include "webcam.h"

#include "memfiles.h"	// generated automatically

#include <string>

#include <esp_ota_ops.h>
#include <sys/socket.h>

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef WWW_ROOT
#define WWW_ROOT "/"
#endif

#ifndef HTTP_PORT
#define HTTP_PORT 80
#endif

using namespace std;


static HttpServer *WWW = 0;

static char TAG[] = "httpd";


static void send_json(HttpRequest *req, void (*f)(stream&))
{
	HttpResponse res;
	strstream jsonstr(res.contentString());
	f(jsonstr);
	res.setResult(HTTP_OK);
	res.setContentType(CT_APP_JSON);
	res.senddata(req->getConnection());
}


static void webdata_json(HttpRequest *req)
{
	send_json(req,runtimedata_to_json);
}


static void publish_config(stream &json)
{
	// security: do not publish confidential data!
	NodeConfig publish(Config);
	publish.clear_pass_hash();
	if (publish.has_station())
		publish.mutable_station()->clear_pass();
	if (publish.has_softap())
		publish.mutable_softap()->clear_pass();
	publish.toJSON(json);
}


static void config_json(HttpRequest *req)
{
	send_json(req,publish_config);
}


#ifdef CONFIG_HTTP_REVEAL_CONFIG
static void config_bin(HttpRequest *req)
{
	size_t s = Config.calcSize();
	char *buf = (char*)malloc(s);
	ssize_t n = Config.toMemory((uint8_t*)buf,s);
	assert(n == s);
	HttpResponse res;
	res.setResult(HTTP_OK);
	res.setContentType(CT_APP_OCTET_STREAM);
	res.addContent(buf,n);
	res.senddata(req->getConnection());
	free(buf);
}
#endif


static void write_alarms(stream &json)
{
#ifdef CONFIG_AT_ACTIONS
	json << "{\"alarms\":[";
	for (size_t i = 0, s = Config.at_actions_size(); i < s; ++i) {
		Config.at_actions(i).toJSON(json);
		if (i + 1 != s)
			json << ',';
	}
	json << "],\n";
#endif
	json <<	"\"valid_actions\":[\n";
	for (size_t i = 0; i < Actions.size(); ++i) {
		json << "{\"name\":\"";
		json << Actions[i].name;
		if (Actions[i].text) {
			json << "\",\"text\":\"";
			json << Actions[i].text;
		}
		if (i < Actions.size()-1)
			json << "\"},\n";
		else
			json << "\"}\n";
	}
	json << "],\n";
	json <<	"\"holidays\":[\n";
	for (size_t i = 0; i < Config.holidays_size(); ++i) {
		Config.holidays(i).toJSON(json);
		if (i + 1 != Config.holidays_size())
			json << ',';
	}
	json << "]}\n";
}

static void alarms_json(HttpRequest *req)
{
	log_info(TAG,"/alarms.json");
	send_json(req,write_alarms);
}


static int extractLine(const char *cont, const char *start, char *param, size_t mpl)
{
	const char *line = strstr(cont,start);
	if (line == 0)
		return 0;
	size_t sl = strlen(start);
	line += sl;
	const char *cr = strchr(line,'\r');
	size_t l;
	if (cr == 0) {
		l = strlen(line);
	} else {
		l = cr - line;
	}
	if (l >= mpl)
		return 1;
	memcpy(param,line,l);
	param[l] = 0;
	return 0;
}


static void exeShell(HttpRequest *req)
{
	//TimeDelta dt(__FUNCTION__);
	HttpResponse ans;
	char com[72];
	char pass[24];
	bool error = false;
	com[0] = 0;
	if (const char *c = req->getHeader("command").c_str()) {
		if (strlen(c) >= sizeof(com))
			error = true;
		else
			strcpy(com,c);
	}
	if (const char *p = req->getHeader("password").c_str()) {
		if (strlen(p) >= sizeof(pass))
			error = true;
		else
			strcpy(pass,p);
	}
	const char *cont = req->getContent();
	if (com[0] == 0) {
		if (extractLine(cont,"command=",com,sizeof(com)))
			error = true;
	}
	if (pass[0] == 0) {
		if (extractLine(cont,"password=",pass,sizeof(pass)))
			error = true;
	}
	if (com[0] == 0)
		error = true;
	log_info(TAG,"exeShell(com='%s', password='%s', cont='%s')",com,pass,cont);
	if (error) {
		log_warn(TAG,"error in request");
		ans.setResult(HTTP_BAD_REQ);
		ans.senddata(req->getConnection());
		return;
	}
	MemTerminal term;
	if (verifyPassword(pass))
		term.setPrivLevel(1);
	ans.setContentType(CT_TEXT_HTML);
	if (int r = shellexe(term,com))
		log_info(TAG,"exeShell('%s') = %d",com,r);
	ans.setResult(HTTP_OK);
	ans.addContent(term.getBuffer());
	ans.senddata(req->getConnection());
}


#define FLASHBUFSIZE 4096

static void updateFirmware(HttpRequest *r)
{
	int c = r->getConnection();
	HttpResponse ans;
	const char *pass = r->getHeader("password").c_str();
	if (!verifyPassword(pass)) {
		ans.setResult(HTTP_UNAUTH);
		ans.setContentType(CT_TEXT_PLAIN);
		ans.addContent("Password is set. Please fill password field.");
		ans.senddata(c);
		return;
	}
	int s = r->getContentLength();
	const char *part = r->getHeader("partition").c_str();
	log_info(TAG,"update part %s, len %u",part,s);
	bool app = (0 == strcmp(part,"app"));
	if ((s <= 0) || (!app && strcmp(part,"storage"))) {
		ans.setResult(HTTP_BAD_REQ);
		ans.senddata(c);
		return;
	}
	ans.setResult(HTTP_OK);
	ans.senddata(c);
	size_t s0 = r->getAvailableLength();
	esp_ota_handle_t ota = 0;
	const esp_partition_t *updatep = 0;
	uint32_t addr;
	vTaskPrioritySet(0,2);
	if (!app) {
#ifdef CONFIG_ROMFS
		// ROMFS/DATA partition
		if (s > RomfsSpace) {
			RTData.set_update_state("image too big");
			log_warn(TAG,"image too big");
			return;
		}
		addr = RomfsBaseAddr;
		if (esp_err_t e = spi_flash_erase_range(addr,RomfsSpace)) {
			log_warn(TAG,"erase failed %s",esp_err_to_name(e));
			RTData.set_update_state("erasing failed");
			return;
		}
		if (s0 > 0) {
			if (esp_err_t e = spi_flash_write(addr,r->getContent(),s0)) {
				log_warn(TAG,"write0 failed %s",esp_err_to_name(e));
				RTData.set_update_state("write0 failed");
				return;
			}
			addr += s0;
			s -= s0;
		}
#else
		RTData.set_update_state("no ROMFS configured");
		log_warn(TAG,"no ROMFS configured");
		return;
#endif
	} else {
		updatep = esp_ota_get_next_update_partition(NULL);
		if ((updatep == 0) || (s <= 0)) {
			log_warn(TAG,"prepare failed");
			RTData.set_update_state("prepare failed");
			return;
		}
		RTData.set_update_state("erasing flash");
		vTaskDelay(10);
		if (esp_err_t err = esp_ota_begin(updatep, s, &ota)) {
			RTData.set_update_state("begin failed");
			log_warn(TAG,"update begin error %s",esp_err_to_name(err));
			return;
		}
		RTData.set_update_state("flashing...");
		addr = updatep->address;
		if (s0 > 0) {
			if (esp_err_t err = esp_ota_write(ota,r->getContent(),s0)) {
				RTData.set_update_state("write0 failed");
				log_warn(TAG,"update write0 error %s",esp_err_to_name(err));
				return;
			}
			s -= s0;
			addr += s0;
		}
	}
	char *buf = (char*)malloc(FLASHBUFSIZE);
	if (buf == 0) {
		RTData.set_update_state("out of memory");
		return;
	}
	while (s > 0) {
		char st[64];
		sprintf(st,"updating at 0x%x, %d to go",addr,s);
		RTData.set_update_state(st);
		vTaskDelay(10);
		log_info(TAG,st);
		int n = read(c,buf,s > FLASHBUFSIZE ? FLASHBUFSIZE : s);
		if (0 > n) {
			snprintf(st,sizeof(st),"receive failed: %s",strerror(c));
			RTData.set_update_state(st);
			free(buf);
			if (ota)
				esp_ota_end(ota);
			return;
		}
		esp_err_t e;
		if (app)
			e = esp_ota_write(ota,buf,n);
		else
			e = spi_flash_write(addr,buf,n);
		if (e) {
			snprintf(st,sizeof(st),"write failed %d: %s",e,strerror(c));
			RTData.set_update_state(st);
			log_warn(TAG,st);
			free(buf);
			if (ota)
				esp_ota_end(ota);
			return;
		}
		s -= n;
		addr += n;
	}
	free(buf);
	if (app) {
		if (esp_ota_end(ota)) {
			RTData.set_update_state("failed");
			return;
		}
		if (esp_err_t err = esp_ota_set_boot_partition(updatep)) {
			RTData.set_update_state("chaning boot partition failed");
			log_error(TAG, "set boot failed: %s",esp_err_to_name(err));
			return;
		}
		RTData.set_update_state("done, rebooting");
		vTaskDelay(1000);
		esp_restart();
	}
	RTData.set_update_state("update completed");
	log_info(TAG,"flash update done");
}


static void postConfig(HttpRequest *r)
{
	int a = r->numArgs();
	log_info(TAG,"post_config: %d args",a);
	const char *newpass0 = 0, *newpass1 = 0;
	HttpResponse ans;
	if (Config.has_pass_hash() && !verifyPassword(r->arg("passwd").c_str())) {
		ans.setResult(HTTP_UNAUTH);
		ans.setContentType(CT_TEXT_PLAIN);
		ans.addContent("Password is set. Please fill password field.");
		ans.senddata(r->getConnection());
		return;
	}
	ans.setResult(HTTP_OK);
	ans.setContentType(CT_TEXT_PLAIN);
	for (int i = 0; i < a; ++i) {
		const char *argname = r->argName(i).c_str();
		if (0 == strcmp(argname,"newpass0")) {
			newpass0 = r->arg(i).c_str();
		} else if (0 == strcmp(argname,"newpass1")) {
			newpass1 = r->arg(i).c_str();
		} else if (0 == strcmp(argname,"passwd")) {
		} else {
			int x = change_setting(argname,r->arg(i).c_str());
			log_info(TAG,"%s = %s: %s\n",argname,r->arg(i).c_str(),x ? "Error" : "OK");
			ans.writeContent("%s = %s: %s\n",argname,r->arg(i).c_str(),x ? "Error" : "OK");
		}
	}
	ans.senddata(r->getConnection());
	if (newpass0 && newpass1 && (0 == strcmp(newpass0,newpass1)))
		setPassword(newpass0);
	activateSettings();
	storeSettings();
}


static void httpd_session(void *arg)
{
	int con = (int)arg;
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	if (0 > setsockopt(con,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)))
		log_warn(TAG,"error setting receive timeout: %s",strneterr(con));
	WWW->handleConnection(con);
	vTaskDelete(0);
}


extern "C"
void httpd_setup()
{
	WWW = new HttpServer(CONFIG_HTTP_ROOT,"/index.html");
#ifdef ESP32
	struct stat st;
	if (stat(CONFIG_HTTP_ROOT,&st) == -1) {
		log_info(TAG,"setting up www root");
		if (-1 == mkdir(CONFIG_HTTP_ROOT,0777))
			log_warn(TAG,"unable to create directory " CONFIG_HTTP_ROOT ": %s",strerror(errno));
	}
	WWW->addDirectory(CONFIG_HTTP_ROOT);
#ifndef CONFIG_ROMFS
	const char *upload = CONFIG_HTTP_UPLOAD;
	if (upload && upload[0]) {
		if (stat(upload,&st) == -1) {
			log_info(TAG,"creating upload directory");
			if (-1 == mkdir(upload,0777)) {
				log_warn(TAG,"unable to create directory %s: %s",upload,strerror(errno));
			} else {
				WWW->setUploadDir(upload);
			}
		} else {
			WWW->setUploadDir(upload);
		}
	}
#endif
#elif defined ESP8266
	/*
	WWW->addMemory("/index.html",s20_index_html);
	WWW->addMemory("/alarms.html",alarms_html);
	WWW->addMemory("/config.html",s20_config_html);
	WWW->addMemory("/shell.html",shell_html);
	WWW->addMemory("/setpass.html",setpass_html);
	*/
#else
#error missing implementation
#endif

	WWW->addFile("/alarms.html");
	WWW->addFile("/config.html");
	WWW->addFile("/index.html");
	WWW->addFile("/shell.html");
	WWW->addFile("/setpass.html");
#ifdef CONFIG_OTA
	WWW->addFile("/update.html");
	WWW->addFunction("/do_update",updateFirmware);
#endif
	WWW->addFunction("/config.json",config_json);	// reveals passwords
#ifdef CONFIG_HTTP_REVEAL_CONFIG
	WWW->addFunction("/config.bin",config_bin);	// reveals passwords
#endif
	WWW->addFunction("/alarms.json",alarms_json);
	WWW->addFunction("/data.json",webdata_json);
	WWW->addFunction("/run_exe",exeShell);
	WWW->addFunction("/post_config",postConfig);
#ifdef CONFIG_CAMERA
	WWW->addFunction("/webcam.jpeg",webcam_sendframe);
#endif
	listen_tcp(HTTP_PORT,httpd_session,"httpd","_http",7,2048);
}

#endif	// CONFIG_HTTP
