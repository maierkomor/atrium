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

#ifdef CONFIG_HTTP

#include "actions.h"
#include "swcfg.h"
#include "globals.h"
#include "HttpServer.h"
#include "HttpReq.h"
#include "HttpResp.h"
#include "inetd.h"
#include "env.h"
#include "log.h"
#include "nvm.h"
#include "lwtcp.h"
#include "mem_term.h"
#include "netsvc.h"
#include "romfs.h"
#include "profiling.h"
#include "settings.h"
#include "shell.h"
#include "strstream.h"
#include "support.h"
#include "webcam.h"

#include "memfiles.h"	// generated automatically

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#ifdef ESP32
#include <esp_core_dump.h>
#if IDF_VERSION < 50
#include <esp_spi_flash.h>
#else
#include <spi_flash_mmap.h>
#endif
#endif
#include <esp_ota_ops.h>

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef WWW_ROOT
#define WWW_ROOT "/"
#endif

#ifndef HTTP_PORT
#define HTTP_PORT 80
#endif

#if defined CONFIG_FATFS || defined CONFIG_SPIFFS
#define HAVE_FS
#endif

using namespace std;


#define TAG MODULE_WWW

static HttpServer *WWW = 0;
static SemaphoreHandle_t Sem = 0;


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


struct PubAction
{
	PubAction(stream &str)
	: out(str)
	{ }

	stream &out;
	bool comma = false;
};


static void pub_action_iterator(void *arg, const Action *a)
{
	if (a && a->text) {
		PubAction *p = (PubAction *)arg;
		if (p->comma)
			p->out << ',';
		else
			p->comma = true;
		p->out << "\n{\"name\":\"" << a->name << "\",\"text\":\"" << a->text << "\"}";
	}
}


static void publish_actions(stream &json)
{
	PubAction a(json);
	json << "{\"actions\":[\n";
	action_iterate(pub_action_iterator,(void*)&a);
	json << "\n]}\n";
}


static void config_json(HttpRequest *req)
{
	send_json(req,publish_config);
}


static void actions_json(HttpRequest *req)
{
	send_json(req,publish_actions);
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


static void write_action(void *p, const Action *a)
{
	static bool first;
	if (a == 0) {
		first = true;
		return;
	}
	stream &json = *(stream *)p;
	if (!first)
		json << ",\n";
	first = false;
	json << "{\"name\":\"";
	json << a->name;
	if (a->text) {
		json << "\",\"text\":\"";
		json << a->text;
	}
	++a;
	json << "\"}";
}


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
	write_action(0,0);
	action_iterate(write_action,&json);
	json << "\n],\n";
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
	log_dbug(TAG,"/alarms.json");
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
	PROFILE_FUNCTION();
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
	log_dbug(TAG,"exeShell(com='%s', cont='%s')",com,cont);
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
	if (const char *r = shellexe(term,com))
		log_dbug(TAG,"exeShell '%s': %s",com,r);
	ans.setResult(HTTP_OK);
	if (int s = term.getSize())
		ans.addContent(term.getBuffer(),s);
	ans.senddata(req->getConnection());
}


#ifdef CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
#define MTU 1024
static void getCore(HttpRequest *req)
{
	LwTcp *con = req->getConnection();
	size_t addr,size;
	esp_err_t e = esp_core_dump_image_get(&addr,&size);
	spi_flash_mmap_handle_t handle = 0;
	const char *data = 0;
	HttpResponse res;
	if (e == 0)
		e = spi_flash_mmap(addr,size,SPI_FLASH_MMAP_DATA,(const void**)&data,&handle);
	if (e) {
		res.setResult(HTTP_BAD_REQ);
		res.senddata(con);
	} else {
		res.setResult(HTTP_OK);
		res.setContentType(CT_APP_OCTET_STREAM);
		res.setContentLength(size);
		res.senddata(con);
		size_t off = 0;
		while (off != size) {
			size_t n = size-off > MTU ? MTU : size-off;
			con->write((char*)data+off,n,false);
			con->sync(true);
			off += n;
		}
	}
	if (handle)
		spi_flash_munmap(handle);
}
#endif


#define FLASHBUFSIZE 1024

