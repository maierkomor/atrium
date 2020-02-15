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

#include "hc_sr04.h"

#include "log.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>


static const char TAG[] = "hc_sr04";
static SemaphoreHandle_t Sem = 0;


int HC_SR04::init(unsigned trigger,unsigned echo)
{
	if (trigger == echo)
		return -1;
	if (esp_err_t e = gpio_set_direction((gpio_num_t)trigger,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"unable to set GPIO %u as output: %s",trigger,esp_err_to_name(e));
		return -2;
	}
	if (esp_err_t e = gpio_set_direction((gpio_num_t)echo,GPIO_MODE_INPUT)) {
		log_error(TAG,"unable to set GPIO %d as input: %s",trigger,esp_err_to_name(e));
		return -3;
	}
	if (esp_err_t e = gpio_set_intr_type((gpio_num_t)echo,GPIO_INTR_ANYEDGE)) {
		log_error(TAG,"error setting interrupt type to anyadge on gpio %d: %s",echo,esp_err_to_name(e));
		return -4;
	}
	if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)echo,hc_sr04_isr,this)) {
		log_error(TAG,"registering isr handler returned %s",esp_err_to_name(e));
		return -5;
	}
	if (Sem == 0)
		Sem = xSemaphoreCreateBinary();
	m_trigger = (gpio_num_t)trigger;
	m_echo = (gpio_num_t)echo;
	gpio_set_level(m_trigger,1);
	return 0;
}


void HC_SR04::hc_sr04_isr(void *arg)
{
	int64_t now = esp_timer_get_time();
	HC_SR04 *o = (HC_SR04*) arg;
	if (gpio_get_level(o->m_echo)) {
		o->m_start = now;
	} else {
		int64_t dt = now - o->m_start;
		if (dt < 0) {
			o->m_delta = 0;
		} else if (dt > UINT32_MAX) {
			o->m_delta = UINT32_MAX;
		} else {
			o->m_delta = dt;
		}
		BaseType_t task = pdFALSE;
		xSemaphoreGiveFromISR(Sem,&task);
		if (task == pdTRUE)
			taskYIELD();
	}
}


int HC_SR04::measure(unsigned *distance)
{
	gpio_set_level(m_trigger,0);
	BaseType_t r = xSemaphoreTake(Sem,50/portTICK_PERIOD_MS);
	gpio_set_level(m_trigger,1);
	if (pdTRUE == r) {
		if (m_delta == 0)
			log_error(TAG,"negative time");
		else if (m_delta == UINT32_MAX)
			log_error(TAG,"saturated");
		*distance = (unsigned) ((float)m_delta/58.2);
		return 0;
	}
	log_warn(TAG,"timeout");
	return -1;
}


