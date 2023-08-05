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

#ifdef CONFIG_SX1276

#include "actions.h"
#include "env.h"
#include "event.h"
#include "log.h"
#include "sx1276.h"
#include "terminal.h"
#include "xio.h"

#include <math.h>
#include <strings.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_timer.h>
#include <rom/ets_sys.h>


#define REG_FIFO			0x00
#define REG_OP_MODE			0x01
#define REG_FRF_MSB			0x06
#define REG_FRF_MID			0x07
#define REG_FRF_LSB			0x08
#define REG_PA_CONFIG			0x09
#define REG_OCP				0x0b
#define REG_LNA				0x0c
#define REG_DIO_MAPPING_1		0x40
#define REG_DIO_MAPPING_2		0x41
#define REG_VERSION			0x42
#define REG_TCXO			0x4b
#define REG_PA_DAC			0x4d
#define REG_FORMER_TEMP			0x5b
#define REG_AGC_REF			0x61
#define REG_AGC_THRESH1			0x62
#define REG_AGC_THRESH2			0x63
#define REG_AGC_THRESH3			0x64
#define REG_PLL				0x70


#define LORA_FIFO_ADDR_PTR		0x0d
#define LORA_FIFO_TX_BASE_ADDR		0x0e
#define LORA_FIFO_RX_BASE_ADDR		0x0f
#define LORA_FIFO_RX_CURRENT_ADDR	0x10
#define LORA_IRQ_FLAGS			0x12
#define LORA_FIFO_RX_BYTES_NB		0x13
#define LORA_PKT_RSSI_VALUE		0x1a
#define LORA_PKT_SNR_VALUE		0x1b
#define LORA_HOP_CHANNEL		0x1c
#define LORA_MODEM_CONFIG_1		0x1d
#define LORA_MODEM_CONFIG_2		0x1e
#define LORA_PREAMBLE_MSB		0x20
#define LORA_PREAMBLE_LSB		0x21
#define LORA_PAYLOAD_LENGTH		0x22
#define LORA_MODEM_CONFIG_3		0x26
#define LORA_RSSI_WIDEBAND		0x2c
#define LORA_DETECTION_OPTIMIZE		0x31
#define LORA_DETECTION_THRESHOLD	0x37
#define LORA_SYNC_WORD			0x39


#define FSKOOK_FIFO			0x00
#define FSKOOK_OP_MODE			0x01
#define FSKOOK_BITRATE_MSB		0x02
#define FSKOOK_BITRATE_LSB		0x03
#define FSKOOK_FDEV_MSB			0x04
#define FSKOOK_FDEV_LSB			0x05
#define FSKOOK_LR_OCP			0x0b
#define FSKOOK_LNA			0x0c
#define FSKOOK_RX_CONFIG		0x0d
#define FSKOOK_RSSI_CONFIG		0x0e
#define FSKOOK_RSSI_COLLISION		0x0f
#define FSKOOK_RSSI_THRESH		0x10
#define FSKOOK_RSSI_VALUE		0x11
#define FSKOOK_RX_BW			0x12
#define FSKOOK_AFC_BW			0x13
#define FSKOOK_OOK_PEAK			0x14
#define FSKOOK_OOK_FIX			0x15
#define FSKOOK_OOK_AVG			0x16
#define FSKOOK_AFC_FEI			0x1a
#define FSKOOK_AFC_MSB			0x1b
#define FSKOOK_AFC_LSB			0x1c
#define FSKOOK_FEI_MSB			0x1d
#define FSKOOK_FEI_LSB			0x1e
#define FSKOOK_PACKET_CONFIG		0x30
#define FSKOOK_PAYLOAD_LENGTH		0x32
#define FSKOOK_NODE_ADRS		0x33
#define FSKOOK_BROADCAST_ADRS		0x34
#define FSKOOK_TEMP			0x3c
#define FSKOOK_IRQ_FLAGS1		0x3e
#define FSKOOK_IRQ_FLAGS2		0x3f
#define FSKOOK_BITRATE_FRAC		0x5d


#define MODE_LONG_RANGE_MODE		0x80
#define MODE_SLEEP			0x00
#define MODE_STDBY			0x01
#define MODE_TX				0x03
#define MODE_RX_CONTINUOUS		0x05
#define MODE_RX_SINGLE			0x06

#define IRQ_TX_DONE_MASK		0x08
#define IRQ_PAYLOAD_CRC_ERROR_MASK	0x20
#define IRQ_RX_DONE_MASK		0x40

#define RFLR_INVERTIQ_RX_MASK		0xBF
#define RFLR_INVERTIQ_RX_OFF		0x00
#define RFLR_INVERTIQ_RX_ON		0x40
#define RFLR_INVERTIQ_TX_MASK		0xFE
#define RFLR_INVERTIQ_TX_OFF		0x01
#define RFLR_INVERTIQ_TX_ON		0x00

#define REG_LR_INVERTIQ			0x33

#define RFLR_INVERTIQ2_ON		0x19
#define RFLR_INVERTIQ2_OFF		0x1D

#define REG_LR_INVERTIQ2		0x3B

#define MAX_PKT_LENGTH			255

// RX_CONFIG
#define BIT_RESTART_RX_ON_COLLISSION	(1<<7)
#define BIT_RESTART_WITHOUT_PLL_LOCK	(1<<6)
#define BIT_RESTART_WITH_PLL_LOCK	(1<<5)
#define BIT_AFC_AUTO_ON			(1<<4)
#define BIT_AGC_AUTO_ON			(1<<3)
#define MODE_INTR_NONE			0
#define MODE_INTR_RX			1
#define MODE_INTR_PREAMBLE		6
#define MODE_INTR_RX_PREAMBLE		7

