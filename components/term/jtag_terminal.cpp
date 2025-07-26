/*
 *  Copyright (C) 2022-2025, Thomas Maier-Komor
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

#ifdef CONFIG_USB_CONSOLE

#include "jtag_terminal.h"
#include "globals.h"
#include "log.h"
#include "swcfg.h"

#include <driver/usb_serial_jtag.h>
#include <freertos/FreeRTOS.h>

#define TAG MODULE_CON

using namespace std;



JtagTerminal::JtagTerminal(bool crnl)
: Terminal(crnl)
{
	if (unsigned h = Config.history())
		setHistorySize(h);
	usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
	cfg.rx_buffer_size = 256;
	cfg.tx_buffer_size = 256;
	if (esp_err_t e = usb_serial_jtag_driver_install(&cfg))
		log_warn(TAG,"JTAG driver install: %s",esp_err_to_name(e));
}


int JtagTerminal::read(char *buf, size_t s, bool block)
{
	int n = usb_serial_jtag_read_bytes(buf,s,0);
	if ((n > 0) || (!block))
		return n;
	n = usb_serial_jtag_read_bytes(buf,1,portMAX_DELAY);
	if (s == 1)
		return 1;
	int x = usb_serial_jtag_read_bytes(buf+1,s-1,0);
	if (x > 0)
		n += x;
	return n;
}


int JtagTerminal::write(const char *str, size_t l)
{
	if (l)
		return usb_serial_jtag_write_bytes(str,l,portMAX_DELAY);
	return 0;
}

#endif
