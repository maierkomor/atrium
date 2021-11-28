/*
 *  Copyright (C) 2018-2020, Thomas Maier-Komor
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

#ifdef CONFIG_HCSR04

#include "actions.h"
#include "hc_sr04.h"
#include "log.h"
#include "stream.h"
#include "ujson.h"

#include <esp_timer.h>
#include <rom/gpio.h>


#define TAG MODULE_HCSR04

HC_SR04 *HC_SR04::First = 0;

HC_SR04::HC_SR04(gpio_num_t trigger, gpio_num_t echo)
: m_dist(new JsonNumber("distance","mm"))
, m_trigger(trigger)
, m_echo(echo)
{
	gpio_set_level(m_trigger,1);
	char name[16];
	sprintf(name,"hcsr04@%d,%d",(int)trigger,(int)echo);
	m_name = strdup(name);
}


HC_SR04 *HC_SR04::create(int8_t trigger, int8_t echo)
{
	if ((trigger == echo) || (trigger < 0) || (echo < 0))
		return 0;
	gpio_pad_select_gpio(trigger);
	gpio_pad_select_gpio(echo);
	if (esp_err_t e = gpio_set_direction((gpio_num_t)trigger,GPIO_MODE_OUTPUT)) {
		log_warn(TAG,"GPIO %u output: %s",trigger,esp_err_to_name(e));
		return 0;
	}
	if (esp_err_t e = gpio_set_direction((gpio_num_t)echo,GPIO_MODE_INPUT)) {
		log_warn(TAG,"GPIO %d input: %s",trigger,esp_err_to_name(e));
		return 0;
	}
	if (esp_err_t e = gpio_set_intr_type((gpio_num_t)echo,GPIO_INTR_ANYEDGE)) {
		log_warn(TAG,"interrupt type on gpio %d: %s",echo,esp_err_to_name(e));
		return 0;
	}
	HC_SR04 *inst = new HC_SR04((gpio_num_t)trigger,(gpio_num_t)echo);
	if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)echo,hc_sr04_isr,inst)) {
		log_warn(TAG,"isr handler %s",esp_err_to_name(e));
		delete inst;
		return 0;
	}
	log_info(TAG,"device at %d/%d",(int)trigger,(int)echo);
	inst->m_next = First;
	First = inst;
	return inst;
}


int HC_SR04::attach(JsonObject *root)
{
	m_ev = event_register(m_name,"`update");
	event_callback(m_ev,action_add(concat(m_name,"!update"),HC_SR04::update,this,0));
	m_dist = root->add(m_name,NAN);
	return 0;
}


void HC_SR04::hc_sr04_isr(void *arg)
{
	int64_t now = esp_timer_get_time();
	HC_SR04 *o = (HC_SR04*) arg;
	if (gpio_get_level(o->m_echo)) {
		o->m_start = now;
	} else {
		o->m_dt = now - o->m_start;
		event_isr_trigger(o->m_ev);
	}
}


void HC_SR04::update(void *arg)
{
	HC_SR04 *o = (HC_SR04 *)arg;
	char buf[16];
	float_to_str(buf,(float)o->m_dt/58.2);
	log_dbug(TAG,"%s (%u)",buf,(unsigned)o->m_dt);
	if (o->m_dt < 0)
		o->m_dist->set(NAN);
	else
		o->m_dist->set((float)o->m_dt/58.2);
}


void HC_SR04::setName(const char *name)
{
	if (m_name)
		free(m_name);
	m_name = strdup(name);
}

void HC_SR04::trigger()
{
	gpio_set_level(m_trigger,0);
	gpio_set_level(m_trigger,1);
}


#endif