// PACKET_CONFIG
#define BIT_FIXED_LENGTH		(1<<7)
#define BIT_CRC_ON			(1<<4)

#define BIT_CRC_ON_PAYLOAD		(1<<6)
#define BIT_PLL_TIMEOUT			(1<<7)
#define BIT_RX_TIMEOUT			(1<<7)
#define BIT_RX_DONE			(1<<6)
#define BIT_PAYLOAD_CRC_ERROR		(1<<5)
#define BIT_VALID_HEADER		(1<<4)
#define BIT_TX_DONE			(1<<3)
#define BIT_CAD_DONE			(1<<2)
#define BIT_FHSS_CHANGE_CHANEL		(1<<1)
#define BIT_CAD_DETECTED		(1<<0)
#define BIT_OCP_ON			(1<<5)
#define BIT_OOK				(1<<5)
#define BIT_LORA			(1<<7)
#define BIT_PAYLOAD_READY		(1<<2)

#define MAX_PACKET_SIZE			58

#define LSB_SPREADING_FACTOR		4
#define LSB_GAIN			5
#define LSB_SPREADING_FACTOR		4
#define MASK_GAIN			0xe0
#define MASK_MODE			0x7
#define MASK_SPREADING_FACTOR		0xf0

#define BW_7_8		0
#define BW_10_4		1
#define BW_15_6		2
#define BW_20_8		3

#define TAG MODULE_SX1276

struct PaConfig
{
	uint8_t output_power : 4;
	uint8_t max_power : 3;
	uint8_t pa_select : 1;
};


SX1276 *SX1276::m_inst = 0;

// bandwith in kHz/10
static const uint16_t BW_dkHz[] = {
	780, 1040, 1560, 2080, 3125, 4170, 6250, 12500, 25000, 50000
};


static const char *ModeStrs[] = {
	"SLEEP", "STDBY", "FSTX", "TX", "FSRX", "RXCONT", "RXSINGLE", "CAD"
};

// heltec wireless stick:
// reset 14
// dio1 35
// dio2 34
// cs 18
SX1276::SX1276(spi_host_device_t host, spi_device_interface_config_t &cfg, int8_t intr, int8_t reset)
: SpiDevice("sx1276", cfg.spics_io_num)
, m_sem(xSemaphoreCreateBinary())
, m_reset(reset)
{
	bzero(m_iev,sizeof(m_iev));
	if (-1 != reset) {
		xio_cfg_t ioc = XIOCFG_INIT;
		ioc.cfg_io = xio_cfg_io_out;
		xio_config(reset,ioc);
		xio_set_lo(reset);
		ets_delay_us(10000);
		xio_set_hi(reset);
		ets_delay_us(10000);
	}
	cfg.command_bits = 1;
	cfg.address_bits = 7;
	cfg.cs_ena_pretrans = 0;
	cfg.clock_speed_hz = SPI_MASTER_FREQ_10M;
	cfg.queue_size = 1;
	cfg.post_cb = postCallback;
//	cfg.flags = ESP_INTR_FLAG_IRAM;
	cfg.flags = SPI_DEVICE_HALFDUPLEX;
	/*
	if (-1 != intr) {
		if (esp_err_t e = xio_set_intr(intr,intrHandler,this))
			log_warn(TAG,"error attaching interrupt: %s",esp_err_to_name(e));
	}
	*/
	if (esp_err_t e = spi_bus_add_device(host,&cfg,&m_hdl))
		log_warn(TAG,"device add failed: %s",esp_err_to_name(e));
	else
		m_inst = this;
}


SX1276 *SX1276::create(spi_host_device_t host, spi_device_interface_config_t &cfg, int8_t intr, int8_t reset)
{
	if (m_inst == 0) {
		return new SX1276(host,cfg,intr,reset);
	} else {
		log_warn(TAG,"Only single instance supported. Cannot instanciate at CS=%u",cfg.spics_io_num);
		return 0;
	}
}


void SX1276::setDio0(uint8_t gpio)
{
	if (esp_err_t e = gpio_set_intr_type((gpio_num_t)gpio,GPIO_INTR_NEGEDGE)) {
		log_warn(TAG,"cannot trigger on neg-edge on gpio%u: %s",gpio,esp_err_to_name(e));
	} else if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)gpio,dio0Handler,(void*)this)) {
		log_warn(TAG,"add isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
	} else {
		log_dbug(TAG,"attached dio0 to gpio%u",gpio);
		m_iev[0] = event_register(concat(m_name,"`dio0"));
		Action *ia = action_add(concat(m_name,"!intr"),intr_action,this,0);
		event_callback(m_iev[0],ia);
	}
}


void SX1276::setDio1(uint8_t gpio)
{
	if (esp_err_t e = gpio_set_intr_type((gpio_num_t)gpio,GPIO_INTR_NEGEDGE)) {
		log_warn(TAG,"cannot trigger on neg-edge on gpio%u: %s",gpio,esp_err_to_name(e));
	} else if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)gpio,dio1Handler,(void*)this)) {
		log_warn(TAG,"add isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
	} else {
		log_dbug(TAG,"attached dio1 to gpio%u",gpio);
		m_iev[1] = event_register(concat(m_name,"`dio1"));
	}
}


void SX1276::setDio2(uint8_t gpio)
{
	if (esp_err_t e = gpio_set_intr_type((gpio_num_t)gpio,GPIO_INTR_NEGEDGE)) {
		log_warn(TAG,"cannot trigger on neg-edge on gpio%u: %s",gpio,esp_err_to_name(e));
	} else if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)gpio,dio2Handler,(void*)this)) {
		log_warn(TAG,"add isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
	} else {
		log_dbug(TAG,"attached dio2 to gpio%u",gpio);
		m_iev[2] = event_register(concat(m_name,"`dio2"));
	}
}


