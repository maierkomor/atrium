/*
 *  Copyright (C) 2022, Thomas Maier-Komor
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

#ifdef CONFIG_TINYUSB

#include "cdc_terminal.h"
#include "log.h"

#include <esp_err.h>
#include <tinyusb.h>
#include <tusb_cdc_acm.h>
#include <tusb_tasks.h>
#include <errno.h>

#define TAG logmod_usb

using namespace std;

static SemaphoreHandle_t Sem = 0, Mtx = 0;
static CdcTerminal *Instance = 0;
void shell(Terminal &term, bool prompt = true);


static void coding_changed_cb(int itf, cdcacm_event_t *ev)
{
	log_dbug(TAG,"line_coding_changed(%d)",ev->type);
}


static void rx_callback(int itf, cdcacm_event_t *ev)
{
	log_dbug(TAG,"rx_callback(%d)",ev->type);
	xSemaphoreGive(Sem);
}


static void rx_wanted_callback(int itf, cdcacm_event_t *ev)
{
	log_dbug(TAG,"rx_wanted_callback(%d)",ev->type);
}


static void line_state_changed_callback(int itf, cdcacm_event_t *ev)
{
	int dtr = ev->line_state_changed_data.dtr;
	int rts = ev->line_state_changed_data.rts;
	log_dbug(TAG,"line change DTR=%d, RTS=%d",dtr,rts);
}


CdcTerminal::CdcTerminal(bool crnl)
: Terminal(crnl)
{
	log_info(TAG,"terminal on CDC_ACM_0");
}


CdcTerminal *CdcTerminal::create(bool crnl)
{
	if (Instance)
		return 0;
	tinyusb_config_t usbcfg;
	bzero(&usbcfg,sizeof(usbcfg));
	if (esp_err_t e = tinyusb_driver_install(&usbcfg)) {
		log_warn(TAG,"USB init: %s",esp_err_to_name(e));
		return 0;
	}

	tinyusb_config_cdcacm_t acmcfg;
	bzero(&acmcfg,sizeof(acmcfg));
	acmcfg.usb_dev = TINYUSB_USBDEV_0;
	acmcfg.cdc_port = TINYUSB_CDC_ACM_0;
	acmcfg.rx_unread_buf_sz = 128;
	acmcfg.callback_rx = rx_callback;
	acmcfg.callback_rx_wanted_char = rx_wanted_callback;
	acmcfg.callback_line_state_changed = line_state_changed_callback;
	acmcfg.callback_line_coding_changed = coding_changed_cb;
	if (esp_err_t e = tusb_cdc_acm_init(&acmcfg)) {
		log_warn(TAG,"CDC ACM init: %s",esp_err_to_name(e));
		return 0;
	}
	Sem = xSemaphoreCreateBinary();
	Mtx = xSemaphoreCreateMutex();
	Instance = new CdcTerminal(crnl);
	return Instance;
}


int CdcTerminal::read(char *buf, size_t s, bool block)
{
	size_t n;
	log_dbug(TAG,"read(%d,%d)",s,block);
	for (;;) {
		esp_err_t e = tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0,(uint8_t*)buf,s,&n);
		log_dbug(TAG,"read(%d,%d): n=%d,e=%d",s,block,n,e);
		if ((e == 0) && (n > 0))
			return n;
		if (e != 257)
			return -e;
		if (!block)
			return -EWOULDBLOCK;
		log_dbug(TAG,"read(%d,%d): block",s,block);
		if (pdTRUE != xSemaphoreTake(Sem,portMAX_DELAY))
			abort_on_mutex(Sem,__FUNCTION__);
		log_dbug(TAG,"read(%d,%d): cont",s,block);
	}
	/*
	log_dbug(TAG,"read(%d,%d)",s,block);
	for (;;) {
		uint32_t n = tud_cdc_n_available(0);
		log_dbug(TAG,"read(%d,%d): %u available",s,block,n);
		if (n) {
			int x = tud_cdc_n_read(0,buf,n);
			log_dbug(TAG,"read(%d,%d)=%d",s,n,x);
			return x;
		}
		if (!block)
			return 0;
		log_dbug(TAG,"read(%d,%d): block",s,block);
		if (pdTRUE != xSemaphoreTake(Sem,portMAX_DELAY))
			abort_on_mutex(Sem,__FUNCTION__);
	}
	*/
}


int CdcTerminal::write(const char *str, size_t l)
{
	log_dbug(TAG,"write(%d)",l);
	esp_err_t e = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0,(const uint8_t *)str,l);
	log_dbug(TAG,"write(%d)=%d",l,e);
	do {
		log_dbug(TAG,"write(%d) flush",l);
		e = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0,100); //portMAX_DELAY);
		log_dbug(TAG,"write(%d) flush=%d",l,e);
	} while (e);
	return e != 0 ? -e : l;
	/*
	log_dbug(TAG,"write(%d)",l);
	int x = tud_cdc_n_write(0,str,l);
	log_dbug(TAG,"write(%d)=%d",l,x);
	return x;
	*/
}

#endif
