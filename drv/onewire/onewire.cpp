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


#include "onewire.h"	// includes #include <sdkconfig.h>

#ifdef CONFIG_ONEWIRE

#include "log.h"
#include "owdevice.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <freertos/portmacro.h>
#include <freertos/task.h>

#if 0
#define log_devel log_dbug
#else
#define log_devel(...)
#endif

#if IDF_VERSION >= 50
#define ets_delay_us esp_rom_delay_us
#endif

#ifdef ESP32
#include <rom/crc.h>
#define ENTER_CRITICAL() portDISABLE_INTERRUPTS()
#define EXIT_CRITICAL() portENABLE_INTERRUPTS()
#elif defined CONFIG_IDF_TARGET_ESP8266
#include <esp8266/gpio_struct.h>
#include <esp_crc.h>
#define ENTER_CRITICAL() portENTER_CRITICAL()
#define EXIT_CRITICAL() portEXIT_CRITICAL()
#else
#error unknown target
#endif

using namespace std;

#define OW_MATCH_ROM		0x55
#define OW_SKIP_ROM		0xCC
#define OW_SEARCH_ROM		0xF0
#define OW_READ_ROM		0x33
#define OW_CONDITIONAL_SEARCH	0xEC
#define OW_OVERDRIVE_SKIP_ROM	0x3C
#define OW_OVERDRIVE_MATCH_ROM	0x69
#define OW_SHORT_CIRCUIT	0xFF
#define OW_SEARCH_FIRST		0xFF
#define OW_PRESENCE_ERR		0x01
#define OW_DATA_ERR		0xFE
#define OW_LAST_DEVICE		0x00

#define OW_THRESHOLD_1		15
#define OW_BASE_FREQ		1000000
#define OW_QUEUE_LEN		16
#define OW_RESET_THRESH		55
#ifdef CONFIG_IDF_TARGET_ESP32C3
#define OW_MEM_BLOCKS 48
#else
#define OW_MEM_BLOCKS 480
#endif

#define TAG MODULE_OWB

// reset:
// pull low for >= 480us
// wait 15-60us
// read level within 60-240us
// write 0:
// pull low 60-240us
// write 1:
// pull low >1us, let rise within 15us, keep high 45us
// read :
// pulse low >1us
// sample after 14us to 60us => sample 30us after pulse low

OneWire *OneWire::Instance = 0;

#ifdef RMT_MODE
//static rmt_symbol_word_t RxSyms[10*8];
rmt_encoder_handle_t EncodeBytes, EncodeCopy;
static const rmt_symbol_word_t SymRst = {
	.duration0 = 500,
	.level0 = 0,
	.duration1 = 280,
	.level1 = 1,
};
static const rmt_symbol_word_t Sym0 = {
	.duration0 = 60,
	.level0 = 0,
	.duration1 = 5,
	.level1 = 1,
};
static const rmt_symbol_word_t Sym1 = {
	.duration0 = 1,	// 240 max
	.level0 = 0,
	.duration1 = 70,
	.level1 = 1,
};
static const rmt_receive_config_t RxCfg = {	// valid times are 1-600us
	.signal_range_min_ns =   1000,
	.signal_range_max_ns = 600000,
};
static const rmt_transmit_config_t TxCfg = {
	.loop_count = 1,
	.flags = {
		.eot_level = 1,
		.queue_nonblocking = 0,
	},
};
static const rmt_bytes_encoder_config_t BencCfg = {
	.bit0 = Sym0,
	.bit1 = Sym1,
	.flags = 0,
};
#endif


/* 
 * imported from OneWireNg, BSD-2 license
 */
uint8_t OneWire::crc8(const uint8_t *in, size_t len)
{
	static const uint8_t CRC8_16L[] = {
		0x00, 0x5e, 0xbc, 0xe2, 0x61, 0x3f, 0xdd, 0x83,
		0xc2, 0x9c, 0x7e, 0x20, 0xa3, 0xfd, 0x1f, 0x41
	};
	static const uint8_t CRC8_16H[] = {
		0x00, 0x9d, 0x23, 0xbe, 0x46, 0xdb, 0x65, 0xf8,
		0x8c, 0x11, 0xaf, 0x32, 0xca, 0x57, 0xe9, 0x74
	};
	uint8_t crc = 0;
	while (len--) {
		crc ^= *in++;
		crc = CRC8_16L[(crc & 0x0f)] ^ CRC8_16H[(crc >> 4)];
	}
	return crc;
}


