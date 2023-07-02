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

#ifdef CONFIG_SPI

#include "env.h"
#include "hwcfg.h"
#include "globals.h"
#include "ili9341.h"
#include "log.h"
#include "spidrv.h"
#include "ssd1309.h"
#include "sx1276.h"
#include "terminal.h"
#include "xpt2046.h"


#ifdef CONFIG_IDF_TARGET_ESP8266
#include <driver/spi.h>
#else
#include <driver/spi_master.h>
#endif
#include <strings.h>

#define TAG MODULE_SPI


static void spi_init_device(spi_host_device_t host, spidrv_t drv, int8_t cs, int8_t intr, int8_t reset, int8_t cd)
{
	spi_device_interface_config_t cfg;
	bzero(&cfg,sizeof(cfg));
	cfg.spics_io_num = cs;
	switch (drv) {
	case spidrv_invalid:
		break;
#ifdef CONFIG_SX1276
	case spidrv_sx1276:
		if (SX1276 *dev = SX1276::create(host,cfg,intr,reset)) {
			const SX1276Config &c = HWConf.sx1276();
			if (c.has_dio0())
				dev->setDio0(c.dio0());
			if (c.has_dio1())
				dev->setDio1(c.dio1());
			if (c.has_dio2())
				dev->setDio2(c.dio2());
			if (c.has_dio3())
				dev->setDio3(c.dio3());
			if (c.has_dio4())
				dev->setDio4(c.dio4());
			if (c.has_dio5())
				dev->setDio5(c.dio5());
		}
		break;
#endif
#ifdef CONFIG_SSD1309
	case spidrv_ssd1309:
		SSD1309::create(host,cfg,cd,reset);
		break;
#endif
#ifdef CONFIG_ILI9341
	case spidrv_ili9341:
		ILI9341::create(host,cfg,cd,reset);
		break;
#endif
#ifdef CONFIG_XPT2046
	case spidrv_xpt2046:
		XPT2046::create(host,cfg,intr);
		break;
#endif
	default:
		log_warn(TAG,"unknown device SPI device type %d",drv);
	}
}


const char *spicmd(Terminal &term, int argc, const char *args[])
{
	SpiDevice *s = SpiDevice::getFirst();
	if (argc == 1) {
		term.println("bus name");
		while (s) {
			term.printf("%3d  %s\n",s->getCS(),s->getName());
			s = s->getNext();
		}
		return 0;
	}
	while (s) {
		if (0 == strcmp(s->getName(),args[1]))
			return s->exeCmd(term,argc-2,args+2);
		s = s->getNext();
	}
	return "Invalid argument #1.";;
}


void spi_setup()
{
	for (const auto &c : HWConf.spibus()) {
		int8_t host = c.host();
		spi_bus_config_t cfg;
		bzero(&cfg,sizeof(cfg));
//		cfg.max_transfer_sz = 0;		// use defaults
		cfg.max_transfer_sz = 4096;
		cfg.flags = SPICOMMON_BUSFLAG_MASTER;
//		cfg.intr_flags = ESP_INTR_FLAG_IRAM;
		if (c.has_miso()) {	
			cfg.miso_io_num = c.miso();
			cfg.flags |= SPICOMMON_BUSFLAG_MISO;
		} else {
			cfg.miso_io_num = -1;
		}
		if (c.has_mosi()) {	
			cfg.mosi_io_num = c.mosi();
			cfg.flags |= SPICOMMON_BUSFLAG_MOSI;
		} else {
			cfg.mosi_io_num = -1;
		}
		if (c.has_sclk()) {	
			cfg.sclk_io_num = c.sclk();
			cfg.flags |= SPICOMMON_BUSFLAG_SCLK;
		} else {
			cfg.sclk_io_num = -1;
		}
		if (c.has_wp() && c.has_hold()) {
			cfg.quadwp_io_num = c.wp();
			cfg.quadhd_io_num = c.hold();
			cfg.flags |= SPICOMMON_BUSFLAG_WPHD;
		} else {
			cfg.quadwp_io_num = -1;
			cfg.quadhd_io_num = -1;
		}
		if (esp_err_t e = spi_bus_initialize((spi_host_device_t)host,&cfg,c.has_dma() ? (spi_common_dma_t)c.dma() : SPI_DMA_CH_AUTO)) {
			log_warn(TAG,"error initializing SPI host %u: %s",host,esp_err_to_name(e));
		} else {
			log_info(TAG,"initialized SPI%u",host);
			for (auto &d : c.devices()) {
				spi_init_device((spi_host_device_t)host,d.drv(),d.cs(),d.intr(),d.reset(),d.cd());
			}
		}
	}
	SpiDevice *d = SpiDevice::getFirst();
	while (d) {
		if (0 == d->init()) {
			EnvObject *o = new EnvObject(d->getName());
			d->attach(o);
			if (o->numChildren())
				RTData->add(o);
			else
				delete o;
		}
		d = d->getNext();
	}
}

#endif
