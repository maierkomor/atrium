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

#if (defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3) && defined CONFIG_IOEXTENDERS

#include "coreio.h"
#include "log.h"

#include "soc/gpio_periph.h"
#include <errno.h>
#include <driver/gpio.h>
#include <rom/gpio.h>

#if IDF_VERSION >= 50
#define gpio_pad_select_gpio(...)
#define gpio_matrix_out(...)
#endif

#define COREIO0_NUMIO 32
#if defined CONFIG_IDF_TARGET_ESP32
	#define COREIO1_NUMIO 8
#elif defined CONFIG_IDF_TARGET_ESP32S2
	#define COREIO1_NUMIO 15
#elif defined CONFIG_IDF_TARGET_ESP32S3
	#define COREIO1_NUMIO 17
//#elif defined CONFIG_IDF_TARGET_ESP32C6
//	#define COREIO1_NUMIO 17
#else
#error unknown device
#endif

#define TAG MODULE_GPIO


struct CoreIO0 : public XioCluster
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


struct CoreIO1 : public XioCluster
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


static CoreIO0 GpioCluster0;
static CoreIO1 GpioCluster1;


const char *CoreIO0::getName() const
{
	return "internal0";
}


const char *CoreIO1::getName() const
{
	return "internal1";
}


unsigned CoreIO0::numIOs() const
{
	return COREIO0_NUMIO;
}


unsigned CoreIO1::numIOs() const
{
	return COREIO1_NUMIO;
}