static void updateFirmware(HttpRequest *r)
{
	log_dbug(TAG,"updateFirmware");
	LwTcp *c = r->getConnection();
	HttpResponse ans;
	const char *pass = r->getHeader("password").c_str();
	if (!verifyPassword(pass)) {
		ans.setResult(HTTP_UNAUTH);
		ans.setContentType(CT_TEXT_PLAIN);
		ans.addContent("Password error.");
		ans.senddata(c);
		log_dbug(TAG,"Password error.");
		return;
	}
	int s = r->getContentLength();
	const char *part = r->getHeader("partition").c_str();
	log_dbug(TAG,"update part %s, len %u",part,s);
	/*
	if ((s <= 0) || (!app && strcmp(part,"storage"))) {
		ans.setResult(HTTP_BAD_REQ);
		ans.senddata(c);
		return;
	}
	*/
	if (UpdateState == 0) {
		UpdateState = new EnvString("update_state","initializing");
		RTData->add(UpdateState);
	}
	ans.setResult(HTTP_OK);
	ans.senddata(c);
	size_t s0 = r->getAvailableLength();
	esp_ota_handle_t ota = 0;
	const esp_partition_t *updatep = 0;
	uint32_t addr;
	bool app = false;
	char st[64];
	if (!strcmp(part,"app")) {
		app = true;
		updatep = esp_ota_get_next_update_partition(NULL);
		if ((updatep == 0) || (s <= 0)) {
			const char *err = "No update partition.";
			log_warn(TAG,err);
			UpdateState->set(err);
			ans.addContent(err);
			return;
		}
		log_dbug(TAG,"erasing flash");
		UpdateState->set("erasing flash");
		if (esp_err_t err = esp_ota_begin(updatep, s, &ota)) {
			sprintf(st,"update begin: %s",esp_err_to_name(err));
			UpdateState->set(st);
			ans.addContent(st);
			return;
		}
		UpdateState->set("flashing...");
		log_dbug(TAG,"flashing...");
		addr = updatep->address;
		if (s0 > 0) {
			if (esp_err_t err = esp_ota_write(ota,r->getContent(),s0)) {
				sprintf(st,"OTA write: %s",esp_err_to_name(err));
				UpdateState->set(st);
				ans.addContent(st);
				return;
			}
			s -= s0;
			addr += s0;
		}
	} else if (!strcmp(part,"hw.cfg") || !strcmp(part,"node.cfg")) {
		void *tmp = malloc(s);
		if (tmp == 0) {
			const char *err = "Out of memory.";
			UpdateState->set(err);
			log_warn(TAG,err);
			ans.addContent(err);
			return;
		}
		memcpy(tmp,r->getContent(),s0);
		if (s0 != s) {
			int n = c->read((char*)tmp+s0,s - s0);
			if (n < 0)
				log_error(TAG,"error receiving data: %s",strerror(errno));
			s0 += n;
		}
		bool r = false;
		if (s == s0) {
			log_dbug(TAG,"updating NVS/%s",part);
			if (0 == nvm_store_blob(part,(uint8_t*)tmp,s))
				r = true;
		}
		UpdateState->set(r ? "success" : "failed");
		free(tmp);
		return;
	} else {
		updatep = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,part);
		addr = updatep->address;
		const char *err = 0;
		if (updatep == 0) {
			sprintf(st,"no ROMFS partition '%s'",part);
			err = st;
		} else if (s > updatep->size) {
			// ROMFS/DATA partition
			err = "image too big";
#if IDF_VERSION >= 50
		} else if (esp_err_t e = esp_partition_erase_range(updatep,0,updatep->size)) {
			sprintf(st,"erase partition: %s",esp_err_to_name(e));
			err = st;
		} else if (s0 <= 0) {
		} else if (esp_err_t e = esp_partition_write_raw(updatep,0,r->getContent(),s0)) {
			sprintf(st,"write partition: %s",esp_err_to_name(e));
			err = st;
			addr = s0;
			s -= s0;
#else
		} else if (esp_err_t e = spi_flash_erase_range(addr,updatep->size)) {
			sprintf(st,"erase failed: %s",esp_err_to_name(e));
			err = st;
		} else if (s0 > 0) {
			if (esp_err_t e = spi_flash_write(addr,r->getContent(),s0)) {
				sprintf(st,"SPI write: %s",esp_err_to_name(e));
				err = st;
			}
			addr += s0;
			s -= s0;
#endif
		}
		if (err) {
			UpdateState->set(err);
			ans.addContent(st);
			return;
		}
	}
	char *buf = (char*)malloc(FLASHBUFSIZE);
	if (buf == 0) {
		UpdateState->set("Out of memory.");
		return;
	}
	while (s > 0) {
		sprintf(st,"updating at 0x%x, %d to go",(unsigned)addr,s);
		UpdateState->set(st);
		log_dbug(TAG,st);
		int n = c->read(buf,s > FLASHBUFSIZE ? FLASHBUFSIZE : s);
		if (0 > n) {
			snprintf(st,sizeof(st),"receive error: %s",c->error());
			UpdateState->set(st);
			free(buf);
			if (ota)
				esp_ota_end(ota);
			ans.addContent(st);
			return;
		}
		esp_err_t e;
		if (app) {
			e = esp_ota_write(ota,buf,n);
		} else {
#if IDF_VERSION >= 50
			e = esp_partition_write_raw(updatep,addr,buf,n);

#else
			e = spi_flash_write(addr,buf,n);
#endif
		}
		if (e) {
			snprintf(st,sizeof(st),"write error: %d",e);
			UpdateState->set(st);
			log_warn(TAG,st);
			free(buf);
			if (ota)
				esp_ota_end(ota);
			ans.addContent(st);
			return;
		}
		s -= n;
		addr += n;
	}
	free(buf);
	if (app) {
		if (esp_ota_end(ota)) {
			UpdateState->set("image error");
			ans.addContent("image error");
			return;
		}
		if (esp_err_t err = esp_ota_set_boot_partition(updatep)) {
			UpdateState->set("set boot failed");
			log_error(TAG, "set boot failed: %s",esp_err_to_name(err));
			return;
		}
		UpdateState->set("success, rebooting");
		ans.addContent("success, rebooting");
		vTaskDelay(1000);
		esp_restart();
	}
	UpdateState->set("update completed");
	log_dbug(TAG,"update completed");
}