OneWire::OneWire(xio_t bus, xio_t pwr)
: m_bus(bus)
, m_pwr(pwr)
#ifdef RMT_MODE
, m_q(xQueueCreate(1,sizeof(rmt_rx_done_event_data_t)))
#endif
{
	Instance = this;
	log_info(TAG,"1-wire on GPIO%u with"
#ifdef RMT_MODE
			" RMT"
#else
			"out RMT"
#endif
			,bus);
	if (pwr != XIO_INVALID) {
		log_info(TAG,"power at %u",pwr);
		setPower(true);
	}
}


#ifdef RMT_MODE
static void log_symbols(const char *src, const rmt_rx_done_event_data_t *edata)
{
	log_dbug(TAG,"%s: num symbols %u",src,edata->num_symbols);
	for (size_t x = 0; x < edata->num_symbols; ++x) {
		log_dbug(TAG,"sym[%u]: %u@%u:%u@%u"
			, x
			, edata->received_symbols[x].duration0
			, edata->received_symbols[x].level0
			, edata->received_symbols[x].duration1
			, edata->received_symbols[x].level1
		);
	}
}


// return true if high prio task has been woken by this callback
bool OneWire::ow_rmt_done_cb(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *arg)
{
	BaseType_t w = pdFALSE;
	QueueHandle_t q = (QueueHandle_t) arg;
	xQueueSendFromISR(q,edata,&w);
	return w;
}
#endif