int CoreIO0::config(uint8_t num, xio_cfg_t cfg)
{
	log_dbug(TAG,"config0 %u,0x%x",num,cfg);
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
		GPIO.enable_w1ts = (1 << num);
		gpio_matrix_out(num, SIG_GPIO_OUT_IDX, false, false);
		GPIO.pin[num].pad_driver = 0;
		log_dbug(TAG,"output %u",num);
	} else if (cfg.cfg_io == xio_cfg_io_od) {
		gpio_pad_select_gpio(num);
		PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[num]);
		GPIO.enable_w1ts = (1 << num);
		gpio_matrix_out(num, SIG_GPIO_OUT_IDX, false, false);
		GPIO.pin[num].pad_driver = 1;
		log_dbug(TAG,"open-drain %u",num);
	} else if (cfg.cfg_io != xio_cfg_io_keep) {
		return -EINVAL;
	}

	if (cfg.cfg_pull == xio_cfg_pull_keep) {
		log_dbug(TAG,"keep pull %u",num);
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


int CoreIO1::config(uint8_t num, xio_cfg_t cfg)
{
	log_dbug(TAG,"config1 %u,0x%x",num,cfg);
	if (num >= COREIO1_NUMIO) {
		log_warn(TAG,"invalid gpio%u",num+32);
		return -EINVAL;
	}
	int r = 0;
	uint8_t xnum = num+32;
	if (cfg.cfg_io == xio_cfg_io_keep) {
		log_dbug(TAG,"keep io %u",xnum);
	} else if (cfg.cfg_io == xio_cfg_io_in) {
		gpio_pad_select_gpio(xnum);
		PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[xnum]);
		GPIO.enable1_w1tc.data = (1 << num);
		GPIO.pin[xnum].pad_driver = 0;
		REG_WRITE(GPIO_FUNC0_OUT_SEL_CFG_REG + (xnum * 4), SIG_GPIO_OUT_IDX);
	} else if (cfg.cfg_io == xio_cfg_io_out) {
		gpio_pad_select_gpio(xnum);
		PIN_INPUT_DISABLE(GPIO_PIN_MUX_REG[xnum]);
		GPIO.enable1_w1ts.data = (1 << num);
		gpio_matrix_out(xnum, SIG_GPIO_OUT_IDX, false, false);
		GPIO.pin[xnum].pad_driver = 0;
	} else if (cfg.cfg_io == xio_cfg_io_od) {
		gpio_pad_select_gpio(xnum);
		PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[xnum]);
		GPIO.enable1_w1ts.data = (1 << num);
		gpio_matrix_out(xnum, SIG_GPIO_OUT_IDX, false, false);
		GPIO.pin[xnum].pad_driver = 1;
	} else if (cfg.cfg_io != xio_cfg_io_keep) {
		return -EINVAL;
	}

	if (cfg.cfg_pull == xio_cfg_pull_keep) {
		log_dbug(TAG,"keep pull %u",xnum);
		if (REG_GET_BIT(GPIO_PIN_MUX_REG[xnum],FUN_PU))
			r |= xio_cap_pullup;
		if (REG_GET_BIT(GPIO_PIN_MUX_REG[xnum],FUN_PD))
			r |= xio_cap_pulldown;
	} else if (cfg.cfg_pull == xio_cfg_pull_none) {
		log_dbug(TAG,"pull none %u",xnum);
		REG_CLR_BIT(GPIO_PIN_MUX_REG[xnum], FUN_PU);
		REG_CLR_BIT(GPIO_PIN_MUX_REG[xnum], FUN_PD);
	} else if (cfg.cfg_pull == xio_cfg_pull_up) {
		log_dbug(TAG,"pull up %u",xnum);
		REG_CLR_BIT(GPIO_PIN_MUX_REG[xnum], FUN_PD);
		REG_SET_BIT(GPIO_PIN_MUX_REG[xnum], FUN_PU);
	} else if (cfg.cfg_pull == xio_cfg_pull_down) {
		log_dbug(TAG,"pull down %u",xnum);
		REG_CLR_BIT(GPIO_PIN_MUX_REG[xnum], FUN_PU);
		REG_SET_BIT(GPIO_PIN_MUX_REG[xnum], FUN_PD);
	} else if (cfg.cfg_pull == xio_cfg_pull_updown) {
		log_dbug(TAG,"pull updown %u",num);
		REG_SET_BIT(GPIO_PIN_MUX_REG[xnum], FUN_PU);
		REG_SET_BIT(GPIO_PIN_MUX_REG[xnum], FUN_PD);
	} else {
		return -EINVAL;
	}
	
	if (cfg.cfg_intr == xio_cfg_intr_keep) {
		log_dbug(TAG,"keep intr %u",xnum);
	} else if (cfg.cfg_intr == xio_cfg_intr_disable) {
		GPIO.pin[xnum].int_type = cfg.cfg_intr;
		GPIO.pin[xnum].int_ena = 0;
		gpio_intr_disable((gpio_num_t)xnum);
		log_dbug(TAG,"interrupt disable %u",xnum);
	} else if (cfg.cfg_intr < xio_cfg_intr_keep) {
		// GPIO.pin[xnum].int_type = cfg.cfg_intr;
		// GPIO.pin[xnum].int_ena = 1;
		// gpio_intr_enable((gpio_num_t)xnum);
		if (esp_err_t e = gpio_set_intr_type((gpio_num_t)xnum,(gpio_int_type_t)cfg.cfg_intr))
			log_error(TAG,"set intr type %d",e);
		else if (esp_err_t e = gpio_intr_enable((gpio_num_t)xnum))
			log_error(TAG,"intr enable %d",e);
		else
			log_dbug(TAG,"interrupt on %s on %u",GpioIntrTriggerStr[cfg.cfg_intr],num);
	} else {
		return -EINVAL;
	}

	if (cfg.cfg_wakeup == xio_cfg_wakeup_keep) {
		log_dbug(TAG,"keep wakeup %u",xnum);
		if (REG_GET_BIT(GPIO_PIN_MUX_REG[xnum],SLP_SEL))
			r |= xio_cap_wakeup;
	} else if (cfg.cfg_wakeup == xio_cfg_wakeup_disable) {
		GPIO.pin[xnum].wakeup_enable = 0;
	} else if (GPIO.pin[xnum].int_type == 4) {
		GPIO.pin[xnum].wakeup_enable = 1;
	} else if (GPIO.pin[xnum].int_type == 5) {
		GPIO.pin[xnum].wakeup_enable = 1;
	} else {
		return -EINVAL;
	}

	if (cfg.cfg_initlvl == xio_cfg_initlvl_keep) {
	} else if (cfg.cfg_initlvl == xio_cfg_initlvl_low) {
		set_lo(num);
	} else if (cfg.cfg_initlvl == xio_cfg_initlvl_high) {
		set_hi(num);
	} else {
		return -EINVAL;
	}
	return r;
}


/*
xio_cfg_t CoreIO0::get_config() const
{
	xio_cfg_t r = XIOCFG_INIT;

}
*/

int CoreIO0::get_dir(uint8_t num) const
{
	if (num >= COREIO0_NUMIO)
		return -1;
	if (GPIO.pin[num].pad_driver)
		return xio_cfg_io_od;
	if (GPIO.enable & (1 << num))
		return xio_cfg_io_out;
	return xio_cfg_io_in;
}


int CoreIO1::get_dir(uint8_t num) const
{
	if (num >= COREIO1_NUMIO)
		return -1;
	if (GPIO.pin[num+32].pad_driver)
		return xio_cfg_io_od;
	if (GPIO.enable1.data & (1 << num))
		return xio_cfg_io_out;
	return xio_cfg_io_in;
}


int CoreIO0::get_lvl(uint8_t num)
{
	if (num < COREIO0_NUMIO) {
		if (GPIO.enable & (1 << num))
			return (GPIO.out >> num) & 0x1;
		else
			return (GPIO.in >> num) & 0x1;
	}
	return -EINVAL;
}