void SX1276::setDio3(uint8_t gpio)
{
	if (esp_err_t e = gpio_set_intr_type((gpio_num_t)gpio,GPIO_INTR_NEGEDGE)) {
		log_warn(TAG,"cannot trigger on neg-edge on gpio%u: %s",gpio,esp_err_to_name(e));
	} else if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)gpio,dio3Handler,(void*)this)) {
		log_warn(TAG,"add isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
	} else {
		log_dbug(TAG,"attached dio3 to gpio%u",gpio);
		m_iev[3] = event_register(concat(m_name,"`dio3"));
	}
}


void SX1276::setDio4(uint8_t gpio)
{
	if (esp_err_t e = gpio_set_intr_type((gpio_num_t)gpio,GPIO_INTR_NEGEDGE)) {
		log_warn(TAG,"cannot trigger on neg-edge on gpio%u: %s",gpio,esp_err_to_name(e));
	} else if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)gpio,dio4Handler,(void*)this)) {
		log_warn(TAG,"add isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
	} else {
		log_dbug(TAG,"attached dio4 to gpio%u",gpio);
		m_iev[4] = event_register(concat(m_name,"`dio4"));
	}
}


void SX1276::setDio5(uint8_t gpio)
{
	if (esp_err_t e = gpio_set_intr_type((gpio_num_t)gpio,GPIO_INTR_NEGEDGE)) {
		log_warn(TAG,"cannot trigger on neg-edge on gpio%u: %s",gpio,esp_err_to_name(e));
	} else if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)gpio,dio5Handler,(void*)this)) {
		log_warn(TAG,"add isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
	} else {
		log_dbug(TAG,"attached dio5 to gpio%u",gpio);
		m_iev[5] = event_register(concat(m_name,"`dio5"));
	}
}


void SX1276::dio0Handler(void *arg)
{
	SX1276 *dev = (SX1276 *) arg;
	event_isr_trigger(dev->m_iev[0]);
}


void SX1276::dio1Handler(void *arg)
{
	SX1276 *dev = (SX1276 *) arg;
	event_isr_trigger(dev->m_iev[1]);
}


void SX1276::dio2Handler(void *arg)
{
	SX1276 *dev = (SX1276 *) arg;
	event_isr_trigger(dev->m_iev[2]);
}


void SX1276::dio3Handler(void *arg)
{
	SX1276 *dev = (SX1276 *) arg;
	event_isr_trigger(dev->m_iev[3]);
}


void SX1276::dio4Handler(void *arg)
{
	SX1276 *dev = (SX1276 *) arg;
	event_isr_trigger(dev->m_iev[4]);
}


void SX1276::dio5Handler(void *arg)
{
	SX1276 *dev = (SX1276 *) arg;
	event_isr_trigger(dev->m_iev[5]);
}


void SX1276::attach(EnvObject *root)
{

}


int SX1276::init()
{
	uint8_t ver;
	readRegs(REG_VERSION,sizeof(ver),&ver);
	log_info(TAG,"version: %u",ver);
	log_info(TAG,"freq: %dMHz",getFreq());

	writeReg(REG_PA_CONFIG,0);
	float pmax,pout;
	getMaxPower(pmax);
	getPower(pout);
	log_info(TAG,"power: max: %g, output %g",pmax,pout);

	log_info(TAG,"Imax: %dmA, %sabled"
		, getImax()
		, getOCP() ? "en" : "dis");

	setFreq(433);
	log_info(TAG,"freq: %dMHz",getFreq());
	setBitRate(2400);
	log_info(TAG,"bit-rate %dB/s",getBitRate());
	/*
	uint8_t ocp;
	readRegs(REG_OCP,sizeof(ocp),&ocp);
	unsigned imax;
	if ((ocp & 0x1f) <= 15) 
		imax = 45+5*(ocp&0x1f);
	else if ((ocp & 0x1f) <= 27) 
		imax = -30 + 10 * (ocp&0x1f);
	else
		imax = 240;
	log_info(TAG,"OCP %sabled, Imax %u mA"
			, ocp & (1<<5) ? "en" : "dis"
			, imax
		);
	uint8_t r;
	if (0 == readReg(LORA_MODEM_CONFIG_1,&r)) {
		log_info(TAG,"modem config: %splicit header, bandwidth %u.%02ukHz, 4/%u error coding rate"
			, r&1 ? "im" : "ex"
			, BW_dkHz[r>>4] / 100
			, BW_dkHz[r>>4] % 100
			, ((r>>1)&7)+4
		);
	}
	*/
	writeReg(LORA_IRQ_FLAGS,0xff);
	setLora(false);
	m_rev = event_register(concat(m_name,"`recv"));
	action_add(concat(m_name,"!send"),send_action,0,"set ");
	return 0;
}


void SX1276::intr_action(void *arg)
{
	SX1276 *dev = (SX1276 *) arg;
	dev->processIntr();
}


void SX1276::processIntr()
{
	log_dbug(TAG,"processIntr()");
	if (isLora()) {
		uint8_t r;
		readReg(LORA_IRQ_FLAGS,&r);
		log_dbug(TAG,"irq flags 0x%x",r);
		writeReg(LORA_IRQ_FLAGS,r);
		if (r & BIT_RX_DONE) {
			uint8_t n,a;
			readReg(LORA_FIFO_RX_BYTES_NB,&n);
			readReg(LORA_FIFO_RX_CURRENT_ADDR,&a);
			writeReg(LORA_FIFO_ADDR_PTR,a);
			Packet *p = (Packet *) malloc(sizeof(Packet)+n);
			uint8_t *d = p->data;
			for (int x = 0; x < n; ++x)
				readReg(REG_FIFO,d++);
			log_hex(TAG,p->data,n,"received packet");
			event_trigger_arg(m_rev,p);
		}
	} else {
		uint8_t r[2];
		readRegs(FSKOOK_IRQ_FLAGS1,sizeof(r),r);
		log_dbug(TAG,"irq flags 0x%02x 0x%02x",r[0],r[1]);
		if (r[1] & BIT_PAYLOAD_READY) {
			// TODO
		}
	}
}


