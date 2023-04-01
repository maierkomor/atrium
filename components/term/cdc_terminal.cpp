/*
 *  Copyright (C) 2022-2023, Thomas Maier-Komor
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

#if defined CONFIG_USB_CONSOLE && defined CONFIG_TINYUSB_CDC_ENABLED

#include "cdc_terminal.h"
#include "log.h"

#include <esp_err.h>
#include <tinyusb.h>
#include <tusb_cdc_acm.h>
#include <tusb_tasks.h>
#include <errno.h>

#define TAG MODULE_CON


using namespace std;

static const char *CdcEventStr[] = {
	"receive",
	"want char",
	"line state changed",
	"line coding changed",
};

static SemaphoreHandle_t Sem = 0;
static CdcTerminal *Instance = 0;
void shell(Terminal &term, bool prompt = true);

static void coding_changed_cb(int itf, cdcacm_event_t *ev)
{
	assert(ev->type < sizeof(CdcEventStr)/sizeof(CdcEventStr[0]));
	log_info(TAG,"%s",CdcEventStr[ev->type]);
}


static void rx_callback(int itf, cdcacm_event_t *ev)
{
	assert(ev->type < sizeof(CdcEventStr)/sizeof(CdcEventStr[0]));
	log_dbug(TAG,"rx_callback(%d): %s",itf,CdcEventStr[ev->type]);
	if (pdFALSE == xSemaphoreGive(Sem))
		log_dbug(TAG,"give failed");
}


static void rx_wanted_callback(int itf, cdcacm_event_t *ev)
{
	assert(ev->type < sizeof(CdcEventStr)/sizeof(CdcEventStr[0]));
	log_dbug(TAG,"rx_wanted_callback(): %s",CdcEventStr[ev->type]);
}


static void line_state_changed_callback(int itf, cdcacm_event_t *ev)
{
	int dtr = ev->line_state_changed_data.dtr;
	int rts = ev->line_state_changed_data.rts;
	log_info(TAG,"line change DTR=%d, RTS=%d",dtr,rts);
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
	} else {
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
			log_warn(TAG,"CDC init: %s",esp_err_to_name(e));
			return 0;
		}
	}
	Sem = xSemaphoreCreateBinary();
	Instance = new CdcTerminal(crnl);
	return Instance;
}


int CdcTerminal::read(char *buf, size_t s, bool block)
{
	size_t n;
	for (;;) {
		esp_err_t e = tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0,(uint8_t*)buf,s,&n);
		log_dbug(TAG,"read(%d,%d): n=%d,e=%d",s,block,n,e);
		if ((e == 0) && (n > 0))
			return n;
		if (e != 257)
			return -e;
		if (!block)
			return -EWOULDBLOCK;
		xSemaphoreTake(Sem,portMAX_DELAY);
	}
}


extern "C" uint32_t tud_cdc_n_write_flush(uint8_t);

int CdcTerminal::write(const char *str, size_t l)
{
	int r = 0;
	while (l) {
		int n = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0,(const uint8_t *)str,l);
		log_dbug(TAG,"writeQ(%d) = %d",l,n);
		if (n < 0)
			return -1;
		r += n;
		l -= n;
		str += n;
		// TODO - find workaround - flushing is broken
		//int e = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0,portMAX_DELAY);
//		int e = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0,100/portTICK_PERIOD_MS);
		//log_dbug(TAG,"flush() = %d",e);
//		uint32_t f = tud_cdc_n_write_flush(0);
//		log_dbug(TAG,"flush() %u",f);
		log_dbug(TAG,"delay()");
		vTaskDelay(200/portTICK_PERIOD_MS);
	}
	return r;
}

#endif