int CoreIO1::get_lvl(uint8_t num)
{
	if (num < COREIO1_NUMIO) {
		return (GPIO.in1.data >> num) & 0x1;
	}
	return -EINVAL;
}


int CoreIO0::get_out(uint8_t num)
{
	if (num < COREIO0_NUMIO) {
		if (((GPIO.enable >> num) & 1) == 0)
			return xio_lvl_hiz;
		return (GPIO.out >> num) & 1;
	}
	return -EINVAL;
}


int CoreIO1::get_out(uint8_t num)
{
	if (num < COREIO1_NUMIO) {
		if (((GPIO.enable1.val >> num) & 1) == 0)
			return xio_lvl_hiz;
		return (GPIO.out1.val >> num) & 1;
	}
	return -EINVAL;
}


int CoreIO0::set_hi(uint8_t num)
{
	if (uint32_t b = 1 << num) {
		GPIO.out_w1ts = b;
		GPIO.enable_w1ts = b;
		return 0;
	}
	return -EINVAL;
}


int CoreIO1::set_hi(uint8_t num)
{
	if (num < COREIO1_NUMIO) {
		uint32_t b = 1 << num;
		GPIO.out1_w1ts.data = b;
		GPIO.enable1_w1ts.data = b;
		return 0;
	}
	return -EINVAL;
}


int CoreIO0::set_hiz(uint8_t num)
{
	if (uint32_t b = 1 << num) {
		GPIO.out_w1tc = b;
		GPIO.enable_w1tc = b;
		return 0;
	}
	return -EINVAL;
}


int CoreIO1::set_hiz(uint8_t num)
{
	if (num < COREIO1_NUMIO) {
		uint32_t b = 1 << num;
		GPIO.out1_w1tc.data = b;
		GPIO.enable1_w1tc.data = b;
		return 0;
	}
	return -EINVAL;
}


int CoreIO0::set_lo(uint8_t num)
{
	if (uint32_t b = 1 << num) {
		GPIO.out_w1tc = b;
		GPIO.enable_w1ts = b;
		return 0;
	}
	return -EINVAL;
}


int CoreIO1::set_lo(uint8_t num)
{
	if (num < COREIO1_NUMIO) {
		uint32_t b = 1 << num;
		GPIO.out1_w1tc.data = b;
		GPIO.enable1_w1ts.data = b;
		return 0;
	}
	return -EINVAL;
}


int CoreIO0::set_lvl(uint8_t num, xio_lvl_t l)
{
	if (uint32_t b = 1 << num) {
		if (l == xio_lvl_0) {
			GPIO.out_w1tc = b;
			GPIO.enable_w1ts = b;
			return 0;
		} else if (l == xio_lvl_1) {
			GPIO.out_w1ts = b;
			GPIO.enable_w1ts = b;
			return 0;
		} else if (l == xio_lvl_hiz) {
			GPIO.out_w1tc = b;
			GPIO.enable_w1tc = b;
			return 0;
		}
	}
	return -EINVAL;
}


int CoreIO1::set_lvl(uint8_t num, xio_lvl_t l)
{
	if (num < COREIO1_NUMIO) {
		uint32_t b = 1 << num;
		if (l == xio_lvl_0) {
			GPIO.out1_w1tc.data = b;
			GPIO.enable1_w1ts.data = b;
			return 0;
		} else if (l == xio_lvl_1) {
			GPIO.out1_w1ts.data = b;
			GPIO.enable1_w1ts.data = b;
			return 0;
		} else if (l == xio_lvl_hiz) {
			GPIO.out1_w1tc.data = b;
			GPIO.enable1_w1tc.data = b;
			return 0;
		}
	}
	return -EINVAL;
}


int CoreIO0::hold(uint8_t num)
{
	if (num < COREIO0_NUMIO) {
		gpio_pad_hold(num);
		return 0;
	}
	return -EINVAL;
}


int CoreIO1::hold(uint8_t num)
{
	if (num < COREIO1_NUMIO) {
		gpio_pad_hold(num+COREIO0_NUMIO);
		return 0;
	}
	return -EINVAL;
}


int CoreIO0::unhold(uint8_t num)
{
	if (num < COREIO0_NUMIO) {
		gpio_pad_unhold(num);
		return 0;
	}
	return -EINVAL;
}


int CoreIO1::unhold(uint8_t num)
{
	if (num < COREIO1_NUMIO) {
		gpio_pad_unhold(num+COREIO0_NUMIO);
		return 0;
	}
	return -EINVAL;
}


int CoreIO0::setm(uint32_t v, uint32_t m)
{
	if ((v ^ m) & v)
		return -EINVAL;
	GPIO.out_w1ts = v & m;
	GPIO.out_w1tc = v ^ m;
	return 0;
}


