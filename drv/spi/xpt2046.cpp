/*
 *  Copyright (C) 2023-2025, Thomas Maier-Komor
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

#ifdef CONFIG_XPT2046

#define TAG MODULE_XPT2046

#include "cyclic.h"
#include "env.h"
#include "event.h"
#include "log.h"
#include "nvm.h"
#include "terminal.h"
#include "xpt2046.h"

#include <driver/gpio.h>

#define BIT_START	0x80
#define BIT_PD_REF	0x01
#define BIT_PD_ADC	0x02
#define BIT_PD_BOTH	0x03
#define BIT_SER		0x04
#define BIT_A0		0x10
#define BIT_A1		0x20
#define BIT_A2		0x40
#define REG_X		(BIT_START|BIT_A2|BIT_A0)
#define REG_Y		(BIT_START|BIT_A0)
#define REG_Z1		(BIT_START|BIT_A1|BIT_A0)
#define REG_Z2		(BIT_START|BIT_A2)

#if IDF_VERSION >= 50
#define gpio_pad_select_gpio(...)
#endif

XPT2046 *XPT2046::Instance = 0;


XPT2046::XPT2046(uint8_t cs, int8_t intr, spi_device_handle_t hdl)
: SpiDevice(drvName(), cs)
, m_rx("rawx")
, m_ry("rawy")
, m_rz("rawz")
, m_a0("a0","%")
, m_a1("a1","%")
, m_p("p",false)
, m_hdl(hdl)
, m_sem(xSemaphoreCreateBinary())
{
	m_evp = event_register(m_name,"`pressed");
	m_evr = event_register(m_name,"`released");
	if (intr >= 0) {
		gpio_num_t i = (gpio_num_t) intr;
		gpio_pad_select_gpio(i);
		gpio_set_direction(i,GPIO_MODE_INPUT);
		if (esp_err_t e = gpio_set_intr_type(i,GPIO_INTR_NEGEDGE)) {
			log_warn(TAG,"failed to set interrupt type: %s",esp_err_to_name(e));
		} else if (esp_err_t e = gpio_isr_handler_add(i,intrHandler,(void*)this)) {
			log_warn(TAG,"failed to attach interrupt handler: %s",esp_err_to_name(e));
		}
	}
	char tag[16];
	sprintf(tag,"xpt2046@%u",cs);
	uint16_t data[6];
	size_t n = sizeof(data);
	if (0 == nvm_read_blob(tag,(uint8_t*)data,&n)) {
		m_lx = data[0];
		m_ux = data[1];
		m_ly = data[2];
		m_uy = data[3];
		m_lz = data[4];
		m_uz = data[5];
	}
}


void XPT2046::attach(EnvObject *root)
{
	root->add(&m_rx);
	root->add(&m_ry);
	root->add(&m_rz);
	root->add(&m_a0);
	root->add(&m_a1);
	root->add(&m_p);
}


XPT2046 *XPT2046::create(spi_host_device_t host, spi_device_interface_config_t &cfg, int8_t intr)
{
	if (cfg.clock_speed_hz == 0)
		cfg.clock_speed_hz = SPI_MASTER_FREQ_8M;	// maximum: ???MHz
	cfg.post_cb = spidrv_post_cb_relsem;
	cfg.queue_size = 1;
	spi_device_handle_t hdl;
	if (esp_err_t e = spi_bus_add_device(host,&cfg,&hdl)) {
		log_warn(TAG,"device add failed: %s",esp_err_to_name(e));
		return 0;
	}
	Instance = new XPT2046(cfg.spics_io_num, intr, hdl);
	return Instance;
}


unsigned XPT2046::cyclic(void *arg)
{
	XPT2046 *drv = (XPT2046 *) arg;
	if ((drv->m_pwst == pw_on) || (drv->m_pwreq != pw_inv))
		drv->readRegs();
	if (drv->m_pwst == pw_on)
		return 50;
	if (drv->m_pwst == pw_sleep)
		return 100;
	return 200;
}


const char *XPT2046::exeCmd(struct Terminal &t, int argc, const char **args)
{
	if (argc == 0) {
		const char *pwr[] = { "invalid", "off", "sleep", "on" };
		t.printf("x: %u..%u, y: %u..%u\n",m_lx,m_ux,m_ly,m_uy);
		t.printf("z: release < %u, pressed > %u\n",m_lz,m_uz);
		t.printf("power: %s\n",pwr[m_pwst]);
	} else if (argc == 1) {
		if (!strcmp(args[0],"sleep")) {
			m_pwreq = pw_sleep;
		} else if (!strcmp(args[0],"off")) {
			m_pwreq = pw_off;
		} else if (!strcmp(args[0],"on")) {
			m_pwreq = pw_on;
		} else if (!strcmp(args[0],"persist")) {
			persist();
		} else if (!strcmp(args[0],"-h")) {
			t.printf("valid commands are: on, sleep, off, persist\n");
		} else {
			return "Invalid argumet #1.";
		}
	} else {
		return "Invalid argumet #1.";
	}
	return 0;
}


void XPT2046::persist()
{
	char tag[16];
	sprintf(tag,"xpt2046@%u",m_cs);
	uint16_t data[] = {
		m_lx, m_ux, m_ly, m_uy, m_lz, m_uz
	};
	nvm_store_blob(tag,(uint8_t*)data,sizeof(data));
}


int XPT2046::init()
{
	cyclic_add_task(m_name,cyclic,this,0);
	return 0;
}


void XPT2046::intrHandler(void *arg)
{
	XPT2046 *drv = (XPT2046 *) arg;
	drv->m_pwst = pw_on;
}


void XPT2046::readRegs()
{
	unsigned x = 0, y = 0, z = 0;
	uint8_t pflags = 0;
	switch (m_pwreq) {
	case pw_inv:
		break;
	case pw_sleep:
		pflags = BIT_PD_ADC;
		m_pwst = pw_sleep;
		m_pwreq = pw_inv;
		break;
	case pw_off:
		pflags = BIT_PD_BOTH;
		m_pwst = pw_off;
		m_pwreq = pw_inv;
		break;
	case pw_on:
		m_pwst = pw_on;
		m_pwreq = pw_inv;
		break;
	}
	for (unsigned i = 0; i < 8; ++i) {
		uint8_t txbuf[] =
			{ REG_X, 0x00
			, REG_Y, 0x00
			, REG_Z1, 0x00
			, 0x00		// terminating byte needed for readback
		};
		if (i == 7)
			txbuf[4] |= pflags;
		uint8_t rxbuf[sizeof(txbuf)];
		spi_transaction_t t;
		bzero(&t,sizeof(t));
		t.user = m_sem;
		t.tx_buffer = txbuf;
		t.rx_buffer = rxbuf;
		t.length = sizeof(txbuf) << 3;
		t.rxlength = sizeof(txbuf) << 3;
		if (esp_err_t e = spi_device_queue_trans(m_hdl,&t,portMAX_DELAY)) {
			log_warn(TAG,"error queuing read: %s",esp_err_to_name(e));
		} else {
			if (pdTRUE != xSemaphoreTake(m_sem,MUTEX_ABORT_TIMEOUT))
				abort_on_mutex(m_sem,"xpt2046");
//			log_hex(TAG,rxbuf,sizeof(rxbuf),"read regs");
		}
		x += (rxbuf[1] << 4) | (rxbuf[2] >> 4);
		y += (rxbuf[3] << 4) | (rxbuf[4] >> 4);
		z += (rxbuf[5] << 4) | (rxbuf[6] >> 4);
	}
	x >>= 3;
	y >>= 3;
	z >>= 3;
	log_dbug(TAG,"x=%u, y=%u, z=%u",x,y,z);
	if (z > m_uz) {
		if ((m_lx == 0) || (x < m_lx))
			m_lx = x;
		if ((m_ux == 0) || (x > m_ux))
			m_ux = x;
		if ((m_ly == 0) || (y < m_ly))
			m_ly = y;
		if ((m_uy == 0) || (y > m_uy))
			m_uy = y;
		if (!m_pressed) {
			m_pressed = true;
			m_p.set(true);
			event_trigger(m_evp);
		}
		float a0 = 0, a1 = 0;
		if (m_lx != m_ux)
			a0 = ((float)(x-m_lx))/((float)(m_ux-m_lx))*100.0;
		m_a0.set(a0);
		if (m_lx != m_ux)
			a1 = ((float)(y-m_ly))/((float)(m_uy-m_ly))*100.0;
		m_a1.set(a1);
		log_dbug(TAG,"a0 %g, a1 %g",a0,a1);
		if (a0 < 0) {
			log_warn(TAG,"a0 = %g",a0);
			a0 = 0;
		} else if (a0 > 1000) {
			log_warn(TAG,"a0 = %g",a0);
			a0 = 100;
		}
		if (a1 < 0) {
			log_warn(TAG,"a1 = %g",a1);
			a1 = 0;
		} else if (a1 > 100) {
			log_warn(TAG,"a1 = %g",a1);
			a1 = 100;
		}
//		assert((a0 >= 0) && (a1 >= 0) && (a0 <= 100) && (a1 <= 100));
	} else if (z < m_lz) {
		if (m_pressed) {
			m_pressed = false;
			m_p.set(false);
			event_trigger(m_evr);
			m_a0.set(0);
			m_a1.set(0);
		}
	}
	m_rx.set(x);
	m_ry.set(y);
	m_rz.set(z);
	log_dbug(TAG,"x=%u y=%u, z=%u",x,y,z);
}


void XPT2046::sleep(void *arg)
{
	XPT2046 *drv = (XPT2046 *) arg;
	drv->m_pwreq = pw_sleep;
}


void XPT2046::wake(void *arg)
{
	XPT2046 *drv = (XPT2046 *) arg;
	drv->m_pwreq = pw_on;
}


#endif // CONFIG_XPT2046
