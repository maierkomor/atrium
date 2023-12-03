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

#ifdef CONFIG_ESP_PHY_ENABLE_USB

#include "adc.h"
#include "charger.h"
#include "cyclic.h"
#include "event.h"
#include "env.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "shell.h"
#include "swcfg.h"
#include "terminal.h"

#define TAG MODULE_USB


struct UsbCcMonitor
{
	EnvString cable, status;
	EnvNumber imax, cc1, cc2;
	adc_oneshot_unit_handle_t cc1h, cc2h;
	adc_channel_t cc1c, cc2c;
	adc_cali_handle_t cali1 = 0, cali2 = 0;

	UsbCcMonitor(adc_oneshot_unit_handle_t h1, adc_oneshot_unit_handle_t h2, adc_channel_t c1, adc_channel_t c2, adc_cali_handle_t ca1, adc_cali_handle_t ca2);

	unsigned cyclic();
};


UsbCcMonitor::UsbCcMonitor(adc_oneshot_unit_handle_t h1, adc_oneshot_unit_handle_t h2, adc_channel_t ch1, adc_channel_t ch2, adc_cali_handle_t ca1, adc_cali_handle_t ca2)
: cable("cable","unknown")
, status("status","invalid")
, imax("Imax",500,"mA")
, cc1("CC1",NAN,"mV")
, cc2("CC2",NAN,"mV")
, cc1h(h1), cc2h(h2)
, cc1c(ch1), cc2c(ch2)
, cali1(ca1), cali2(ca2)
{
}


unsigned usb_cc_cyclic(void *arg)
{
	UsbCcMonitor *drv = (UsbCcMonitor*)arg;
	int raw1, raw2, v1, v2;
	const char *errstr = "failed";
	const char *cbl = "unknown";
	double imax = 200;

	if (0 != adc_oneshot_read(drv->cc1h, drv->cc1c, &raw1)) {
		errstr = "read-cc1";
	} else if (0 != adc_oneshot_read(drv->cc2h, drv->cc2c, &raw2)) {
		errstr = "read-cc2";
	} else {
		if (adc_cali_raw_to_voltage(drv->cali1, raw1, &v1)) {
			errstr = "conv-cc1";
		} else if (adc_cali_raw_to_voltage(drv->cali2, raw2, &v2)) {
			errstr = "conv-cc2";
		} else {
			// 0.4V : 500mA
			// 0.9V : 1500mA
			// 1.6V : 3000mA
			if ((v1 > 1400) && (v2 < 100)) {
				imax = 3000;
				cbl = "connected";
			} else if ((v1 < 100) && (v2 > 1400)) {
				imax = 3000;
				cbl = "flipped";
			} else if ((v1 > 800) && (v2 < 100)) {
				imax = 1500;
				cbl = "connected";
			} else if ((v1 < 100) && (v2 > 800)) {
				imax = 1500;
				cbl = "flipped";
			} else if ((v1 > 400) && (v2 < 100)) {
				imax = 500;
				cbl = "connected";
			} else if ((v1 < 100) && (v2 > 400)) {
				imax = 500;
				cbl = "flipped";
			} else if ((v1 > 300) && (v2 > 300)) {
				cbl = "connected";
				imax = 500;
			} else if ((v1 < 200) && (v2 < 200)) {
				cbl = "disconnected";
			} else {
				cbl = "unknown";
				imax = 500;
			}
			errstr= "ok";
			if (Charger *c = Charger::getInstance())
				c->setImax(imax);
		}
	}
	drv->cable.set(cbl);
	drv->imax.set(imax);
	drv->status.set(errstr);
	drv->cc1.set((float)v1);
	drv->cc2.set((float)v2);
	return 100;
}


void usb_setup()
{
	const auto &sys = HWConf.system();
	int8_t cc1 = sys.cc1_gpio();
	int8_t cc2 = sys.cc2_gpio();
	log_dbug(TAG,"cc1 = %d, cc2 = %d",(int)cc1,(int)cc2);
	if ((-1 != cc1) && (-1 != cc2)) {
		adc_unit_t cc1u,cc2u;
		adc_channel_t cc1c,cc2c;
		if (esp_err_t e = adc_oneshot_io_to_channel(cc1,&cc1u,&cc1c)) {
			log_warn(TAG,"invalid GPIO%d for CC1: %s",cc1,esp_err_to_name(e));
		} else if (esp_err_t e = adc_oneshot_io_to_channel(cc2,&cc2u,&cc2c)) {
			log_warn(TAG,"invalid GPIO%d for CC2: %s",cc2,esp_err_to_name(e));
		} else {
			adc_oneshot_unit_handle_t cc1h = adc_get_unit_handle(cc1u);
			adc_oneshot_unit_handle_t cc2h = adc_get_unit_handle(cc2u);
			EnvObject *usbenv = RTData->add("usb");
			adc_cali_handle_t cali1 = 0, cali2 = 0;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
			adc_cali_curve_fitting_config_t cali_config = {
				.unit_id = cc1u,
				.chan = cc1c,
				.atten = ADC_ATTEN_DB_0,
				.bitwidth = ADC_BITWIDTH_DEFAULT,
			};
			if (esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali1))
				log_warn(TAG,"calibration failure: %s",esp_err_to_name(ret));
			cali_config.unit_id = cc2u;
			cali_config.chan = cc2c;
			if (esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali2))
				log_warn(TAG,"calibration failure: %s",esp_err_to_name(ret));
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
			adc_cali_line_fitting_config_t cali_config = {
				.unit_id = cc1u,
				.atten = ADC_ATTEN_DB_0,
				.bitwidth = ADC_BITWIDTH_DEFAULT,
			};
			if (esp_err_t ret = adc_cali_create_scheme_line_fitting(&cali_config, &cali1))
				log_warn(TAG,"calibration failure: %s",esp_err_to_name(ret));
			cali_config.unit_id = cc2u;
			if (esp_err_t ret = adc_cali_create_scheme_line_fitting(&cali_config, &cali2))
				log_warn(TAG,"calibration failure: %s",esp_err_to_name(ret));
#else
#error no known adc cali scheme
#endif
			UsbCcMonitor *m = new UsbCcMonitor(cc1h,cc2h,cc1c,cc2c,cali1,cali2);
			usbenv->add(&m->cable);
			usbenv->add(&m->status);
			usbenv->add(&m->imax);
			usbenv->add(&m->cc1);
			usbenv->add(&m->cc2);
			cyclic_add_task("usb",usb_cc_cyclic,m,0);
		}
	}
}

#endif // CONFIG_ESP_PHY_ENABLE_USB


#if 0 // defined CONFIG_USB_CONSOLE && defined CONFIG_TINYUSB_CDC_ENABLED

#include <tinyusb.h>
#include <tusb_cdc_acm.h>

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



