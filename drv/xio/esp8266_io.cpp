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

#include "coreio.h"
#include "log.h"

#define TAG MODULE_GPIO

#if defined CONFIG_IDF_TARGET_ESP8266 && defined CONFIG_IOEXTENDERS

#include <esp8266/gpio_struct.h>
#include <esp8266/pin_mux_register.h>
#include <errno.h>
#include <driver/gpio.h>
#include <rom/gpio.h>

#define PERIPHS_IO_MU		0x60000800
#define PERIPHS_RTC_BASEADDR	0x60000700
#define RTC_GPIO_REG (PERIPHS_RTC_BASEADDR + 0xa0)


struct CoreIO : public XioCluster
{
	CoreIO();
	int get_lvl(uint8_t io) override;
	int setm(uint32_t,uint32_t) override;
	int set_hi(uint8_t io) override;
	int set_lo(uint8_t io) override;
	int set_intr(uint8_t,xio_intrhdlr_t,void*) override;
	int config(uint8_t io, xio_cfg_t) override;
	int set_lvl(uint8_t io, xio_lvl_t v) override;
//	xio_caps_t getFlags(uint8_t) const override;
	const char *getName() const override;
	int get_dir(uint8_t num) const override;
	unsigned numIOs() const override;
};


struct RtcIO : public XioCluster
{
	RtcIO();
	int get_lvl(uint8_t io) override;
	int set_hi(uint8_t io) override;
	int set_lo(uint8_t io) override;
	int config(uint8_t io, xio_cfg_t) override;
	int set_lvl(uint8_t io, xio_lvl_t v) override;
//	xio_caps_t getFlags(uint8_t) const override;
	const char *getName() const override;
	int get_dir(uint8_t num) const override;
	unsigned numIOs() const override;
};


CoreIO GpioCluster0;
RtcIO GpioCluster1;


static const uint8_t IOREG_OFF[] = {
	0x34,		// PERIPHS_IO_MUX_GPIO0_U
	0x18,		// PERIPHS_IO_MUX_U0TXD_U
	0x38,		// PERIPHS_IO_MUX_GPIO2_U
	0x14,		// PERIPHS_IO_MUX_U0RXD_U
	0x3c,		// PERIPHS_IO_MUX_GPIO4_U
	0x40,		// PERIPHS_IO_MUX_GPIO5_U
	0x1c,		// PERIPHS_IO_MUX_SD_CLK_U
	0x20,		// PERIPHS_IO_MUX_SD_DATA0_U
	0x24,		// PERIPHS_IO_MUX_SD_DATA1_U
	0x28,		// PERIPHS_IO_MUX_SD_DATA2_U
	0x2c,		// PERIPHS_IO_MUX_SD_DATA3_U
	0x30,		// PERIPHS_IO_MUX_SD_CMD_U
	0x04,		// PERIPHS_IO_MUX_MTDI_U
	0x08,		// PERIPHS_IO_MUX_MTCK_U
	0x0c,		// PERIPHS_IO_MUX_MTMS_U
	0x10,		// PERIPHS_IO_MUX_MTDO_U
};


static inline void pullup(uint8_t num, bool en)
{
	gpio_pin_reg_t pin_reg;
	uint32_t addr = PERIPHS_IO_MUX+IOREG_OFF[num];
	pin_reg.val = READ_PERI_REG(addr);
	pin_reg.pullup = en;
	WRITE_PERI_REG(addr, pin_reg.val);
	log_dbug(TAG,"pull-up %sabled on %u",en?"en":"dis",num);
}


int CoreIO::config(uint8_t num, xio_cfg_t cfg)
{
	uint16_t x = (1<<num);
	if (x == 0) {
		log_warn(TAG,"invalid gpio%u",num);
		return -EINVAL;
	}

	if (cfg.cfg_io == xio_cfg_io_in) {
		log_dbug(TAG,"%u input",num);
		gpio_pad_select_gpio(num);
		GPIO.enable_w1tc = x;
		GPIO.pin[num].driver = 0;	// 1=open-drain
	} else if (cfg.cfg_io == xio_cfg_io_out) {
		log_dbug(TAG,"%u output",num);
		gpio_pad_select_gpio(num);
		GPIO.enable_w1ts = x;
		GPIO.pin[num].driver = 0;	// 1=open-drain
	} else if (cfg.cfg_io == xio_cfg_io_od) {
		log_dbug(TAG,"%u open-drain",num);
		gpio_pad_select_gpio(num);
		GPIO.enable_w1ts = x;
		GPIO.pin[num].driver = 1;	// 1=open-drain
	} else if (cfg.cfg_io != xio_cfg_io_keep) {
		return -EINVAL;
	}

	if (cfg.cfg_pull == xio_cfg_pull_none) {
		pullup(num,0);
	} else if (cfg.cfg_pull == xio_cfg_pull_up) {
		pullup(num,1);
	} else if (cfg.cfg_pull == xio_cfg_pull_down) {
		log_warn(TAG,"no pull-down on gpio%u",num);
		return -ENOTSUP;
	} else if (cfg.cfg_pull == xio_cfg_pull_updown) {
		log_warn(TAG,"no pull-up+down on gpio%u",num);
		return -ENOTSUP;
	} else if (cfg.cfg_pull != xio_cfg_pull_keep) {
		return -EINVAL;
	}

	if (cfg.cfg_intr < xio_cfg_intr_keep) {
		GPIO.pin[num].int_type = cfg.cfg_intr;
	} else if (cfg.cfg_intr != xio_cfg_intr_keep) {
		return -EINVAL;
	}

	if (cfg.cfg_wakeup == xio_cfg_wakeup_disable) {
		GPIO.pin[num].wakeup_enable = 0;
	} else if (GPIO.pin[num].int_type == 4) {
		GPIO.pin[num].wakeup_enable = 1;
	} else if (GPIO.pin[num].int_type == 5) {
		GPIO.pin[num].wakeup_enable = 1;
	} else if (cfg.cfg_wakeup != xio_cfg_wakeup_keep) {
		return -EINVAL;
	}

	return 0x1ff ^ xio_cap_pulldown;
}