int CoreIO1::setm(uint32_t v, uint32_t m)
{
	if (m >> COREIO1_NUMIO)
		return -EINVAL;
	if ((v ^ m) & v)
		return -EINVAL;
	GPIO.out1_w1ts.data = v & m;
	GPIO.out1_w1tc.data = v ^ m;
	return 0;
}


int CoreIO0::set_intr(uint8_t gpio, xio_intrhdlr_t hdlr, void *arg)
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


int CoreIO1::set_intr(uint8_t gpio, xio_intrhdlr_t hdlr, void *arg)
{
	if (gpio >= COREIO1_NUMIO) {
		log_warn(TAG,"set intr: invalid gpio%u",gpio+32);
		return -EINVAL;
	}
	gpio += 32;
	if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)gpio,hdlr,(void*)arg)) {
		log_warn(TAG,"add isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
		return e;
	}
	log_dbug(TAG,"attached isr to gpio%u",gpio);
	return 0;
}


#if 0
int CoreIO0::intr_enable(uint8_t gpio)
{
	if (gpio >= 32) {
		log_warn(TAG,"enable intr: invalid gpio%u",gpio);
		return -EINVAL;
	}
	if (esp_err_t e = gpio_intr_enable((gpio_num_t)gpio)) {
		log_warn(TAG,"enable isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
		return e;
	}
	log_dbug(TAG,"enable isr on gpio%u",gpio);
	return 0;
}


int CoreIO1::intr_enable(uint8_t gpio)
{
	if (gpio >= COREIO1_NUMIO) {
		log_warn(TAG,"enable intr: invalid gpio%u",gpio);
		return -EINVAL;
	}
	gpio += 32;
	if (esp_err_t e = gpio_intr_enable((gpio_num_t)gpio)) {
		log_warn(TAG,"enable isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
		return e;
	}
	log_dbug(TAG,"enable isr on gpio%u",gpio);
	return 0;
}


int CoreIO0::intr_disable(uint8_t gpio)
{
	if (gpio >= 32) {
		log_warn(TAG,"disable intr: invalid gpio%u",gpio);
		return -EINVAL;
	}
	if (esp_err_t e = gpio_intr_disable((gpio_num_t)gpio)) {
		log_warn(TAG,"disable isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
		return e;
	}
	log_dbug(TAG,"disable isr on gpio%u",gpio);
	return 0;
}


int CoreIO1::intr_disable(uint8_t gpio)
{
	if (gpio >= COREIO1_NUMIO) {
		log_warn(TAG,"disable intr: invalid gpio%u",gpio);
		return -EINVAL;
	}
	gpio += 32;
	if (esp_err_t e = gpio_intr_disable((gpio_num_t)gpio)) {
		log_warn(TAG,"disable isr handler to gpio%u: %s",gpio,esp_err_to_name(e));
		return e;
	}
	log_dbug(TAG,"disable isr on gpio%u",gpio);
	return 0;
}
#endif


int coreio_config(uint8_t num, xio_cfg_t cfg)
{
	if (num < COREIO0_NUMIO)
		return GpioCluster0.config(num,cfg);
	num -= COREIO0_NUMIO;
	if (num < COREIO1_NUMIO)
		return GpioCluster1.config(num,cfg);
	return -1;
}


int coreio_lvl_get(uint8_t num)
{
	if (num < COREIO0_NUMIO)
		return GpioCluster0.get_lvl(num);
	num -= COREIO0_NUMIO;
	if (num < COREIO1_NUMIO)
		return GpioCluster1.get_lvl(num);
	return -1;
}


int coreio_lvl_hi(uint8_t num)
{
	if (num < COREIO0_NUMIO)
		return GpioCluster0.set_hi(num);
	num -= COREIO0_NUMIO;
	if (num < COREIO1_NUMIO)
		return GpioCluster1.set_hi(num);
	return -1;
}


int coreio_lvl_lo(uint8_t num)
{
	if (num < COREIO0_NUMIO)
		return GpioCluster0.set_lo(num);
	num -= COREIO0_NUMIO;
	if (num < COREIO1_NUMIO)
		return GpioCluster1.set_lo(num);
	return -1;
}


int coreio_lvl_set(uint8_t num, xio_lvl_t l)
{
	if (num < COREIO0_NUMIO)
		return GpioCluster0.set_lvl(num,l);
	num -= COREIO0_NUMIO;
	if (num < COREIO1_NUMIO)
		return GpioCluster1.set_lvl(num,l);
	return -1;
}


void coreio_register()
{
	if (esp_err_t e = gpio_install_isr_service(ESP_INTR_FLAG_LOWMED))
		log_warn(TAG,"install isr service: %s",esp_err_to_name(e));
	else
		log_info(TAG,"ISR service ready");
	GpioCluster0.attach(0);
	GpioCluster1.attach(32);
}

#endif
