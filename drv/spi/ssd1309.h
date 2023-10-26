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

#ifndef SSD1309_H
#define SSD1309_H

#ifdef ESP32
#include "ssd130x.h"
#include "spidrv.h"

#include <driver/gpio.h>


struct ssd1309_trans_t {
	spi_transaction_t trans;
	SemaphoreHandle_t sem;
	gpio_num_t gpio;
	bool set,lvl;
};


class SSD1309 : public SSD130X, public SpiDevice
{
	public:
	enum hwcfg_t : uint8_t {
		hwc_iscan = 0x8,	// inverted scan
		hwc_altm = 0x10,	// alternating rows (non-sequential)
		hwc_rlmap = 0x20,	// right-to-left mapping
	};

#ifdef CONFIG_IDF_TARGET_ESP8266
	static SSD1309 *create(spi_host_t host, int8_t dc, int8_t reset);
#else
	typedef spi_host_device_t spi_host_t;
	static SSD1309 *create(spi_host_t host, spi_device_interface_config_t &cfg, int8_t dc, int8_t reset);
#endif
	int init(uint16_t maxx, uint16_t maxy, uint8_t options);
	void flush() override;
	int setBrightness(uint8_t contrast) override;
	int setInvert(bool inv) override;
	int setOn(bool on) override;
	const char *drvName() const
	{ return "ssd1309"; }

	bool hasAlpha() const override
	{ return true; }

	bool hasChar(char) const override
	{ return true; }

	static SSD1309 *getInstance()
	{ return Instance; }

	private:
	SSD1309(spi_host_t host, int8_t cs, uint8_t cd, int8_t r, struct spi_device_t *);
	int drawByte(uint16_t x, uint16_t y, uint8_t b);
	int drawBits(uint16_t x, uint16_t y, uint8_t b, uint8_t n);
	void drawHLine(uint16_t x, uint16_t y, uint16_t n);
	void drawVLine(uint16_t x, uint16_t y, uint16_t n);
	int drawChar(char c);
#ifndef CONFIG_IDF_TARGET_ESP8266
	static void postCallback(spi_transaction_t *t);
#endif
	typedef enum { pre_0, pre_c, pre_d } pre_t;
	spi_transaction_t *getTransaction(pre_t);
	int writeByte(uint8_t, pre_t = pre_c);
	int writeWord(uint8_t, uint8_t, pre_t = pre_c);
	int writeBytes(const uint8_t *data, unsigned len, pre_t pre);

	static SSD1309 *Instance;
	ssd1309_trans_t m_trans[8];
	uint8_t m_xtrans = 0;
	gpio_num_t m_dc, m_reset;
};

#endif	// ESP32
#endif

