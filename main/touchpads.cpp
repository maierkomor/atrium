/*
 *  Copyright (C) 2020-2021, Thomas Maier-Komor
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

#ifdef CONFIG_TOUCHPAD
#ifdef ESP8266
#error no touchpad hardware on esp8266
#endif
#include "dataflow.h"
#include "event.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "terminal.h"
#include "touchpads.h"

#include <string.h>
#include <driver/touch_pad.h>


class TouchPad
{
	public:
	TouchPad(const char *n, int8_t ch, uint16_t thresh);

	uint16_t getFiltered() const
	{ return m_filtered; }

	uint16_t getRaw() const
	{ return m_raw; }

	uint16_t getThreshold() const
	{ return m_thresh; }

	int8_t getChannel() const
	{ return m_ch; }

	event_t getLastEvent()
	{ return m_filtered < m_thresh ? m_tev : m_uev; }


	private:
	static void sampleData(uint16_t *f, uint16_t *r);
	static void isr(void *);
	friend int touchpads_setup();

	const char *m_name;
	IntSignal *m_sigr, *m_sigf;
	uint16_t m_raw, m_filtered, m_thresh;
	int8_t m_ch;
	event_t m_tev,m_uev;
};


#define TAG MODULE_TP

static TouchPad **Channels = 0;
static uint8_t NumCh;


TouchPad::TouchPad(const char *name, int8_t ch, uint16_t thresh)
: m_name(name)
, m_sigr(new IntSignal(concat(name,"_raw")))
, m_sigf(new IntSignal(concat(name,"_filt")))
, m_thresh(thresh)
, m_ch(ch)
{
	m_tev = event_register(concat(name,"`touched"));
	m_uev = event_register(concat(name,"`untouched"));
}

void TouchPad::isr(void*)
{
	uint32_t intr = touch_pad_get_status();
	log_dbug(TAG,"isr %x",intr);
	touch_pad_clear_status();
	if (Channels == 0)
		return;
	for (int i = 0; i < NumCh; ++i) {
		if (Channels[i] && ((1 << Channels[i]->m_ch) & intr)) {
			log_dbug(TAG,"trigger %s",Channels[i]->m_name);
			event_isr_trigger(Channels[i]->m_tev);
		}
	}
}


void TouchPad::sampleData(uint16_t *r, uint16_t *f)
{
//	log_dbug(TAG,"sample");
	for (size_t i = 0; i < NumCh; ++i) {
		if (Channels[i] == 0)
			continue;
		uint8_t ch = Channels[i]->getChannel();
		Channels[i]->m_sigr->setValue(r[ch]);
		Channels[i]->m_sigf->setValue(f[ch]);
		Channels[i]->m_raw = r[ch];
		uint16_t v = f[ch];
		uint16_t ov = Channels[i]->m_filtered;
		Channels[i]->m_filtered = v;
		uint16_t thresh = Channels[i]->m_thresh;
		if ((ov < thresh) && (v > thresh)) {
			log_dbug(TAG,"untouch %s",Channels[i]->m_name);
			event_trigger(Channels[i]->m_uev);
		} else if ((ov > thresh) && (v < thresh)) {
			log_dbug(TAG,"touch %s",Channels[i]->m_name);
			event_trigger(Channels[i]->m_tev);
		}
	}
}


int touchpad(Terminal &term, int argc, const char *args[])
{
	term.printf("ch          raw  filt\n");
	for (int i = 0; i < NumCh; ++i) {
		if (Channels[i] != 0) {
			unsigned ch = Channels[i]->getChannel();
			uint16_t r,f;
			touch_pad_read_raw_data((touch_pad_t)ch,&r);
			touch_pad_read_filtered((touch_pad_t)ch,&f);
			term.printf("%2u last: %5u %5u\n",ch,Channels[i]->getRaw(),Channels[i]->getFiltered());
			term.printf("%2u now : %5u %5u\n",ch,r,f);
		}
	}
	return 0;
}


int touchpads_setup()
{
	if (HWConf.tp_channel().empty() || (Channels != 0))
		return 0;
	log_info(TAG,"init");
	auto &tpc = HWConf.touchpad();
	touch_pad_init();
	touch_pad_set_fsm_mode((touch_fsm_mode_t)tpc.fsm_mode());
	if (tpc.has_hvolt() && tpc.has_lvolt() && tpc.has_atten())
		touch_pad_set_voltage((touch_high_volt_t)tpc.hvolt(), (touch_low_volt_t)tpc.lvolt(), (touch_volt_atten_t)tpc.atten());
	NumCh = HWConf.tp_channel().size();
	Channels = (TouchPad **) calloc(sizeof(TouchPad*),NumCh);
	int i = 0;
	for (auto &c : HWConf.tp_channel()) {
		if (c.has_channel() && c.has_name()) {
			touch_pad_t ch = (touch_pad_t)c.channel();
			Channels[i] = new TouchPad(c.name().c_str(),c.channel(),c.threshold());
			if (esp_err_t e = touch_pad_set_thresh(ch,c.threshold()))
				log_error(TAG,"error setting threshold %u on channel %u: %s",c.threshold(),c.channel(),esp_err_to_name(e));
			else if (esp_err_t e = touch_pad_config(ch,c.threshold()))
				log_error(TAG,"error configuring rtc threshold %u on channel %u: %s",c.threshold(),c.channel(),esp_err_to_name(e));
			else
				++i;
			if (c.has_slope() && c.has_tieopt())
				touch_pad_set_cnt_mode(ch,(touch_cnt_slope_t)c.slope(),(touch_tie_opt_t)c.tieopt());
		}
	}
	touch_pad_set_filter_read_cb(TouchPad::sampleData);
	if (tpc.has_interval()) {
		touch_pad_filter_start(tpc.interval());
	} else {
		touch_pad_clear_status();
		touch_pad_isr_register(TouchPad::isr,0);
		touch_pad_intr_enable();
	}
	log_info(TAG,"%d channels initialized",i);
	return 0;
}


#endif
