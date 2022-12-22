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

#ifdef CONFIG_USB

#include "cyclic.h"
#include "event.h"
#include "log.h"

#include <tinyusb.h>
#include <tusb_cdc_acm.h>

#define TAG MODULE_USB


struct UsbConsole
{
	size_t size = 0;
	bool dts = false, rts = false;
	char buf[];
};

static UsbConsole *con = 0;


static void rx_callback(int itf, cdcacm_event_t *ev)
{

}


static void line_state_change_callback(int itf, cdcacm_event_t *ev)
{
	int dtr = ev->line_state_changed_data.dtr;
	int rts = ev->line_state_changed_data.rts;

}


void usb_setup()
{
	tinyusb_config_t usbcfg;
	bzero(&usbcfg,sizeof(usbcfg));
	if (esp_err_t e = tinyusb_driver_install(&usbcfg)) {
		log_warn(TAG,"USB init: %s",esp_err_to_name(e));
		return;
	}

	tinyusb_config_cdcacm_t acmcfg;
	bzero(&acmcfg,sizeof(acmcfg));
	acmcfg.usb_dev = TINYUSB_USBDEV_0;
	acmcfg.cdc_port = TINYUSB_CDC_ACM_0;
	acmcfg.rx_unread_buf_sz = 128;
	acmcfg.callback_rx = rx_callback;
	acmcfg.rx_wanted_char = 0;
	acmcfg.callback_line_state_changed = line_state_changed_callback;
	acmcfg.callback_line_coding_changed = 0;
	if (esp_err_t e = tusb_cdc_acm_init(&acmcfg)) {
		log_warn(TAG,"CDC ACM init: %s",esp_err_to_name(e));
		return;
	}
}


#endif // CONFIG_USB