void SX1276::send_action(void *arg)
{
	const char *a = (const char *) arg;
	m_inst->send(a,strlen(a));
}


bool SX1276::isLora()
{
	uint8_t r;
	readReg(REG_OP_MODE,&r);
	return (r & BIT_LORA) == BIT_LORA;
}


int SX1276::setLora(bool lora)
{
	uint8_t r;
	if (readReg(REG_OP_MODE,&r))
		return -1;
	if (((r&BIT_LORA) != 0) == lora) {
		return 0;
	}
	uint8_t m = r & MASK_MODE;
	r &= ~BIT_LORA;
	r &= ~MASK_MODE;
	if (lora)
		r |= BIT_LORA;
	else
		r &= ~BIT_LORA;
	if (writeReg(REG_OP_MODE,r))
		return -1;
	writeReg(REG_OP_MODE,r|m);
	if (lora) {
		getBitRate();
	} else {
		writeReg(FSKOOK_RX_CONFIG,BIT_AFC_AUTO_ON|BIT_AGC_AUTO_ON|MODE_INTR_RX_PREAMBLE);
		writeReg(FSKOOK_PACKET_CONFIG,BIT_CRC_ON);	// variable-length, no address filter
		writeReg(FSKOOK_PAYLOAD_LENGTH,0xff);	// maximum payload lenght
		getBandwidth();
	}
	return 0;
}


int SX1276::getOOK()
{
	uint8_t r;
	if (readReg(REG_OP_MODE,&r))
		return -1;
	return r & BIT_OOK;
}


int SX1276::setOOK(bool ook)
{
	uint8_t r;
	if (readReg(REG_OP_MODE,&r))
		return -1;
	if (ook)
		r |= BIT_OOK;
	else
		r &= ~BIT_OOK;
	return writeReg(REG_OP_MODE,r);
}


int SX1276::getMaxPower(float &pmax)
{
	PaConfig pac;
	if (readRegs(REG_PA_CONFIG,sizeof(pac),(uint8_t*)&pac))
		return -1;
	pmax = (float) pac.max_power * 0.6 + 10.8;
	log_dbug(TAG,"power mode %s, max %g, output %g"
			, pac.pa_select ? "boost" : "RFO"
			, pmax
			, pac.pa_select ? 17.0-(15.0-pac.output_power) : pmax-(15-pac.output_power)
		);
	return 0;
}


int SX1276::setMaxPower(float pmax)
{
	if ((pmax > 20) || (pmax < 10.8)) {
		log_warn(TAG,"valid maximum power range 10.8..20dBm");
		return -1;
	}
	PaConfig pac;
	if (readRegs(REG_PA_CONFIG,sizeof(pac),(uint8_t*)&pac))
		return -1;
	float pout;
	if (pmax > 15) {
		if (!pac.pa_select) {
			pac.pa_select = true;
			pout = pac.max_power-(15-pac.output_power);
			pac.output_power = 2 + pout;
			pac.max_power = 0;
		}
		pmax = 20;
	} else {
		if (pac.pa_select) {
			pac.pa_select = false;
		}
		pac.max_power = (pmax-10.8) / 0.6;
		pmax = (float) pac.max_power * 0.6 + 10.8;
	}
	if (writeReg(REG_PA_CONFIG,*(uint8_t*)&pac))
		return -1;
	log_dbug(TAG,"pmax = %g",pmax);
	return 0;
}


int SX1276::getPower(float &pout)
{
	PaConfig pac;
	if (readRegs(REG_PA_CONFIG,sizeof(pac),(uint8_t*)&pac))
		return -1;
	float pmax = (float) pac.max_power * 0.6 + 10.8;
	pout = pac.pa_select ? (2.0+pac.output_power) : pmax-(15-pac.output_power);
	// negative value plausible?
//	pout = pac.pa_select ? (2.0+pac.output_power) : (pmax-pac.output_power);
	log_dbug(TAG,"power mode %s, max %g, output %g"
			, pac.pa_select ? "boost" : "RFO"
			, pmax
			, pout
		);
	return 0;
}


int SX1276::setPower(float pout)
{
	if (pout > 20)
		return -1;
	PaConfig pac;
	if (readRegs(REG_PA_CONFIG,sizeof(pac),(uint8_t*)&pac))
		return -1;
	if (pac.pa_select) {
		if (pout < 2)
			return -1;
		pac.output_power = pout-2;
	} else if (pout > (pac.max_power*0.6+10.8)) {
		return -1;
	} else {
		float pmax = (float) pac.max_power * 0.6 + 10.8;
		pac.output_power = pout-pmax+15;
	}
	return writeReg(REG_PA_CONFIG,*(uint8_t*)&pac);
}


int SX1276::getFreq()
{
	uint8_t freq[3];
	int r = -1;
	if (0 == readRegs(REG_FRF_MSB,sizeof(freq),freq)) {
		uint32_t f = (freq[0]<<16)|(freq[1]<<8)|freq[0];
		r = (f<<5)/(1<<19);
		log_dbug(TAG,"freq: %uMHz",r);
	}
	return r;
}


