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

#include "log.h"
#include "xio.h"

#include <limits.h>
#include <stdlib.h>
#include <strings.h>

#define TAG MODULE_XIO


const char *GpioDirStr[] = {
	"in", "out", "open-drain"
};

const char *GpioIntrTriggerStr[] = {
	"disabled",
	"raising edge",
	"falling edge",
	"any edge",
	"low level",
	"high level",
};

const char *GpioPullStr[] = {
	"none", "up", "down", "up+down"
};


#ifdef CONFIG_IOEXTENDERS

XioCluster **XioCluster::Instances = 0;
uint8_t XioCluster::NumInstances = 0, XioCluster::TotalIOs = 0;
uint8_t *XioCluster::IdMap = 0;


XioCluster::XioCluster()
: m_id(NumInstances)
, m_base(0)
{
	uint8_t n = NumInstances;
	++NumInstances;
	Instances = (XioCluster **)realloc(Instances,sizeof(XioCluster *)*NumInstances);
	Instances[n] = this;
}


XioCluster::~XioCluster()
{

}


XioCluster *XioCluster::getCluster(xio_t io)
{
	if (io < TotalIOs)
		return Instances[IdMap[io]];
	return 0;
}


XioCluster *XioCluster::getInstance(const char *n)
{
	for (XioCluster **i = Instances, **e = i + NumInstances; i != e; ++i) {
		if (0 == strcmp((*i)->getName(),n))
			return *i;
	}
	return 0;
}


unsigned XioCluster::getInstanceId(XioCluster *c)
{
	for (unsigned x = 0; x < NumInstances; ++x) {
		if (Instances[x] == c)
			return x;
	}
	abort();
	return UINT_MAX;
}


int XioCluster::attach(uint8_t base)
{
	unsigned numio = numIOs();
	log_dbug(TAG,"attach %s,%u+%u, TotalIOs=%u",getName(),base,numio,TotalIOs);
	if (base == 0)
		base = TotalIOs;
	unsigned nmax = base + numIOs();
	if (nmax > TotalIOs) {
		IdMap = (uint8_t *) realloc(IdMap,nmax);
		memset(IdMap+TotalIOs,0,nmax-TotalIOs);
	}

	unsigned e =  base+numio;
	const char *name = getName();
	for (unsigned s = base; s != e; ++s) {
		if (IdMap[s]) {
			log_warn(TAG,"attach %s failed: xio%u in use",name,s);
			return -1;
		}
		IdMap[s] = m_id;
	}
	TotalIOs = nmax;
	log_info(TAG,"%s %u..%u",name,base,e-1);
	m_base = base;
//	memset(IdMap + base,numio,m_id);
	return 0;
}


int XioCluster::getBase() const
{
	if (m_base)
		return m_base;
	if (IdMap[0] == m_id)
		return 0;
	return -1;
}


int xio_config(xio_t x, xio_cfg_t cfg)
{
	if (XioCluster *c = XioCluster::getCluster(x))
		return c->config(x-c->getBase(),cfg);
	log_warn(TAG,"xio_config: invalid io %u",x);
	return -1;
}


int xio_get_dir(xio_t x)
{
	if (XioCluster *c = XioCluster::getCluster(x))
		return c->get_dir(x-c->getBase());
	log_warn(TAG,"get_dir: invalid io %u",x);
	return -1;
}


int xio_get_lvl(xio_t x)
{
	if (XioCluster *c = XioCluster::getCluster(x))
		return c->get_lvl(x-c->getBase());
	log_warn(TAG,"get_lvl: invalid io %u",x);
	return -1;
}


int xio_set_hi(xio_t x)
{
//	log_dbug(TAG,"set_hi %u",x);
	if (XioCluster *c = XioCluster::getCluster(x))
		return c->set_hi(x-c->getBase());
	log_warn(TAG,"set_hi: invalid io %u",x);
	return -1;
}


int xio_set_lo(xio_t x)
{
//	log_dbug(TAG,"set_lo %u",x);
	if (XioCluster *c = XioCluster::getCluster(x))
		return c->set_lo(x-c->getBase());
	log_warn(TAG,"set_lo: invalid io %u",x);
	return -1;
}


int xio_set_lvl(xio_t x, xio_lvl_t l)
{
	log_dbug(TAG,"set_lvl %u,%u",x,l);
	if (XioCluster *c = XioCluster::getCluster(x))
		return c->set_lvl(x-c->getBase(),l);
	log_warn(TAG,"set_lvl: invalid io %u",x);
	return -1;
}


int xio_set_intr(xio_t x, xio_intrhdlr_t h, void *arg)
{
	log_dbug(TAG,"set intr xio%u",x);
	if (XioCluster *c = XioCluster::getCluster(x))
		return c->set_intr(x-c->getBase(),h,arg);
	log_warn(TAG,"set_intr: invalid io %u",x);
	return -1;
}


event_t xio_get_fallev(xio_t x)
{
	if (XioCluster *c = XioCluster::getCluster(x))
		return c->get_fallev(x-c->getBase());
	return 0;
}


event_t xio_get_riseev(xio_t x)
{
	if (XioCluster *c = XioCluster::getCluster(x))
		return c->get_riseev(x-c->getBase());
	return 0;
}

#else // !CONFIG_IOEXTENDERS

#include <rom/gpio.h>

static const gpio_mode_t Modes[] = { GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, GPIO_MODE_OUTPUT_OD };

int xio_config(xio_t gpio, xio_cfg_t cfg)
{
	if (cfg.cfg_io != xio_cfg_io_keep) {
		gpio_pad_select_gpio(gpio);
		gpio_mode_t m = Modes[cfg.cfg_io];
		if (esp_err_t e = gpio_set_direction(gpio,m)) {
			log_warn(TAG,"set dir %s on %u: %s",GpioDirStr[cfg.cfg_io],gpio,esp_err_to_name(e));
			return -1;
		}
		log_dbug(TAG,"set dir %s on %u",GpioDirStr[cfg.cfg_io]);

	}
	if (cfg.cfg_pull != xio_cfg_pull_keep) {
		if (gpio_set_pull_mode(gpio,(gpio_pull_mode_t)cfg.cfg_pull))
			return -1;
		log_dbug(TAG,"set pull %s on %u",GpioPullStr[cfg.cfg_pull]);
	}
	if (cfg.cfg_intr != xio_cfg_intr_keep) {
		if (gpio_set_intr_type(gpio,(gpio_int_type_t)cfg.cfg_intr))
			return -1;
		log_dbug(TAG,"set intr %s on %u",GpioIntrTriggerStr[cfg.cfg_intr]);
	}
	return 0;
}


void coreio_register()
{
	gpio_install_isr_service(0);
}


#endif
