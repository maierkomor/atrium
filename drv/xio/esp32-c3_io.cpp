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

#if (defined CONFIG_IDF_TARGET_ESP32C3 || defined CONFIG_IDF_TARGET_ESP32C6) && defined CONFIG_IOEXTENDERS

#include "coreio.h"
#include "log.h"

#include "soc/gpio_periph.h"
#include <errno.h>
#include <driver/gpio.h>
#include <rom/gpio.h>

#define COREIO0_NUMIO 22

#define TAG MODULE_GPIO

struct CoreIO : public XioCluster
{
	int get_dir(uint8_t num) const override;
	int get_lvl(uint8_t io) override;
	int get_out(uint8_t io) override;
	int setm(uint32_t,uint32_t) override;
	int set_hi(uint8_t io) override;
	int set_hiz(uint8_t io) override;
	int set_lo(uint8_t io) override;
	int set_intr(uint8_t,xio_intrhdlr_t,void*) override;
//	int intr_enable(uint8_t) override;
//	int intr_disable(uint8_t) override;
	int config(uint8_t io, xio_cfg_t) override;
	int set_lvl(uint8_t io, xio_lvl_t v) override;
	int hold(uint8_t io) override;
	int unhold(uint8_t io) override;
	const char *getName() const override;
	unsigned numIOs() const override;
};


static CoreIO GpioCluster;


const char *CoreIO::getName() const
{
	return "internal";
}


unsigned CoreIO::numIOs() const
{
	return COREIO0_NUMIO;
}