const char *CoreIO::getName() const
{
	return "coreio";
}


const char *RtcIO::getName() const
{
	return "rtcio";
}


unsigned CoreIO::numIOs() const
{
	return 16;
}


unsigned RtcIO::numIOs() const
{
	return 1;
}


int CoreIO::get_dir(uint8_t num) const
{
	if (num >= 16)
		return -1;
	uint32_t dir = GPIO_REG_READ(GPIO_ENABLE_ADDRESS);
	if (dir & (1<<num))
		return xio_cfg_io_out;
	return xio_cfg_io_in;
}


int CoreIO::get_lvl(uint8_t num)
{
	if (num < 16)
		return (GPIO.in >> num) & 1;
	return -EINVAL;
}


int CoreIO::set_hi(uint8_t num)
{
	if (uint16_t b = 1 << num) {
		GPIO.out_w1ts = b;
		return 0;
	}
	return -EINVAL;
}


int CoreIO::set_lo(uint8_t num)
{
	if (uint16_t b = 1 << num) {
		GPIO.out_w1tc = b;
		return 0;
	}
	return -EINVAL;
}


int CoreIO::set_lvl(uint8_t num, xio_lvl_t l)
{
	if (uint16_t b = 1 << num) {
		if (l == xio_lvl_0) {
			GPIO.out_w1tc = b;
			GPIO.enable_w1ts = b;
			return 0;
		} else if (l == xio_lvl_1) {
			GPIO.out_w1ts = b;
			GPIO.enable_w1ts = b;
			return 0;
		} else if (l == xio_lvl_hiz) {
			GPIO.enable_w1tc = b;
			return 0;
		}
	}
	return -EINVAL;
}


int CoreIO::setm(uint32_t values, uint32_t mask)
{
	if (mask >> 16)
		return -EINVAL;
	GPIO.out_w1ts = values & mask;
	GPIO.out_w1tc = values ^ mask;
	return 0;
}


int CoreIO::set_intr(uint8_t gpio, xio_intrhdlr_t hdlr, void *arg)
{
	if (gpio >= 16) {
		log_warn(TAG,"set intr esp8266:gpio%u: invaid gpio",gpio);
		return -EINVAL;
	}
	if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)gpio,hdlr,(void*)arg)) {
		log_warn(TAG,"add isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
		return e;
	}
	log_dbug(TAG,"attached isr to gpio%u",gpio);
	return 0;
}


void rtc_set_pulldown(bool en)
{
	gpio_pin_reg_t pin_reg;
	pin_reg.val = READ_PERI_REG(RTC_GPIO_REG);
	pin_reg.rtc_pin.pulldown = en;
	WRITE_PERI_REG(RTC_GPIO_REG, pin_reg.val);
}