OneWire *OneWire::create(unsigned bus, bool pullup, int8_t pwr)
{
	assert(Instance == 0);	// currently only one 1-wire bus is supported
	// idle bus is pulled up
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_od;
	cfg.cfg_pull = pullup ? xio_cfg_pull_up : xio_cfg_pull_none;
	cfg.cfg_initlvl = xio_cfg_initlvl_high;
	if (0 > xio_config((xio_t)bus,cfg)) {
		log_warn(TAG,"failed to config xio%u",bus);
		return 0;
	}
	xio_t p = XIO_INVALID;
	if (pwr != -1) {
		cfg.cfg_io = xio_cfg_io_out;
		cfg.cfg_pull = xio_cfg_pull_none;
		if (0 > xio_config((xio_t)pwr,cfg)) {
			log_warn(TAG,"failed to config power xio%u",bus);
		} else {
			p = (xio_t) pwr;
		}
	}
#ifdef RMT_MODE
	if (esp_err_t e = rmt_new_bytes_encoder(&BencCfg, &EncodeBytes))
		log_warn(TAG,"create byte encoder: %s",esp_err_to_name(e));
	rmt_copy_encoder_config_t cpenccfg = {};
	if (esp_err_t e = rmt_new_copy_encoder(&cpenccfg, &EncodeCopy))
		log_warn(TAG,"create copy encoder: %s",esp_err_to_name(e));
	rmt_channel_handle_t rx;
	rmt_rx_channel_config_t rxccfg = {
		.gpio_num = (gpio_num_t) bus,
		.clk_src = RMT_CLK_SRC_DEFAULT,
		.resolution_hz = OW_BASE_FREQ,
		.mem_block_symbols = CONFIG_SOC_RMT_MEM_WORDS_PER_CHANNEL, //32,//480, // for 10*8 of DS18B20, multiple of 48 for C3
		.flags = {
			.invert_in = 0,
			.with_dma = 1,
			.io_loop_back = 1,
		},
		.intr_priority = 0,
	};
	if (esp_err_t e = rmt_new_rx_channel(&rxccfg,&rx)) {
		if ((ESP_ERR_NOT_SUPPORTED == e) || (ESP_ERR_NOT_FOUND == e)) {
			rxccfg.flags.with_dma = 0;
			rxccfg.mem_block_symbols = OW_MEM_BLOCKS; // for 10*8 of DS18B20, multiple of 48 for C3
			if (esp_err_t e = rmt_new_rx_channel(&rxccfg,&rx)) {
				log_warn(TAG,"create rx channel: %s",esp_err_to_name(e));
				return 0;
			}
			log_warn(TAG,"disabled RX DMA");
		} else {
			log_warn(TAG,"create rx channel: %s",esp_err_to_name(e));
			return 0;
		}
	}
	rmt_channel_handle_t tx;
	rmt_tx_channel_config_t txccfg = {
		.gpio_num = (gpio_num_t) bus,
		.clk_src = RMT_CLK_SRC_DEFAULT,
		.resolution_hz = OW_BASE_FREQ,
		.mem_block_symbols = CONFIG_SOC_RMT_MEM_WORDS_PER_CHANNEL,//480, // for 10*8 bit of DS18B20, multiple of 48 for C3
		.trans_queue_depth = OW_QUEUE_LEN,
		.intr_priority = 0,
		.flags = {
			.invert_out = 0,
			.with_dma = 1,
			.io_loop_back = 1,
			.io_od_mode = 1,
		}
	};
	if (esp_err_t e = rmt_new_tx_channel(&txccfg,&tx)) {
		if ((ESP_ERR_NOT_SUPPORTED == e) || (ESP_ERR_NOT_FOUND == e)) {
			txccfg.flags.with_dma = 0;
			txccfg.mem_block_symbols = OW_MEM_BLOCKS; // for 10*8 bit of DS18B20, multiple of 48 for C3
			if (esp_err_t e = rmt_new_tx_channel(&txccfg,&tx)) {
				log_warn(TAG,"create tx channel: %s",esp_err_to_name(e));
				return 0;
			}
			log_warn(TAG,"disabled TX DMA");
		} else {
			log_warn(TAG,"create tx channel: %s",esp_err_to_name(e));
			return 0;
		}
	}
	OneWire *ow = new OneWire((xio_t)bus,p);
	rmt_rx_event_callbacks_t cb = { ow_rmt_done_cb };
	if (esp_err_t e = rmt_rx_register_event_callbacks(rx,&cb,ow->m_q)) {
		log_warn(TAG,"register callback: %s",esp_err_to_name(e));
	}
	if (esp_err_t e = rmt_enable(rx))
		log_warn(TAG,"enable rx: %s",esp_err_to_name(e));
	else if (esp_err_t e = rmt_enable(tx))
		log_warn(TAG,"enable tx: %s",esp_err_to_name(e));
	ow->m_tx = tx;
	ow->m_rx = rx;
#else
	OneWire *ow = new OneWire((xio_t)bus,p);
#endif
	return ow;
}


int OneWire::addDevice(uint64_t id)
{
	uint8_t crc = crc8((uint8_t*)&id,7);
	if (crc != (id >> 56)) {
		log_warn(TAG,"CRC error: calculated 0x%02x, received 0x%02x",(unsigned)crc,(unsigned)(id>>56));
		return 1;
	}
	if (OwDevice::getDevice(id)) {
		log_dbug(TAG,"device " IDFMT " is already registered",IDARG(id));
		return 1;
	}
	log_dbug(TAG,"add device " IDFMT,IDARG(id));
	return OwDevice::create(id);
}


void OneWire::setPower(bool on)
{
	if (m_pwr != XIO_INVALID) {
		xio_lvl_t l = (on ? xio_lvl_0 : xio_lvl_hiz);
		xio_set_lvl(m_pwr,l);
		m_pwron = on;
	}
}


int OneWire::scanBus()
{
	if (resetBus())
		log_warn(TAG,"no response on reset");
	vector<uint64_t> collisions;
	uint64_t id = 0;
	do {
		log_devel(TAG,"searchRom(" IDFMT ",%d)",IDARG(id),collisions.size());
		int e = searchRom(id,collisions);
		if (e > 0) {
			log_warn(TAG,"searchRom(" IDFMT ",%d):%d",IDARG(id),collisions.size(),e);
			return 1;	// error occured
		}
		if (e < 0)
			log_dbug(TAG,"no response");
		if (id)
			addDevice(id);
		if (collisions.empty()) {
			id = 0;
		} else {
			id = collisions.back();
			collisions.pop_back();
			vTaskDelay(10);
		}
	} while (id);
	return 0;
}

