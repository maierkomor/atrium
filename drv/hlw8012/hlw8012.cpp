/*
 *  Copyright (C) 2018-2022, Thomas Maier-Komor
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

#ifdef CONFIG_HLW8012

#include "actions.h"
#include "env.h"
#include "hlw8012.h"
#include "log.h"

#include <esp_timer.h>

#define TAG MODULE_HLW8012

// Datasheet is chinese only.
// Available docu is unclear about how to translate the duty cycle into
// power, voltage, and current.


// If sel is not provided, it is assumed to be stuck at GND - i.e.
// measure current.
HLW8012::HLW8012(int8_t sel, int8_t cf, int8_t cf1)
: m_sel((gpio_num_t) sel)
, m_cf((gpio_num_t) cf)
, m_cf1((gpio_num_t) cf1)
{
	snprintf(m_name,sizeof(m_name),"hlw8012@%d",sel != -1 ? sel : cf != -1 ? cf : cf1);
	if (cf != -1) {
		if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)cf,intrHandlerCF,(void*)this)) {
			log_warn(TAG,"isr_handler for %d: %d",cf,e);
		}
		if (esp_err_t e = gpio_set_intr_type((gpio_num_t)cf,GPIO_INTR_ANYEDGE)) {
			log_warn(TAG,"isr type for %d: %d",cf,e);
		}
		m_ev = event_register(m_name,"`power");
		Action *a = action_add(concat(m_name,"!calcW"),calcPower,this,0);
		event_callback(m_ev,a);
	}
	if (cf1 != -1) {
		if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)cf1,intrHandlerCF1,(void*)this)) {
			log_warn(TAG,"isr_handler for %d: %d",cf1,e);
		}
		if (esp_err_t e = gpio_set_intr_type((gpio_num_t)cf1,GPIO_INTR_ANYEDGE)) {
			log_warn(TAG,"isr type for %d: %d",cf1,e);
		}
		m_ec = event_register(m_name,"`current");
		Action *c = action_add(concat(m_name,"!calcC"),calcPower,this,0);
		event_callback(m_ec,c);
		if (sel != -1) {
			m_ev = event_register(m_name,"`voltage");
			Action *v = action_add(concat(m_name,"!calcC"),calcPower,this,0);
			event_callback(m_ev,v);
		}
	}
}


HLW8012 *HLW8012::create(int8_t sel, int8_t cf, int8_t cf1)
{
	if ((cf != -1) || (cf != -1))
		return new HLW8012(sel,cf,cf1);
	return 0;
}


void HLW8012::attach(EnvObject *root)
{
	EnvObject *r = root->add(m_name);
	if (m_cf != -1) {
		m_power = r->add("power",0.0,"W");
	}
	if (m_cf1 != -1) {
		m_curr = r->add("current",0.0,"A");
		if (m_sel != -1)
			m_volt = r->add("voltage",0.0,"V");
	}
}


void HLW8012::calcPower(void *arg)
{
	HLW8012 *dev = (HLW8012 *)arg;
	int64_t dt = dev->m_tscf1[2]-dev->m_tscf1[0];
	int64_t high = dev->m_tscf1[1]-dev->m_tscf1[0];
	float duty = (float)high/(float)dt;
	log_dbug(TAG,"power duty %g",duty);
//	dev->m_power->set(...);
}


void HLW8012::calcCurrent(void *arg)
{
	HLW8012 *dev = (HLW8012 *)arg;
	int64_t dt = dev->m_tscf[2]-dev->m_tscf[0];
	int64_t high = dev->m_tscf[1]-dev->m_tscf[0];
	float duty = (float)high/(float)dt;
	log_dbug(TAG,"current duty %g",duty);
//	dev->m_current->set(...);
}


void HLW8012::calcVoltage(void *arg)
{
	HLW8012 *dev = (HLW8012 *)arg;
	int64_t dt = dev->m_tscf[2]-dev->m_tscf[0];
	int64_t high = dev->m_tscf[1]-dev->m_tscf[0];
	float duty = (float)high/(float)dt;
	log_dbug(TAG,"voltage duty %g",duty);
//	dev->m_volt->set(...);
}


void HLW8012::intrHandlerCF(void *arg)
{
	HLW8012 *dev = (HLW8012 *)arg;
	int64_t now = esp_timer_get_time();
	bool high = gpio_get_level(dev->m_cf);
	if (high) {
		dev->m_tscf[0] = dev->m_tscf[2];
		dev->m_tscf[2] = now;
		event_isr_trigger(dev->m_ep);
	} else {
		dev->m_tscf[1] = now;
	}
}


void HLW8012::intrHandlerCF1(void *arg)
{
	HLW8012 *dev = (HLW8012 *)arg;
	int64_t now = esp_timer_get_time();
	bool high = gpio_get_level(dev->m_cf1);
	if (high) {
		dev->m_tscf1[0] = dev->m_tscf1[2];
		dev->m_tscf1[2] = now;
		if (dev->m_sel == -1) {
			event_isr_trigger(dev->m_ec);
		} else if (gpio_get_level(dev->m_sel)) {
			event_isr_trigger(dev->m_ev);
			gpio_set_level(dev->m_sel,0);
		} else {
			event_isr_trigger(dev->m_ec);
			gpio_set_level(dev->m_sel,1);
		}
	} else {
		dev->m_tscf1[1] = now;
	}
}

#endif // CONFIG_HLW8012
