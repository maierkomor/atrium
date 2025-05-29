/*
 *  Copyright (C) 2020-2025, Thomas Maier-Komor
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

#ifndef ONEWIRE_H
#define ONEWIRE_H

#include <esp_timer.h>
#include <sdkconfig.h>

#ifdef CONFIG_IDF_TARGET_ESP32S3
//#if defined CONFIG_SOC_RMT_GROUPS && (CONFIG_SOC_RMT_GROUPS > 0) && (CONFIG_SOC_RMT_CHANNELS_PER_GROUP > 1)
// - only S3 is tested
// - C3 fails with "received symbols truncated"
#define RMT_MODE
#include <driver/rmt_encoder.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#include <vector>

#include "xio.h"

class OneWire
{
	public:
	static OneWire *create(unsigned bus, bool pullup, int8_t pwr = -1);

	int sendCommand(uint64_t id, uint8_t command);
	void readBytes(uint8_t *, size_t n);
	uint8_t writeByte(uint8_t);
	int resetBus();
	int scanBus();
	int readRom();
	void setPower(bool);

	static OneWire *getInstance()
	{ return Instance; }

	static uint8_t crc8(const uint8_t *in, size_t len);

	protected:

	private:
	OneWire(xio_t bus, xio_t pwr);
	int addDevice(uint64_t);
	int searchRom(uint64_t &id, std::vector<uint64_t> &collisions);
	int writeBits(uint8_t b);
	int readBit(bool = true);
	void sendBytes(uint8_t *b, size_t n);
	uint64_t searchId(uint64_t &xid, std::vector<uint64_t> &coll);

	xio_t m_bus, m_pwr;
	bool m_pwron = false;
#ifdef RMT_MODE
	typedef enum owerr_e { owe_none = 0, owe_decode } owerr_t;
	rmt_channel_handle_t m_rx, m_tx;
	QueueHandle_t m_q;
	owerr_t m_err;
	rmt_symbol_word_t m_rxsym[10*8];

	int queryBits(bool x, bool v);
	static bool ow_rmt_done_cb(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *arg);
	static bool reset_cb(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *arg);
#endif

	static OneWire *Instance;
};


#endif
