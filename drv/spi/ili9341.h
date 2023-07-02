/*
 *  Copyright (C) 2023, Thomas Maier-Komor
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

#ifndef ILI9341_H
#define ILI9341_H

#ifdef ESP32
#include "display.h"
#include "spidrv.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <driver/gpio.h>


class ILI9341 : public MatrixDisplay, public SpiDevice
{
	public:
	~ILI9341();

	typedef spi_host_device_t spi_host_t;
	static ILI9341 *create(spi_host_t host, spi_device_interface_config_t &cfg, int8_t dc, int8_t reset);
	int init(uint16_t maxx, uint16_t maxy, uint8_t options);

	void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t c = -1) override;
	void drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data, int32_t fg = -1, int32_t bg = -1) override;
	void setPixel(uint16_t x, uint16_t y, int32_t c) override;
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
	void flush() override;
	int32_t getColor(color_t) const override;

	int setFont(unsigned f) override
	{
		m_font = (fontid_t) f;
		return 0;
	}

	int setBrightness(uint8_t contrast) override;
	int setInvert(bool inv) override;
	int setOn(bool on) override;

	const char *drvName() const
	{ return "ili9341"; }

	bool hasAlpha() const override
	{ return true; }

	bool hasChar(char) const override
	{ return true; }

	static ILI9341 *getInstance()
	{ return Instance; }

	void drawHLine(uint16_t x, uint16_t y, uint16_t n, int32_t col = -1) override;
	void drawVLine(uint16_t x, uint16_t y, uint16_t n, int32_t col = -1) override;
	unsigned drawText(uint16_t x, uint16_t y, const char *txt, int n, int32_t fg, int32_t bg) override;

	private:
	ILI9341(uint8_t cs, uint8_t cd, int8_t r, SemaphoreHandle_t sem, spi_device_handle_t hdl);
	uint8_t fontHeight() const;
	static void postCallback(spi_transaction_t *t);
	void setC();
	void setD();
	void sleepIn();
	void sleepOut();
	void reset();
	int readRegs(uint8_t reg, uint8_t *data, uint8_t num);
	int readBytes(uint8_t *data, unsigned len);
	int writeByte(uint8_t);
	int writeBytes(uint8_t *data, unsigned len);
	int readData(uint8_t *data, unsigned len);
	int writeData(uint8_t data);
	int writeData(uint8_t *data, unsigned len);
	int writeCmd(uint8_t v);
	int writeCmdArg(uint8_t v, uint8_t a);
	int writeCmdArg(uint8_t v, uint8_t *a, size_t n);
	void checkPowerMode();
	unsigned drawChars(const char *at, const char *e, int32_t fg, int32_t bg);
	void drawBitmapOffScr(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data, int32_t fg, int32_t bg);
	int setupOffScreen(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t bg);
	void commitOffScreen();

	static int readRegs(spi_device_handle_t hdl, uint8_t reg, uint8_t num, uint8_t *data, SemaphoreHandle_t);
	spi_transaction_t *getTransaction();

	static ILI9341 *Instance;
	uint32_t m_oss = 0;
	uint16_t m_colfg = 0xffff, m_colbg = 0;	// foreground/background color
	uint16_t *m_os = 0, *m_temp = 0;
	uint16_t m_osx = 0xffff, m_osy = 0xffff, m_osw, m_osh;
	spi_device_handle_t m_hdl;
	SemaphoreHandle_t m_sem;
	spi_transaction_t m_trans[8];
	uint8_t m_xtrans = 0;
	bool m_fos = false;	// full off-screen memory
	gpio_num_t m_dc, m_reset;
};


#endif	// ESP32
#endif