#ifdef RMT_MODE
void rx_sync_cb(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *arg)
{
	unsigned *num = (unsigned *) arg;
	*num = edata->num_symbols;
}


static void ow_decode(rmt_symbol_word_t *sym, size_t nsym, uint8_t *buf, size_t s)
{
	//log_dbug(TAG,"ow_decode %u",nsym);
	uint8_t b = 1;
	uint8_t *at = buf;
	while (nsym) {
		--nsym;
		//log_dbug(TAG,"symbol l0=%u, d0=%u, l1=%u, d1=%u",sym->level0,sym->duration0,sym->level1,sym->duration1);
		if (sym->duration0 > OW_THRESHOLD_1)
			*at &= ~b;
		else
			*at |= b;
		b <<= 1;
		if (0 == b) {
			b = 1;
			//log_dbug(TAG,"byte 0x%x",*at);
			++at;
		}
		++sym;
	}
	//log_hex(TAG,buf,at-buf,"ow_decode result");
}


int OneWire::readBit(bool t)
{
	bzero(m_rxsym,sizeof(m_rxsym));
	if (esp_err_t e = rmt_receive(m_rx,m_rxsym,sizeof(m_rxsym),&RxCfg)) {
		log_warn(TAG,"readBit rx: %s",esp_err_to_name(e));
		return -1;
	}
	if (esp_err_t e = rmt_transmit(m_tx,EncodeCopy,t?&Sym1:&Sym0,sizeof(Sym1),&TxCfg)) {
		log_warn(TAG,"readBit tx: %s",esp_err_to_name(e));
		return -1;
	}
	rmt_rx_done_event_data_t edata;
	if (pdPASS != xQueueReceive(m_q,&edata,pdMS_TO_TICKS(100))) {
		log_warn(TAG,"resetBus Q receive timeout");
		return -1;
	}
	//log_symbols("readBit",&edata);
	return (m_rxsym[0].duration1 > OW_THRESHOLD_1);
}


void OneWire::sendBytes(uint8_t *b, size_t n)
{
	log_hex(TAG,b,n,"sendBytes");
	assert(n <= sizeof(m_rxsym)/sizeof(m_rxsym[0])/8);
	bzero(m_rxsym,sizeof(m_rxsym));
	if (esp_err_t e = rmt_receive(m_rx,m_rxsym,sizeof(m_rxsym),&RxCfg)) {
		log_warn(TAG,"sendBytes rx: %s",esp_err_to_name(e));
		return;
	}
	if (esp_err_t e = rmt_transmit(m_tx,EncodeBytes,b,n,&TxCfg)) {
		log_warn(TAG,"sendBytes tx: %s",esp_err_to_name(e));
		return;
	}
	rmt_rx_done_event_data_t edata;
	if (pdPASS != xQueueReceive(m_q,&edata,pdMS_TO_TICKS(100))) {
		log_warn(TAG,"sendBytes Q timeout");
	} else {
		uint8_t r[n];
		bzero(r,n);
		log_symbols("sendBytes",&edata);
		ow_decode(edata.received_symbols,edata.num_symbols,r,n);
		if (memcmp(r,b,n)) {
			log_hex(TAG,r,n,"received");
		}
		memcpy(b,r,n);
	}
}


void OneWire::readBytes(uint8_t *b, size_t n)
{
	log_dbug(TAG,"readBytes(tx,0x%x)",n);
	memset(b,0xff,n);
	sendBytes(b,n);
}