int SX1276::setFreq(unsigned f)
{
	if (((m_opmode & MASK_MODE) != mode_stdb) && ((m_opmode & MASK_MODE) != mode_sleep)) {
		log_warn(TAG,"frequency can only be set in sleep or standby mode");
		return -1;
	}
	uint32_t f32 = (f * 1<<19) / 32;
	uint8_t freq[3] =
		{ (uint8_t) ((f32 >> 16) & 0xff)
		, (uint8_t) ((f32 >> 8) & 0xff)
		, (uint8_t) (f32 & 0xff)
	};
	if (0 == writeRegs(REG_FRF_MSB,sizeof(freq),freq)) {
		log_dbug(TAG,"freq: %uMHz, 0x%x",f,f32);
		return 0;
	}
	log_warn(TAG,"frequency set failed");
	return -1;
}


int SX1276::setImax(unsigned imax)
{
	uint8_t v;
	if (imax >= 240)
		v = 28;
	else if (imax >= 130)
		v = (imax+30)/10;
	else
		v = (imax-45)/5;
	uint8_t ocp;
	if (readRegs(REG_OCP,sizeof(ocp),&ocp))
		return -1;
	v |= (ocp & BIT_OCP_ON);
	if (ocp != v)
		return writeReg(REG_OCP,v);
	return 0;
}


int SX1276::getImax()
{
	uint8_t ocp;
	if (readReg(REG_OCP,&ocp))
		return -1;
	unsigned imax;
	if ((ocp & 0x1f) <= 15) 
		imax = 45+5*(ocp&0x1f);
	else if ((ocp & 0x1f) <= 27) 
		imax = -30 + 10 * (ocp&0x1f);
	else
		imax = 240;
	log_dbug(TAG,"OCP %sabled, Imax %u mA"
			, ocp & (1<<5) ? "en" : "dis"
			, imax
		);
	return imax;
}


int SX1276::setOCP(bool en)
{
	int r = -1;
	uint8_t ocp, ocpn;
	if (0 == readRegs(REG_OCP,sizeof(ocp),&ocp)) {
		if (en)
			ocpn = ocp | BIT_OCP_ON;
		else
			ocpn = ocp & ~BIT_OCP_ON;
		if (ocp != ocpn)
			r = writeReg(REG_OCP,ocpn);
		else
			r = 0;
	}
	if (r)
		log_warn(TAG,"setOCP(%u): failed",en);
	else
		log_dbug(TAG,"setOCP(%u)",en);
	return r;
}


// @return: 0=disable, 1=enabled, -1=I/O-error
int SX1276::getOCP()
{
	uint8_t ocp;
	if (0 == readReg(REG_OCP,&ocp))
		return ocp & BIT_OCP_ON;
	return -1;
}


int SX1276::getGain()
{
	uint8_t lna;
	if (0 == readReg(REG_LNA,&lna))
		return (lna & MASK_GAIN) >> LSB_GAIN;
	return -1;
}


int SX1276::setGain(unsigned g)
{
	if ((g < 1) || (g > 6))
		return -1;
	uint8_t lna;
	if (readReg(REG_LNA,&lna))
		return -1;
	lna &= ~MASK_GAIN;
	lna |= g << LSB_GAIN;
	return writeReg(REG_LNA,lna);
}


int SX1276::setMode(mode_t m)
{
	if (m > 7) {
		log_warn(TAG,"invalide mode %u",m);
		return -1;
	}
	uint8_t opmode = (m_opmode & ~0x7) | m;
	return writeReg(REG_OP_MODE,opmode);
}


//	FSK only
int SX1276::setBitRate(unsigned br)
{
	if (isLora()) {
		log_warn(TAG,"set bitrate only for FSK/OOK");
		return -1;
	}
	unsigned reg = 32000000/br;
	uint8_t v[2] = 
		{ (uint8_t)((reg >> 8)&0xff)
		, (uint8_t)((reg >> 0)&0xff)
	};
	return writeRegs(FSKOOK_BITRATE_MSB,sizeof(v),v);
}


int SX1276::getBitRate()
{
	if (isLora())
		return -1;
	uint8_t v[2], f;
	if (readRegs(FSKOOK_BITRATE_MSB,sizeof(v),v))
		return -1;
	if (readReg(FSKOOK_BITRATE_FRAC,&f))
		return -1;
	float br = (float)((v[0] << 8) | v[1]) + ((float)f/16);
	float r = rintf(32.0E6 / br);
	log_dbug(TAG,"bit-rate %dB/s",(int)r);
	return (int)r;
}


int SX1276::getBandwidth()
{
	if (!isLora())
		return -1;
	uint8_t r;
	if (readReg(LORA_MODEM_CONFIG_1,&r))
		return -1;
	log_dbug(TAG,"modem config #1: %splicit header, bandwidth %u.%02ukHz, 4/%u error coding rate"
		, r&1 ? "im" : "ex"
		, BW_dkHz[r>>4] / 100
		, BW_dkHz[r>>4] % 100
		, ((r>>1)&7)+4
		);
	r >>= 4;
	return BW_dkHz[r] * 10;
}


int SX1276::setBandwidth(unsigned bw)
{
	if (!isLora()) {
		log_warn(TAG,"bandwidth only in LORA mode");
		return -1;
	}
	bw /= 10;
	for (int x = 0; x < sizeof(BW_dkHz)/sizeof(BW_dkHz[0]); ++x) {
		if (bw == BW_dkHz[x]) {
			uint8_t r;
			if (readReg(LORA_MODEM_CONFIG_1,&r))
				return -1;
			r &= 0xf;
			r |= x << 4;
			if (writeReg(LORA_MODEM_CONFIG_1,r))
				return -1;
			return 0;
		}
	}
	log_warn(TAG,"invalid bandwidth %u",bw*10);
	return -1;
}


