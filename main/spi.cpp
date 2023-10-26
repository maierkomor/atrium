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
#include "fs.h"
#include "hwcfg.h"
#include "globals.h"
#include "ili9341.h"
#include "log.h"
#include "spidrv.h"
#include "ssd1309.h"
#include "sx1276.h"
#include "terminal.h"
#include "xpt2046.h"

#ifdef CONFIG_SDCARD
#include <driver/sdspi_host.h>
#include <sdmmc_cmd.h>
#include <esp_vfs.h>
#include <esp_vfs_fat.h>
#endif

#ifdef CONFIG_IDF_TARGET_ESP8266
#include <driver/spi.h>
#else
#include <driver/spi_master.h>
#endif
#include <strings.h>

#define TAG MODULE_SPI


#ifdef CONFIG_SDCARD
struct SDCard : public SpiDevice
{
	static SDCard *create(spi_host_device_t host, spi_device_interface_config_t &cfg, int8_t intr);
	void attach(class EnvObject *) override;
	const char *drvName() const override;
	int init() override;
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;

	private:
	SDCard(spi_host_device_t host, gpio_num_t cs)
	: SpiDevice(drvName(), cs)
	, m_host(host)
	{ }

	sdmmc_card_t *m_card = 0;
	spi_host_device_t m_host;
	bool m_mounted = false;
};

void SDCard::attach(class EnvObject *)
{
}

const char *SDCard::drvName() const
{
	return "sdcard";
}

int SDCard::init()
{
	log_info(TAG, "mounting sd-card");
	sdmmc_host_t sdh = SDSPI_HOST_DEFAULT();

	sdspi_device_config_t sdspi = SDSPI_DEVICE_CONFIG_DEFAULT();
	sdspi.gpio_cs = (gpio_num_t) m_cs;
	sdspi.host_id = m_host;

	esp_vfs_fat_sdmmc_mount_config_t mnt = {
		.format_if_mount_failed = false,
		.max_files = 4,
		.allocation_unit_size = 16 << 10,
		.disk_status_check_enable = false,
	};
	if (esp_err_t e = esp_vfs_fat_sdspi_mount("/sdcard", &sdh, &sdspi, &mnt, &m_card)) {
		log_warn(TAG,"mount failed: %s",esp_err_to_name(e));
		return e;
	}
	rootfs_add("/sdcard");
	m_mounted = true;
#if 0
	sdspi_dev_handle_t hdl;
	sdspi_device_config_t devcfg = SDSPI_DEVICE_CONFIG_DEFAULT();
	devcfg.gpio_cs = (gpio_num_t) m_cs;

	if (esp_err_t e = sdspi_host_init()) {
		log_warn(TAG,"sdspi host intt: %s",esp_err_to_name(e));
		return 1;
	}
	if (esp_err_t e = sdspi_host_init_device(&devcfg,&hdl)) {
		log_warn(TAG,"sdspi host intt device: %s",esp_err_to_name(e));
		return 1;
	}
	if (esp_err_t e = sdspi_host_set_card_clk(hdl,20000)) {
		log_warn(TAG,"sdspi card clock: %s",esp_err_to_name(e));
		return 1;
	}
#endif
	return 0;
}

const char *SDCard::exeCmd(struct Terminal &, int argc, const char **argv)
{
	return 0;
}


SDCard *SDCard::create(spi_host_device_t host, spi_device_interface_config_t &cfg, int8_t intr)
{
	return new SDCard(host, (gpio_num_t) cfg.spics_io_num);
}
#endif

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
#ifdef CONFIG_SDCARD
	case spidrv_sdcard:
		SDCard::create(host,cfg,intr);
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
		if (host == 0) {
			log_warn(TAG,"SPI host 0 cannot be used");
			continue;
		}
		spi_bus_config_t cfg;
		bzero(&cfg,sizeof(cfg));
//		cfg.max_transfer_sz = 0;		// use defaults
		cfg.max_transfer_sz = 4096;
		cfg.flags = SPICOMMON_BUSFLAG_MASTER;
//		cfg.intr_flags = ESP_INTR_FLAG_IRAM;
		if (c.has_miso()) {	
			cfg.miso_io_num = c.miso();
			cfg.flags |= SPICOMMON_BUSFLAG_MISO;
#ifdef CONFIG_IDF_TARGET_ESP32C3
			if (cfg.miso_io_num != 2)
				cfg.flags |= SPICOMMON_BUSFLAG_GPIO_PINS;
#endif
		} else {
			cfg.miso_io_num = -1;
		}
		if (c.has_mosi()) {	
			cfg.mosi_io_num = c.mosi();
			cfg.flags |= SPICOMMON_BUSFLAG_MOSI;
#ifdef CONFIG_IDF_TARGET_ESP32C3
			if (cfg.mosi_io_num != 7)
				cfg.flags |= SPICOMMON_BUSFLAG_GPIO_PINS;
#endif
		} else {
			cfg.mosi_io_num = -1;
		}
		if (c.has_sclk()) {	
			cfg.sclk_io_num = c.sclk();
			cfg.flags |= SPICOMMON_BUSFLAG_SCLK;
#ifdef CONFIG_IDF_TARGET_ESP32C3
			if (cfg.sclk_io_num != 6)
				cfg.flags |= SPICOMMON_BUSFLAG_GPIO_PINS;
#endif
		} else {
			cfg.sclk_io_num = -1;
		}
		if (c.has_wp() && c.has_hold()) {
			cfg.quadwp_io_num = c.wp();
			cfg.quadhd_io_num = c.hold();
			cfg.flags |= SPICOMMON_BUSFLAG_WPHD;
#ifdef CONFIG_IDF_TARGET_ESP32C3
			if (cfg.quadwp_io_num != 5)
				cfg.flags |= SPICOMMON_BUSFLAG_GPIO_PINS;
			if (cfg.quadhd_io_num != 4)
				cfg.flags |= SPICOMMON_BUSFLAG_GPIO_PINS;
#endif
		} else {
			cfg.quadwp_io_num = -1;
			cfg.quadhd_io_num = -1;
		}
		spi_host_device_t hdev = (spi_host_device_t)(host-1);
		spi_dma_chan_t dma = c.has_dma() ? (spi_common_dma_t)c.dma() : SPI_DMA_CH_AUTO;
		if (esp_err_t e = spi_bus_initialize(hdev,&cfg,dma)) {
			log_warn(TAG,"error initializing SPI host %u: %s",host,esp_err_to_name(e));
			continue;
		}
		log_info(TAG,"initialized SPI%u",host);
		for (auto &d : c.devices()) {
			spi_init_device(hdev,d.drv(),d.cs(),d.intr(),d.reset(),d.cd());
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