int IRAM_ATTR OneWire::writeBits(uint8_t b)
{
	log_dbug(TAG,"writeBits 0x%x",b);
	bzero(m_rxsym,sizeof(m_rxsym));
	if (esp_err_t e = rmt_receive(m_rx,m_rxsym,sizeof(m_rxsym),&RxCfg)) {
		log_warn(TAG,"writeBits rx: %s",esp_err_to_name(e));
		return -1;
	}
	if (esp_err_t e = rmt_transmit(m_tx,EncodeBytes,&b,sizeof(b),&TxCfg)) {
		log_warn(TAG,"writeBits tx: %s",esp_err_to_name(e));
		return -1;
	}
	rmt_rx_done_event_data_t edata;
	if (pdPASS != xQueueReceive(m_q,&edata,pdMS_TO_TICKS(10))) {
		log_warn(TAG,"writeBits Q receive timeout");
	} else {
		//log_symbols("writeBits",&edata);
		ow_decode(edata.received_symbols,edata.num_symbols,&b,sizeof(b));
	}
	return b;
}


int IRAM_ATTR OneWire::queryBits(bool x, bool v)
{
	bzero(m_rxsym,sizeof(m_rxsym));
	rmt_symbol_word_t syms[x?3:2] = {
		Sym1, Sym1
	};
	if (x) {
		syms[0] = v ? Sym1 : Sym0;
		syms[2] = Sym1;
	}
	if (esp_err_t e = rmt_receive(m_rx,m_rxsym,sizeof(m_rxsym),&RxCfg)) {
		log_warn(TAG,"writeBits rx: %s",esp_err_to_name(e));
		return -1;
	}
	if (esp_err_t e = rmt_transmit(m_tx,EncodeCopy,syms,sizeof(syms),&TxCfg)) {
		log_warn(TAG,"queryBits tx: %s",esp_err_to_name(e));
		return -1;
	}
	rmt_rx_done_event_data_t edata;
	uint8_t b = 0;
	if (pdPASS != xQueueReceive(m_q,&edata,pdMS_TO_TICKS(30))) {
		log_warn(TAG,"queryBits Q receive timeout");
	} else {
		ow_decode(edata.received_symbols,edata.num_symbols,&b,sizeof(b));
		log_symbols("queryBits",&edata);
		log_dbug(TAG,"queryBits = %u",b);
	}
	return b;
}


IRAM_ATTR uint64_t OneWire::searchId(uint64_t &xid, vector<uint64_t> &coll)
{
	uint64_t x0 = 0, x1 = 0, id = xid;
	int r = 0;
	log_dbug(TAG,"searchId %llu",xid);
	bool pv = false;
	int bits = queryBits(false,false);
	bits <<= 1;
	for (unsigned b = 0; b < 64; ++b) {
		uint8_t t0 = (bits >> 1) & 1;
		uint8_t t1 = (bits >> 2) & 1;
//		log_dbug(TAG,"t0=%u,t1=%u",t0,t1);
		if (t0 & t1) {
			r = -1;	// no response
			break;
		}
		x0 |= (uint64_t)t0 << b;
		x1 |= (uint64_t)t1 << b;
		if (t0) {
			id |= 1LL<<b;
		}
		if ((t1|t0) == 0) {
			// collision
//			log_dbug(TAG,"collision id %x",id|1<<b);
			if ((id >> b) & 1) {	// collision seen before
				pv = true;
			} else {
				coll.push_back(id|1<<b);
				pv = false;
			}
		} else if (t0) {
			pv = true;
		} else {
			pv = false;
		}
		bits = queryBits(true,pv);
	}
	xid = id;
//	log_dbug(TAG,"x0=%016lx, x1=%016lx",x0,x1);
	return r;
}

#else

static IRAM_ATTR unsigned xmitBit(uint8_t bus, uint8_t b)
{
	// write 0:
	// pull low 60-240us
	// write 1:
	// pull low >1us, let rise within 15us, keep high 45us
	// read :
	// pulse low >1us
	// sample after 14us to 60us => sample 30us after pulse low
	// TESTED OK
	// idle bus is input pull-up
	unsigned d,r;
	ets_delay_us(1);
	xio_set_lo(bus);
	if (b) {
		ets_delay_us(1);
		xio_set_hiz(bus);
		ets_delay_us(20);
		r = xio_get_lvl(bus);
		d = 40;
	} else {
		ets_delay_us(60);
		xio_set_hiz(bus);
		d = 5;
		r = 0;
	}
	ets_delay_us(d);
//	log_dbug(TAG,"xmit(%d): %d",b,r);
	return r;
}