int CoreIO::config(uint8_t num, xio_cfg_t cfg)
{
	log_dbug(TAG,"config %u,0x%x",num,cfg);
	if (num >= COREIO0_NUMIO) {
		log_warn(TAG,"invalid gpio%u",num);
		return -EINVAL;
	}
#if 0
	gpio_config_t ioc;
	ioc.pin_bit_mask = 1ULL << num;
	if (cfg.cfg_io == xio_cfg_io_keep) {
	} else if (cfg.cfg_io == xio_cfg_io_in) {
		ioc.mode = GPIO_MODE_INPUT;
	} else if (cfg.cfg_io == xio_cfg_io_out) {
		ioc.mode = GPIO_MODE_OUTPUT;
	} else if (cfg.cfg_io == xio_cfg_io_od) {
		abort();
	} else {
		return -EINVAL;
	}
	if (cfg.cfg_pull == xio_cfg_pull_keep) {
	} else if (cfg.cfg_pull == xio_cfg_pull_none) {
		ioc.pull_up_en = GPIO_PULLUP_DISABLE;
		ioc.pull_down_en = GPIO_PULLDOWN_ENABLE;
	} else if (cfg.cfg_pull == xio_cfg_pull_up) {
		ioc.pull_up_en = GPIO_PULLUP_ENABLE;
		ioc.pull_down_en = GPIO_PULLDOWN_DISABLE;
	} else if (cfg.cfg_pull == xio_cfg_pull_down) {
		ioc.pull_up_en = GPIO_PULLUP_DISABLE;
		ioc.pull_down_en = GPIO_PULLDOWN_ENABLE;
	} else if (cfg.cfg_pull == xio_cfg_pull_updown) {
		ioc.pull_up_en = GPIO_PULLUP_ENABLE;
		ioc.pull_down_en = GPIO_PULLDOWN_ENABLE;
	} else {
		return -EINVAL;
	}
	if (cfg.cfg_intr == xio_cfg_intr_keep) {
	} else if (cfg.cfg_intr < xio_cfg_intr_keep) {
		ioc.intr_type = (gpio_int_type_t)cfg.cfg_intr;
		if (esp_err_t e = gpio_set_intr_type((gpio_num_t)num,(gpio_int_type_t)cfg.cfg_intr))
			log_error(TAG,"set intr type %d",e);
	} else {
		return -EINVAL;
	}
	if (esp_err_t e = gpio_config(&ioc)) {
		log_error(TAG,"config %u: %s",num,esp_err_to_name(e));
		return -1;
	}
#endif
#if 1
	if (cfg.cfg_io == xio_cfg_io_keep) {
		log_dbug(TAG,"keep io %u",num);
	} else if (cfg.cfg_io == xio_cfg_io_in) {
//		PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[num]);
//		GPIO.enable_w1tc = (1 << num);
//		GPIO.pin[num].pad_driver = 0;
		gpio_pad_select_gpio(num);
		gpio_set_direction((gpio_num_t)num,GPIO_MODE_INPUT);
		log_dbug(TAG,"input %u",num);
	} else if (cfg.cfg_io == xio_cfg_io_out) {
		gpio_pad_select_gpio(num);
		PIN_INPUT_DISABLE(GPIO_PIN_MUX_REG[num]);
		GPIO.enable_w1ts.val = (1 << num);
		gpio_matrix_out(num, SIG_GPIO_OUT_IDX, false, false);
		GPIO.pin[num].pad_driver = 0;
		log_dbug(TAG,"output %u",num);
	} else if (cfg.cfg_io == xio_cfg_io_od) {
		gpio_pad_select_gpio(num);
		PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[num]);
		GPIO.enable_w1ts.val = (1 << num);
		gpio_matrix_out(num, SIG_GPIO_OUT_IDX, false, false);
		GPIO.pin[num].pad_driver = 1;
		log_dbug(TAG,"open-drain %u",num);
	} else if (cfg.cfg_io != xio_cfg_io_keep) {
		return -EINVAL;
	}

	if (cfg.cfg_pull == xio_cfg_pull_keep) {
	} else if (cfg.cfg_pull == xio_cfg_pull_none) {
		REG_CLR_BIT(GPIO_PIN_MUX_REG[num], FUN_PU);
		REG_CLR_BIT(GPIO_PIN_MUX_REG[num], FUN_PD);
		log_dbug(TAG,"no pull %u",num);
	} else if (cfg.cfg_pull == xio_cfg_pull_up) {
		gpio_pulldown_dis((gpio_num_t)num);
		if (esp_err_t e = gpio_pullup_en((gpio_num_t)num))
			log_warn(TAG,"pull-up on %d failed: %d",num,e);
		else
			log_dbug(TAG,"pull-up %u",num);
//		REG_SET_BIT(GPIO_PIN_MUX_REG[num], FUN_PU);
//		REG_CLR_BIT(GPIO_PIN_MUX_REG[num], FUN_PD);
	} else if (cfg.cfg_pull == xio_cfg_pull_down) {
		REG_CLR_BIT(GPIO_PIN_MUX_REG[num], FUN_PU);
		REG_SET_BIT(GPIO_PIN_MUX_REG[num], FUN_PD);
		log_dbug(TAG,"pull-down %u",num);
	} else if (cfg.cfg_pull == xio_cfg_pull_updown) {
		REG_SET_BIT(GPIO_PIN_MUX_REG[num], FUN_PU);
		REG_SET_BIT(GPIO_PIN_MUX_REG[num], FUN_PD);
		log_dbug(TAG,"pull-up+down %u",num);
	} else {
		return -EINVAL;
	}
	
	if (cfg.cfg_intr == xio_cfg_intr_keep) {
	} else if (cfg.cfg_intr == xio_cfg_intr_disable) {
		GPIO.pin[num].int_type = cfg.cfg_intr;
		GPIO.pin[num].int_ena = 0;
		gpio_intr_disable((gpio_num_t)num);
		log_dbug(TAG,"interrupt disable %u",num);
	} else if (cfg.cfg_intr < xio_cfg_intr_keep) {
//		GPIO.pin[num].int_type = cfg.cfg_intr;
//		GPIO.pin[num].int_ena = 1;
		if (esp_err_t e = gpio_set_intr_type((gpio_num_t)num,(gpio_int_type_t)cfg.cfg_intr))
			log_error(TAG,"set intr type %d",e);
		else if (esp_err_t e = gpio_intr_enable((gpio_num_t)num))
			log_error(TAG,"intr enable %d",e);
		else
			log_dbug(TAG,"interrupt on %s on %u",GpioIntrTriggerStr[cfg.cfg_intr],num);
	} else {
		return -EINVAL;
	}

	if (cfg.cfg_wakeup == xio_cfg_wakeup_keep) {
	} else if (cfg.cfg_wakeup == xio_cfg_wakeup_disable) {
		GPIO.pin[num].wakeup_enable = 0;
	} else if (GPIO.pin[num].int_type == 4) {
		GPIO.pin[num].wakeup_enable = 1;
	} else if (GPIO.pin[num].int_type == 5) {
		GPIO.pin[num].wakeup_enable = 1;
	} else {
		return -EINVAL;
	}
#endif
	if (cfg.cfg_initlvl == xio_cfg_initlvl_keep) {
	} else if (cfg.cfg_initlvl == xio_cfg_initlvl_low) {
		set_lo(num);
	} else if (cfg.cfg_initlvl == xio_cfg_initlvl_high) {
		set_hi(num);
	} else {
		return -EINVAL;
	}
	return 0x1ff;
}


int CoreIO::get_dir(uint8_t num) const
{
	if (num >= COREIO0_NUMIO)
		return -1;
	if (GPIO.pin[num].pad_driver)
		return xio_cfg_io_od;
	if (GPIO.enable.val & (1 << num))
		return xio_cfg_io_out;
	return xio_cfg_io_in;
}


int CoreIO::get_lvl(uint8_t num)
{
	if (num < COREIO0_NUMIO) {
		if (GPIO.enable.val & (1 << num))
			return (GPIO.out.val >> num) & 0x1;
		else
			return (GPIO.in.val >> num) & 0x1;
	}
	return -EINVAL;
}


