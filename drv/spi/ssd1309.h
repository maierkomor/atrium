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

#include "ssd130x.h"
//#include "fonts.h"
#include "spidrv.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <driver/gpio.h>


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
	/*
	int drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data, int32_t fg, int32_t bg) override;
	void drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t col) override;

	int write(const char *t, int n) override;
	int writeHex(uint8_t, bool) override;
	int clear() override;
//	int clrEol() override;
	int flush() override;

	uint16_t maxX() const override
	{ return m_maxx; }

	uint16_t maxY() const override
	{ return m_maxy; }

	int setXY(uint16_t x, uint16_t y) override
	{
		if (x >= m_maxx)
			return -1;
		if (y >= m_maxy)
			return -1;
		m_posx = x;
		m_posy = y;
		return 0;
	}

	int setFont(int f) override
	{
		m_font = (fontid_t) f;
		return 0;
	}

	int setFont(const char *) override;
	*/

	void flush() override;
	int setBrightness(uint8_t contrast) override;
	//int setPos(uint16_t x, uint16_t y) override;
	int setInvert(bool inv) override;
	int setOn(bool on) override;
	const char *drvName() const
	{ return "ssd1309"; }

	bool hasAlpha() const override
	{ return true; }

	bool hasChar(char) const override
	{ return true; }

//	uint16_t numLines() const override;
//	uint16_t charsPerLine() const override;

	static SSD1309 *getInstance()
	{ return Instance; }

//	void setPixel(uint16_t x, uint16_t y, int32_t v) override;
//	int setPixel(uint16_t x, uint16_t y);
//	int clrPixel(uint16_t x, uint16_t y);

	private:
	SSD1309(spi_host_t host, int8_t cs, uint8_t cd, int8_t r, struct spi_device_t *);
	int drawBitmap_ssd1309(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data);
	int drawByte(uint16_t x, uint16_t y, uint8_t b);
	int drawBits(uint16_t x, uint16_t y, uint8_t b, uint8_t n);
	void drawHLine(uint16_t x, uint16_t y, uint16_t n);
	void drawVLine(uint16_t x, uint16_t y, uint16_t n);
	int drawChar(char c);
//	uint8_t fontHeight() const;
#ifndef CONFIG_IDF_TARGET_ESP8266
	static void postCallback(spi_transaction_t *t);
#endif
	void setC();
	void setD();
	int writeByte(uint8_t);
	int writeWord(uint8_t, uint8_t);
	int writeBytes(uint8_t *data, unsigned len);

	static SSD1309 *Instance;
//	uint8_t *m_disp = 0;
	SemaphoreHandle_t m_sem = 0;
	gpio_num_t m_dc, m_reset;
//	uint8_t m_maxx = 0, m_maxy = 0, m_posx = 0, m_posy = 0, m_dirty = 0xff;
//	uint8_t m_dirty = 0xff;
//	fontid_t m_font = font_native;
};


#endif