IRAM_ATTR int OneWire::writeBits(uint8_t byte)
{
//	log_dbug(TAG,"writeBits 0x%02x",byte);
	ENTER_CRITICAL();
	uint8_t r = 0;
	for (uint8_t b = 1; b; b<<=1) {
		if (xmitBit(m_bus, byte & b))
			r |= b;
	}
	EXIT_CRITICAL();
//	no debug here, as writeBits is used in timinig critical sections!
	return r;
}


uint64_t OneWire::searchId(uint64_t &xid, vector<uint64_t> &coll)
{
	uint64_t x0 = 0, x1 = 0, id = xid;
	int r = 0;
	ENTER_CRITICAL();
	for (unsigned b = 0; b < 64; ++b) {
		uint8_t t0 = xmitBit(m_bus, 1);
		uint8_t t1 = xmitBit(m_bus, 1);
//		log_dbug(TAG,"t0=%u,t1=%u",t0,t1);
		if (t0 & t1) {
			r = -1;	// no response
			break;
		}
		x0 |= (uint64_t)t0 << b;
		x1 |= (uint64_t)t1 << b;
		if (t0) {
			id |= 1LL<<b;
		}
		if ((t1|t0) == 0) {
			// collision
//			log_dbug(TAG,"collision id %x",id|1<<b);
			if ((id >> b) & 1) {	// collision seen before
				xmitBit(m_bus, 1);
			} else {
				coll.push_back(id|1<<b);
				xmitBit(m_bus, 0);
			}
		} else {
			xmitBit(m_bus, t0);
		}
	}
	EXIT_CRITICAL();
	xid = id;
//	log_dbug(TAG,"x0=%016lx, x1=%016lx",x0,x1);
	return r;
}
#endif


int OneWire::searchRom(uint64_t &id, vector<uint64_t> &coll)
{
	if (resetBus()) {
		log_warn(TAG,"reset failed");
		return OW_PRESENCE_ERR;
	}
	setPower(false);
	uint8_t c = writeBits(OW_SEARCH_ROM);
	if (OW_SEARCH_ROM != c) {
		log_warn(TAG,"search ROM command failed: %02x",c);
		return 1;
	}
	size_t nc = coll.size();
	log_dbug(TAG,"search id %lx",id);
	int r = searchId(id,coll);
	setPower(true);
	if (Modules[TAG]) {
		while (nc < coll.size()) {
			uint64_t c = coll[nc++];
			log_dbug(TAG,"collision %08lx%08lx",(uint32_t)(c>>32),(uint32_t)c);
		}
	}
//	log_dbug(TAG,"x0=%016lx, x1=%016lx",x0,x1);
	return r;
}


int OneWire::readRom()
{
	if (resetBus()) {
		log_warn(TAG,"reset failed");
		return OW_PRESENCE_ERR;
	}
	uint8_t r = writeBits(OW_READ_ROM);
	if (OW_READ_ROM != r) {
		log_warn(TAG,"search ROM error: %02x",r);
		return 1;
	}
	uint8_t id[8];
	readBytes(id,sizeof(id));
	uint8_t crc = crc8(id,7);
	if (crc == id[7])
		log_dbug(TAG,"CRC %02x ok",crc);
	else
		log_warn(TAG,"CRC mismatch: received %02x, calculated %02x",id[7],crc);
	log_hex(TAG,id,sizeof(id),"id:");
	uint64_t id64 = 0;
	for (int i = 0; i < sizeof(id); ++i)
		id64 |= (uint64_t)id[i] << (i<<3);
	//log_dbug(TAG,"id64 0x%llx",id64);
	addDevice(id64);
	//log_hex(TAG,id,sizeof(id),"read ROM");
	return 0;
}