int SX1276::getCodingRate()
{
	if (!isLora())
		return -1;
	uint8_t r;
	if (readReg(LORA_MODEM_CONFIG_1,&r))
		return -1;
	r >>= 1;
	r &= 7;
	r += 4;
	return r;
}


int SX1276::setCodingRate(uint8_t frac)
{
	if (!isLora()) {
		log_warn(TAG,"coding rate only in LORA mode");
		return -1;
	}
	if ((frac < 5) || (frac > 8)) {
		log_warn(TAG,"invalid coding rate 4/%u",frac);
		return -1;
	}
	frac -= 4;
	uint8_t r;
	if (readReg(LORA_MODEM_CONFIG_1,&r))
		return -1;
	r &= 0xf1;
	r |= frac << 1;
	if (writeReg(LORA_MODEM_CONFIG_1,r))
		return -1;
	return 0;
}


int SX1276::setHeaderMode(bool implicit)
{
	if (!isLora()) {
		log_warn(TAG,"header mode only in LORA mode");
		return -1;
	}
	uint8_t r;
	if (readReg(LORA_MODEM_CONFIG_1,&r))
		return -1;
	r &= 0xfe;
	r |= implicit;
	if (writeReg(LORA_MODEM_CONFIG_1,r))
		return -1;
	return 0;
}


int SX1276::setCRC(bool crc)
{
	if (!isLora()) {
		log_warn(TAG,"CRC only in LORA mode");
		return -1;
	}
	uint8_t r;
	if (readReg(LORA_HOP_CHANNEL,&r))
		return -1;
	if (crc)
		r |= BIT_CRC_ON_PAYLOAD;
	else
		r &= ~BIT_CRC_ON_PAYLOAD;
	return writeReg(LORA_HOP_CHANNEL,r);
}


int SX1276::getSpreadingFactor()
{
	uint8_t r;
	readReg(LORA_MODEM_CONFIG_2,&r);
	return 1 << ((r & MASK_SPREADING_FACTOR) >> LSB_SPREADING_FACTOR);
}


int SX1276::setSpreadingFactor(unsigned sf)
{
	if (sf < 6)
		return -1;
	if (sf > 12) {
		for (unsigned x = 6; x <= 12; ++x) {
			if ((1<<x) == sf) {
				sf = x;
				break;
			}
		}
		if (sf > 12)
			return -1;
	}
	uint8_t r;
	readReg(LORA_MODEM_CONFIG_2,&r);
	r &= ~MASK_SPREADING_FACTOR;
	r |= sf << LSB_SPREADING_FACTOR;
	return writeReg(LORA_MODEM_CONFIG_2,r);
}


IRAM_ATTR void SX1276::postCallback(spi_transaction_t *t)
{
	SX1276 *dev = (SX1276 *) t->user;
	xSemaphoreGive(dev->m_sem);
}