int CoreIO::get_out(uint8_t num)
{
	if (num < COREIO0_NUMIO) {
		if (((GPIO.enable.val >> num) & 1) == 0)
			return xio_lvl_hiz;
		return (GPIO.out.val >> num) & 1;
	}
	return -EINVAL;
}


int CoreIO::set_hi(uint8_t num)
{
	if (num < COREIO0_NUMIO) {
		uint32_t b = 1 << num;
		GPIO.out_w1ts.val = b;
		GPIO.enable_w1ts.val = b;
		return 0;
	}
	return -EINVAL;
}


int CoreIO::set_hiz(uint8_t num)
{
	if (num < COREIO0_NUMIO) {
		uint32_t b = 1 << num;
		GPIO.out_w1tc.val = b;
		GPIO.enable_w1tc.val = b;
		return 0;
	}
	return -EINVAL;
}


int CoreIO::set_lo(uint8_t num)
{
	if (num < COREIO0_NUMIO) {
		uint32_t b = 1 << num;
		GPIO.out_w1tc.val = b;
		GPIO.enable_w1ts.val = b;
		return 0;
	}
	return -EINVAL;
}


int CoreIO::set_lvl(uint8_t num, xio_lvl_t l)
{
	if (num < COREIO0_NUMIO) {
		uint32_t b = 1 << num;
		if (l == xio_lvl_0) {
			GPIO.out_w1tc.val = b;
			GPIO.enable_w1ts.val = b;
			return 0;
		} else if (l == xio_lvl_1) {
			GPIO.out_w1ts.val = b;
			GPIO.enable_w1ts.val = b;
			return 0;
		} else if (l == xio_lvl_hiz) {
			GPIO.out_w1tc.val = b;
			GPIO.enable_w1tc.val = b;
			return 0;
		}
	}
	return -EINVAL;
}


int CoreIO::hold(uint8_t num)
{
	if (num < COREIO0_NUMIO) {
		gpio_pad_hold(num);
		return 0;
	}
	return -EINVAL;
}


int CoreIO::unhold(uint8_t num)
{
	if (num < COREIO0_NUMIO) {
		gpio_pad_unhold(num);
		return 0;
	}
	return -EINVAL;
}


int CoreIO::setm(uint32_t v, uint32_t m)
{
	if ((v ^ m) & v)
		return -EINVAL;
	GPIO.out_w1ts.val = v & m;
	GPIO.out_w1tc.val = v ^ m;
	return 0;
}


int CoreIO::set_intr(uint8_t gpio, xio_intrhdlr_t hdlr, void *arg)
{
	if (gpio >= COREIO0_NUMIO) {
		log_warn(TAG,"set intr: invalid gpio%u",gpio);
		return -EINVAL;
	}
	if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)gpio,hdlr,arg)) {
		log_warn(TAG,"add isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
		return e;
	}
	log_dbug(TAG,"attached isr to gpio%u",gpio);
	return 0;
}


#if 0
int CoreIO::intr_enable(uint8_t gpio)
{
	if (gpio >= COREIO0_NUMIO) {
		log_warn(TAG,"enable intr: invalid gpio%u",gpio);
		return -EINVAL;
	}
	if (esp_err_t e = gpio_intr_enable((gpio_num_t)gpio)) {
		log_warn(TAG,"enable isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
		return e;
	}
	log_dbug(TAG,"enable intr on gpio%u",gpio);
	return 0;
}


int CoreIO::intr_disable(uint8_t gpio)
{
	if (gpio >= COREIO0_NUMIO) {
		log_warn(TAG,"disable intr: invalid gpio%u",gpio);
		return -EINVAL;
	}
	if (esp_err_t e = gpio_intr_disable((gpio_num_t)gpio)) {
		log_warn(TAG,"disable isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
		return e;
	}
	log_dbug(TAG,"disable intr on gpio%u",gpio);
	return 0;
}
#endif


int coreio_config(uint8_t num, xio_cfg_t cfg)
{
	if (num < COREIO0_NUMIO)
		return GpioCluster.config(num,cfg);
	return -1;
}


int coreio_lvl_get(uint8_t num)
{
	if (num < COREIO0_NUMIO)
		return GpioCluster.get_lvl(num);
	return -1;
}


int coreio_lvl_hi(uint8_t num)
{
	if (num < COREIO0_NUMIO)
		return GpioCluster.set_hi(num);
	return -1;
}


int coreio_lvl_lo(uint8_t num)
{
	if (num < COREIO0_NUMIO)
		return GpioCluster.set_lo(num);
	return -1;
}


int coreio_lvl_set(uint8_t num, xio_lvl_t l)
{
	if (num < COREIO0_NUMIO)
		return GpioCluster.set_lvl(num,l);
	return -1;
}


void coreio_register()
{
	if (esp_err_t e = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM))
		log_error(TAG,"isr service %d",e);
	GpioCluster.attach(0);
}

#endif