int OneWire::resetBus(void)
{
#ifdef RMT_MODE
	// reset:
	// pull low for >= 480us
	// wait 15-60us
	// read level within 60-240us
	bzero(m_rxsym,sizeof(m_rxsym));
	if (esp_err_t e = rmt_receive(m_rx,m_rxsym,sizeof(m_rxsym),&RxCfg)) {
		log_warn(TAG,"resetBus rx: %s",esp_err_to_name(e));
		return -1;
	}
	if (esp_err_t e = rmt_transmit(m_tx,EncodeCopy,&SymRst,sizeof(SymRst),&TxCfg)) {
		log_warn(TAG,"resetBus tx: %s",esp_err_to_name(e));
		return -1;
	}
	rmt_rx_done_event_data_t edata;
	if (pdPASS != xQueueReceive(m_q,&edata,pdMS_TO_TICKS(100))) {
		log_warn(TAG,"resetBus Q receive timeout");
		return -1;
	}
	if (Modules[0]||Modules[TAG]) {
		//log_hex(TAG,edata.received_symbols,sizeof(edata.received_symbols[0])*edata.num_symbols,"num symbols %u",edata.num_symbols);
		log_dbug(TAG,"reset: num symbols %u",edata.num_symbols);
		for (size_t x = 0; x < edata.num_symbols; ++x) {
			log_dbug(TAG,"reset: sym[%u]: %u@%u:%u@%u"
				, x
				, edata.received_symbols[x].duration0
				, edata.received_symbols[x].level0
				, edata.received_symbols[x].duration1
				, edata.received_symbols[x].level1
			);
		}
	}
	if (edata.num_symbols >= 2) {
		if ((edata.received_symbols[0].level0 == 0) && (edata.received_symbols[0].duration0 > OW_RESET_THRESH))
			return 0;
		if ((edata.received_symbols[0].level1 == 0) && (edata.received_symbols[0].duration1 > OW_RESET_THRESH))
			return 0;
		if ((edata.received_symbols[1].level0 == 0) && (edata.received_symbols[1].duration0 > OW_RESET_THRESH))
			return 0;
		if ((edata.received_symbols[1].level1 == 0) && (edata.received_symbols[1].duration1 > OW_RESET_THRESH))
			return 0;
	}
	log_dbug(TAG,"no response");
	return ESP_ERR_NOT_FOUND;
#else
	assert((m_pwr == XIO_INVALID) || (m_pwron == true));
	ENTER_CRITICAL();
	xio_set_lo(m_bus);
	ets_delay_us(500);
	xio_set_hiz(m_bus);
	ets_delay_us(20);
	int r = 1;
	for (int i = 0; i < 26; ++i) {
		if (0 == xio_get_lvl(m_bus)) {
			r = 0;
		}
		ets_delay_us(10);
	}
	ets_delay_us(220);
	EXIT_CRITICAL();
	if (r)
		log_warn(TAG,"reset: no response");
	else
		log_dbug(TAG,"reset ok");
	return r;
#endif
}



uint8_t OneWire::writeByte(uint8_t byte)
{
	log_dbug(TAG,"writeByte 0x%02x",byte);
	return writeBits(byte);
}


#ifndef RMT_MODE
void OneWire::readBytes(uint8_t *b, size_t n)
{
	// read by sending 0xff
	while (n) {
		--n;
		*b++ = writeBits(0xFF);
	}
	//log_dbug(TAG,"readByte() = 0x%02x",r);
}
#endif


int OneWire::sendCommand(uint64_t id, uint8_t command)
{
	log_dbug(TAG,"sendCommand(" IDFMT ",%02x)",IDARG(id),command);
	setPower(false);
	if (id) {
#ifdef RMT_MODE
		uint8_t data[10], *d = data;
		*d++ = OW_MATCH_ROM;
		for (unsigned i = 0; i < sizeof(id); ++i) {
			*d++ = id&0xff;
			id >>= 8;
		}
		*d = command;
		sendBytes(data,sizeof(data));
#else
		uint8_t c = writeBits(OW_MATCH_ROM);            // to a single device
		if (c != OW_MATCH_ROM) {
			log_warn(TAG,"match rom command failed");
			return 1;
		}
		for (unsigned i = 0; i < sizeof(id); ++i) {
			writeBits(id&0xff);
			id >>= 8;
		}
		uint8_t tmp = writeBits(command);
		if (tmp != command) {
			log_warn(TAG,"readback %02x",tmp);
		}
#endif
	} else {
		writeBits(OW_SKIP_ROM);            // to all devices
		writeBits(command);
	}
	setPower(true);
	return 0;
}


#endif