static void postConfig(HttpRequest *r)
{
	int a = r->numArgs();
	log_dbug(TAG,"post_config: %d args",a);
	const char *newpass0 = 0, *newpass1 = 0;
	HttpResponse ans;
	if (Config.has_pass_hash() && !verifyPassword(r->arg("passwd").c_str())) {
		ans.setResult(HTTP_UNAUTH);
		ans.setContentType(CT_TEXT_PLAIN);
		ans.addContent("Password error.");
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
			NullTerminal t;
			const char *x = update_setting(t,argname,r->arg(i).c_str());
			log_dbug(TAG,"%s = %s: %s\n",argname,r->arg(i).c_str(),x);
			ans.writeContent("%s = %s: %s\n",argname,r->arg(i).c_str(),x);
		}
	}
	ans.senddata(r->getConnection());
	if (newpass0 && newpass1 && (0 == strcmp(newpass0,newpass1)))
		setPassword(newpass0);
	cfg_activate();
	cfg_store_nodecfg();
}


static void httpd_session(LwTcp *con)
{
	PROFILE_FUNCTION();
	if (pdTRUE == xSemaphoreTake(Sem,100/portTICK_PERIOD_MS)) {
		if (WWW)
			WWW->handleConnection(con);
		xSemaphoreGive(Sem);
		log_dbug(TAG,"closing connection");
	} else {
		log_warn(TAG,"too many connections");
	}
	con->close();
	xSemaphoreGive(Sem);
	vTaskDelete(0);
}


void httpd_setup()
{
	auto &c = Config.httpd();
	if (c.has_start() && !c.start())
		return;
	const char *index_html = 0;
	const char *root = "/flash";
	uint16_t port = c.has_port() ? c.port() : HTTP_PORT;
#ifdef CONFIG_ROMFS
	if (-1 != romfs_open("index.html"))
		index_html = "/index.html";
#endif
#ifdef HAVE_FS
	struct stat st;
	const char *upload = "/flash/upload";
	if (c.has_uploaddir())
		upload = c.uploaddir().c_str();
	if ((stat(upload,&st) == 0) && S_ISDIR(st.st_mode)) {
		log_info(TAG,"uplaod dir %s",upload);
	} else {
		log_info(TAG,"no uplaod dir");
		upload = 0;
	}
	char file[64];
	char *f;
	if (c.has_root()) {
		size_t rl = c.root().size();
		memcpy(file,c.root().data(),rl);
		f = file+rl;
		if (f[-1] != '/')
			*f++ = '/';
	} else {
		file[0] = '/';
		f = file+1;
	}
	strcpy(f,"index.htm");
	if (stat(file,&st) == 0) {
		index_html = "/index.htm";
	} else {
		strcpy(f,"index.html");
		if (stat(file,&st) == 0)
			index_html = "/index.html";
	}
#endif
	if (!c.has_start() && (index_html == 0)) {
		log_info(TAG,"no index.html found, no start requested.");
		return;
	}
	WWW = new HttpServer(root,index_html);
	if (root)
		WWW->addDirectory(root);
#ifdef HAVE_FS
	if (upload)
		WWW->setUploadDir(upload);
#endif
#ifdef CONFIG_OTA
	WWW->addFunction("/do_update",updateFirmware);
#endif
	WWW->addFunction("/config.json",config_json);
	WWW->addFunction("/actions.json",actions_json);
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
#ifdef CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
	WWW->addFunction("/core",getCore);
#endif
	Sem = xSemaphoreCreateCounting(4,4);
	listen_port(port,m_tcp,httpd_session,"httpd","_http",7,4096);
}

#endif	// CONFIG_HTTP