int RtcIO::config(uint8_t num, xio_cfg_t cfg)
{
	if (num) {
		log_warn(TAG,"invalid rtc-gpio%u",num);
		return -EINVAL;
	}
	if (cfg.cfg_io) {
		if (cfg.cfg_io == xio_cfg_io_in) {
			WRITE_PERI_REG(PAD_XPD_DCDC_CONF, ((READ_PERI_REG(PAD_XPD_DCDC_CONF) & (uint32_t)0xffffffbc)) | (uint32_t)0x1);         // mux configuration for XPD_DCDC and rtc_gpio0 connection
			CLEAR_PERI_REG_MASK(RTC_GPIO_CONF, 0x1);    //mux configuration for out enable
			CLEAR_PERI_REG_MASK(RTC_GPIO_ENABLE, 0x1);   //out disable
		} else if (cfg.cfg_io == xio_cfg_io_out) {
			WRITE_PERI_REG(PAD_XPD_DCDC_CONF, ((READ_PERI_REG(PAD_XPD_DCDC_CONF) & (uint32_t)0xffffffbc)) | (uint32_t)0x1); // mux configuration for XPD_DCDC and rtc_gpio0 connection
			CLEAR_PERI_REG_MASK(RTC_GPIO_CONF, 0x1);                                                                        //mux configuration for out enable
			SET_PERI_REG_MASK(RTC_GPIO_ENABLE, 0x1);                                                                        //out enable
		} else {
			return -ENOTSUP;
		}
	}
	if (cfg.cfg_pull) {
		if (cfg.cfg_pull == xio_cfg_pull_none) {
			rtc_set_pulldown(false);
		} else if (cfg.cfg_pull == xio_cfg_pull_up) {
			log_warn(TAG,"no pull-up on gpio16",num);
			return -ENOTSUP;
		} else if (cfg.cfg_pull == xio_cfg_pull_down) {
			rtc_set_pulldown(true);
		} else if (cfg.cfg_pull == xio_cfg_pull_updown) {
			log_warn(TAG,"no pull-up+down on gpio16");
			return -ENOTSUP;
		}
	}
	if (cfg.cfg_intr) {
		return -ENOTSUP;
	}
	if (cfg.cfg_wakeup) {
		return -ENOTSUP;
	}
	return xio_cap_pulldown;
}


int RtcIO::get_dir(uint8_t num) const
{
	if (num)
		return -1;
	return (READ_PERI_REG(RTC_GPIO_ENABLE) & 1) ? xio_cfg_io_out : xio_cfg_io_in;
}


int RtcIO::get_lvl(uint8_t num)
{
	if (num == 0)
		return READ_PERI_REG(RTC_GPIO_IN_DATA) & 0x1;
	return -1;
}


int RtcIO::set_hi(uint8_t num)
{
	if (num == 0) {
		SET_PERI_REG_MASK(RTC_GPIO_OUT,1);
		return 0;
	}
	return -1;
}


int RtcIO::set_lo(uint8_t num)
{
	if (num == 0) {
		CLEAR_PERI_REG_MASK(RTC_GPIO_OUT,1);
		return 0;
	}
	return -1;
}


int RtcIO::set_lvl(uint8_t num, xio_lvl_t l)
{
	int r = 0;
	if (num == 0) {
		if (l == xio_lvl_0) {
			CLEAR_PERI_REG_MASK(RTC_GPIO_OUT,1);
		} else if (l == xio_lvl_1) {
			SET_PERI_REG_MASK(RTC_GPIO_OUT,1);
		} else {
			r = -1;
		}
	} else {
		r = -1;
	}
	return r;
}


int rtc_set_intr(uint8_t gpio, xio_intrhdlr_t hdlr, void *arg)
{
	return -ENOTSUP;
}


#if 0
void IRAM_ATTR _xt_isr_handler(void)
{
	uint32_t mask;
	s_xt_isr_status = 1;
	while ((mask = soc_get_int_mask()) != 0) {
		for (unsigned bit = 0; bit < ETS_INT_MAX && mask; ++i) {
			if ((mask&1) && s_isr[i].handler) {
				soc_clear_int_mask(bit);
				s_isr[i].handler(s_isr[i].arg);
			}
			mask >>= 1;
		}
	}
	s_xt_isr_status = 0;
}
#endif

CoreIO::CoreIO()
{
}


RtcIO::RtcIO()
{
}


int coreio_config(uint8_t num, xio_cfg_t cfg)
{
	if ((num >> 4) == 0)
		return GpioCluster0.config(num,cfg);
	if (num == 16)
		return GpioCluster1.config(0,cfg);
	return -1;
}


int coreio_lvl_get(uint8_t num)
{
	if ((num >> 4) == 0)
		return GpioCluster0.get_lvl(num);
	if (num == 16)
		return GpioCluster1.get_lvl(0);
	return -1;
}


int coreio_lvl_hi(uint8_t num)
{
	if ((num >> 4) == 0)
		return GpioCluster0.set_hi(num);
	if (num == 16)
		return GpioCluster1.set_hi(0);
	return -1;
}


int coreio_lvl_lo(uint8_t num)
{
	if ((num >> 4) == 0)
		return GpioCluster0.set_lo(num);
	if (num == 16)
		return GpioCluster1.set_lo(0);
	return -1;
}


int coreio_lvl_set(uint8_t num, xio_lvl_t l)
{
	if ((num >> 4) == 0)
		return GpioCluster0.set_lvl(num,l);
	if (num == 16)
		return GpioCluster1.set_lvl(0,l);
	return -1;
}


void coreio_register()
{
	gpio_install_isr_service(0);
	GpioCluster0.attach(0);
	GpioCluster1.attach(16);
}

#endif