void SX1276::readRegsSync(uint8_t reg, uint8_t num)
{
	uint8_t data[num];
	spi_transaction_t t;
	bzero(&t,sizeof(t));
	t.user = this;
	t.addr = reg;
	t.length = num<<3;
	t.rxlength = num<<3;
	t.rx_buffer = data;
	if (esp_err_t e = spi_device_queue_trans(m_hdl,&t,1)) {
		log_warn(TAG,"error queuing read: %s",esp_err_to_name(e));
		return;
	}
	if (pdTRUE != xSemaphoreTake(m_sem,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(m_sem,"sx1276");
	log_hex(TAG,data,sizeof(data),"read regs %u@%u:",num,reg);
}


int SX1276::readRegs(uint8_t reg, uint8_t num, uint8_t *data)
{
	spi_transaction_t t;
	bzero(&t,sizeof(t));
	t.user = this;
	t.addr = reg;
	t.length = num<<3;
	t.rxlength = num<<3;
	t.rx_buffer = data;
	if (esp_err_t e = spi_device_queue_trans(m_hdl,&t,1)) {
		log_warn(TAG,"error queuing read: %s",esp_err_to_name(e));
		return -1;
	}
	if (pdTRUE != xSemaphoreTake(m_sem,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(m_sem,"sx1276");
	log_hex(TAG,data,num,"read regs %u@%u:",num,reg);
	return 0;
}


int SX1276::readReg(uint8_t reg, uint8_t *data)
{
	spi_transaction_t t;
	bzero(&t,sizeof(t));
	t.user = this;
	t.addr = reg;
	t.length = 1<<3;
	t.rxlength = 1<<3;
	t.rx_buffer = data;
	if (esp_err_t e = spi_device_queue_trans(m_hdl,&t,1)) {
		log_warn(TAG,"error queuing read: %s",esp_err_to_name(e));
		return -1;
	}
	if (pdTRUE != xSemaphoreTake(m_sem,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(m_sem,"sx1276");
	log_dbug(TAG,"read reg 0x%x: 0x%x",reg,*data);
	return 0;
}


int SX1276::writeReg(uint8_t r, uint8_t v)
{
	spi_transaction_t t;
	bzero(&t,sizeof(t));
	t.cmd = 1;
	t.user = this;
	t.addr = r;
	t.length = 1<<3;
	t.rxlength = 0;
	t.rx_buffer = 0;
	t.tx_buffer = &v;
//	if (esp_err_t e = spi_device_transmit(m_hdl,&t))
//		log_warn(TAG,"error writing reg 0x%x: %s",r,esp_err_to_name(e));
	if (esp_err_t e = spi_device_queue_trans(m_hdl,&t,1)) {
		log_warn(TAG,"error queuing read: %s",esp_err_to_name(e));
		return -1;
	}
	if (pdTRUE != xSemaphoreTake(m_sem,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(m_sem,"sx1276");
	log_dbug(TAG,"write 0x%02x, 0x%02x",r,v);
	return 0;
}


int SX1276::writeRegs(uint8_t r, uint8_t n, uint8_t *v)
{
	spi_transaction_t t;
	bzero(&t,sizeof(t));
	t.cmd = 1;
	t.user = this;
	t.addr = r;
	t.length = n<<3;
	t.rxlength = 0;
	t.rx_buffer = 0;
	t.tx_buffer = v;
//	if (esp_err_t e = spi_device_transmit(m_hdl,&t))
//		log_warn(TAG,"error writing reg 0x%x: %s",r,esp_err_to_name(e));
	if (esp_err_t e = spi_device_queue_trans(m_hdl,&t,1)) {
		log_warn(TAG,"error queuing read: %s",esp_err_to_name(e));
		return -1;
	}
	if (pdTRUE != xSemaphoreTake(m_sem,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(m_sem,"sx1276");
	return 0;
}


int SX1276::send(const char *buf, int s)
{
	uint8_t opm;
	if ((buf == 0) || (s <= 0) || (s > MAX_PACKET_SIZE)) {
		log_warn(TAG,"send: invalid arg");
		return -1;
	}
	if (readReg(REG_OP_MODE,&opm))
		return -1;
	if (((opm & MASK_MODE) != mode_stdb) && ((opm & MASK_MODE) != mode_sleep)) {
		log_warn(TAG,"cannot send from mode %s",ModeStrs[opm&MASK_MODE]);
		return -1;
	}
	opm &= ~MASK_MODE;
	if (isLora()) {
		opm |= mode_stdb;
		writeReg(REG_OP_MODE,opm);
		writeReg(LORA_FIFO_ADDR_PTR, 0);
		writeReg(LORA_FIFO_TX_BASE_ADDR, 0);
		uint8_t n = s;
		do {
			writeReg(REG_FIFO,*buf);
			++buf;
		} while (--s);
		writeReg(LORA_PAYLOAD_LENGTH, n);
		opm &= ~MASK_MODE;
		opm |= mode_tx;
		writeReg(REG_OP_MODE,opm);
		log_dbug(TAG,"send started");
		uint8_t irqf;
		do {
			ets_delay_us(10000);
			readReg(REG_OP_MODE,&opm);
			readReg(LORA_IRQ_FLAGS,&irqf);
		} while ((irqf & BIT_TX_DONE) == 0);
		log_dbug(TAG,"send done");
		writeReg(LORA_IRQ_FLAGS,BIT_TX_DONE);
		opm &= ~MASK_MODE;
		opm |= mode_stdb;
		writeReg(REG_OP_MODE,opm);
	} else {
//		writeReg(FSKOOK_PAYLOAD_LENGTH,s);
		// first write length to fifo, for variable packet length
		writeReg(REG_FIFO,s);
		do {
			writeReg(REG_FIFO,*buf);
			++buf;
		} while (--s);
		opm |= mode_tx;
		writeReg(REG_OP_MODE,opm);
		log_dbug(TAG,"send started");
		uint8_t irqf;
		do {
			ets_delay_us(10000);
			readReg(REG_OP_MODE,&opm);
			readReg(LORA_IRQ_FLAGS,&irqf);
		} while ((irqf & BIT_TX_DONE) == 0);
		log_dbug(TAG,"send done");
		writeReg(LORA_IRQ_FLAGS,BIT_TX_DONE);
		opm &= ~MASK_MODE;
		opm |= mode_stdb;
		writeReg(REG_OP_MODE,opm);
	}
	return 0;
}


const char *SX1276::exeCmd(Terminal &t, int argc, const char **args)
{
	if ((argc == 0) || ((argc == 1) && (0 == strcmp(args[0],"-h")))) {
		t.println(
			"info       : print current settings\n"
			"addr <a>   : 8bit node address (only FSK/OOK)\n"
			"bca <a>    : 8bit broadcast address (only FSK/OOK)\n"
			"modem <m>  : switch modem to FSK, OOK, LORA\n"
			"freq <f>   : set frequency in MHz\n"
			"bps <b>    : set bit-rate in B/s (only FSK/OOK)\n"
			"bw <b>     : set bandwidth (LORA only)\n"
			"crc <b>    : enable/disable CRC (LORA only)\n"
			"sf <r>     : set spreading factor in chips/symbol (LORA only)\n"
			"cr <r>     : set coding rate 4/5..4/8 (LORA only)\n"
			"imax <i>   : set maximum current of OCP\n"
			"flags      : read irq flags\n"
			"reg <a>    : get register <a>\n"
			"reg <a> <v>: set register <a> to value <v>\n"
			);
		return 0;
	}
	if (argc == 1) {
		if (0 == strcmp(args[0],"freq")) {
			t.printf("%u MHz\n",getFreq());
		} else if (0 == strcmp(args[0],"mode")) {
			uint8_t opm;
			readReg(REG_OP_MODE,&opm);
			t.println(ModeStrs[opm&MASK_MODE]);
		} else if (0 == strcmp(args[0],"addr")) {
			uint8_t addr;
			readReg(FSKOOK_NODE_ADRS,&addr);
			t.printf("%02x\n",addr);
		} else if (0 == strcmp(args[0],"bca")) {
			uint8_t addr;
			readReg(FSKOOK_BROADCAST_ADRS,&addr);
			t.printf("%02x\n",addr);
		} else if (0 == strcmp(args[0],"flags")) {
			uint8_t flags[2];
			readRegs(FSKOOK_IRQ_FLAGS1,sizeof(flags),flags);
			t.printf("%02x %02x\n",flags[0],flags[1]);
			if (flags[0] & 0x80)
				t.println("mode ready");
			if (flags[1] & 0x80)
				t.println("fifo full");
			if (flags[1] & 0x40)
				t.println("fifo empty");
			if (flags[1] & 0x10)
				t.println("fifo overrun");
			if (flags[1] & 0x8)
				t.println("packet sent");
			if (flags[1] & 0x4)
				t.println("payload ready");
			if (flags[1] & 0x2)
				t.println("CRC ok");
		} else if (0 == strcmp(args[0],"modem")) {
			t.println(isLora() ? "LORA" : getOOK() ? "OOK" : "FSK");
		} else if (0 == strcmp(args[0],"info")) {
			uint8_t opm;
			readReg(REG_OP_MODE,&opm);
			t.printf("modem: %s at %u MHz\n",(opm&BIT_LORA) ? "LORA" : (opm&BIT_OOK)?"OOK":"FSK", getFreq());
			t.printf("mode : %s\n",ModeStrs[opm&MASK_MODE]);
			t.printf("gain: %u\n",getGain());
			if (opm&BIT_LORA) {
				uint8_t c[2];
				readRegs(LORA_MODEM_CONFIG_1,sizeof(c),c);
				t.printf("%splicit header, bandwidth %u.%02ukHz, 4/%u error coding rate, %u chips/symbol\n"
					, c[0]&1 ? "im" : "ex"
					, BW_dkHz[c[0]>>4] / 100
					, BW_dkHz[c[0]>>4] % 100
					, ((c[0]>>1)&7)+4
					, 1 << (c[1] >> LSB_SPREADING_FACTOR)
					);
			} else {
				t.printf("bit-rate: %u B/s\n",getBitRate());
			}
		} else if (0 == strcmp(args[0],"cad")) {
			writeReg(LORA_IRQ_FLAGS,0xff);
			setMode(mode_stdb);
			setMode(mode_cad);
		} else if (0 == strcmp(args[0],"regs")) {
			uint8_t r[32];
			readRegs(1,sizeof(r),r);
			log_hex(TAG,r,sizeof(r),"regs:");
		} else {
			return "Invalid argument #1.";
		}
		return 0;
	} else if (argc == 2) {
		if (0 == strcmp(args[0],"mode")) {
			if (0 == strcmp(args[1],"rx1"))
				setMode(mode_rxsingle);
			else if (0 == strcmp(args[1],"rx"))
				setMode(mode_rxcont);
			else if (0 == strcmp(args[1],"recv"))
				setMode(mode_rxcont);
			else if (0 == strcmp(args[1],"standby"))
				setMode(mode_stdb);
		} else if (0 == strcmp(args[0],"freq")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if (*e || (l < 137) || setFreq(l))
				return "Invalid argument #2.";
		} else if (0 == strcmp(args[0],"addr")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if (*e || (l < 0) || (l > 255))
				return "Invalid argument #2.";
			writeReg(FSKOOK_NODE_ADRS,l);
		} else if (0 == strcmp(args[0],"bca")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if (*e || (l < 0) || (l > 255))
				return "Invalid argument #2.";
			writeReg(FSKOOK_BROADCAST_ADRS,l);
		} else if (0 == strcmp(args[0],"reg")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if (*e || (l < 0) || (l > 255))
				return "Invalid argument #2.";
			uint8_t r;
			if (0 == readReg(l,&r))
				t.printf("0x%x: 0x%x\n",l,r);
			else
				t.println("error reading register");
		} else if (0 == strcmp(args[0],"bw")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if (*e || setBandwidth(l))
				return "Invalid argument #2.";
		} else if (0 == strcmp(args[0],"bps")) {
			if (isLora())
				return "Bit-rate can only be set in LORA mode.";
			char *e;
			long l = strtol(args[1],&e,0);
			if (*e || setBitRate(l))
				return "Invalid argument #2.";
		} else if (0 == strcmp(args[0],"gain")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if (*e || (l < 1) || (l > 6))
				return "Invalid argument #2.";
			if (setGain(l))
				return "Failed.";
		} else if (0 == strcmp(args[0],"crc")) {
			if (0 == strcasecmp(args[1],"on"))
				setCRC(true);
			else if (0 == strcasecmp(args[1],"off"))
				setCRC(false);
			if (0 == strcasecmp(args[1],"enable"))
				setCRC(true);
			else if (0 == strcasecmp(args[1],"disable"))
				setCRC(false);
			else
				return "Invalid argument #2.";
		} else if (0 == strcmp(args[0],"sf")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if (*e || setSpreadingFactor(l))
				return "Invalid argument #2.";
		} else if (0 == strcmp(args[0],"cr")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if (*e || setCodingRate(l))
				return "Invalid argument #2.";
		} else if (0 == strcmp(args[0],"modem")) {
			if (0 == strcasecmp(args[1],"lora")) {
				setLora(true);
			} else if (0 == strcasecmp(args[1],"ook")) {
				setLora(false);
				setOOK(true);
			} else if (0 == strcasecmp(args[1],"fsk")) {
				setLora(false);
				setOOK(false);
			} else {
				return "Invalid argument #2.";
			}
		} else {
			return "Invalid argument #1.";
		}
		return 0;
	} else if (argc == 3) {

		} else if (0 == strcmp(args[0],"reg")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if (*e || (l < 0) || (l > 255))
				return "Invalid argument #2.";
			long v = strtol(args[2],&e,0);
			if (*e || (v < 0) || (v > 255))
				return "Invalid argument #3.";
			if (writeReg(l,v))
				return "Error writing register.";
	} else {
		return "Invalid number of arguments.";
	}
	return 0;

}


#endif // CONFIG_SX1276
