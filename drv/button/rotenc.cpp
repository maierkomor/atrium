/*
 *  Copyright (C) 2022-2025, Thomas Maier-Komor
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

#ifdef CONFIG_ROTARYENCODER

#include "actions.h"
#include "button.h"
#include "log.h"
#include "rotenc.h"
#include <esp_timer.h>
//#include <rom/uart.h>	// for debugging, if needed

#define TAG MODULE_BUTTON


RotaryEncoder::RotaryEncoder(const char *name, xio_t clk, xio_t dt, xio_t sw)
: m_clk(clk)
, m_dt(dt)
, m_sw(sw)
, m_rev(event_register(name,"`released"))
, m_pev(event_register(name,"`pressed"))
, m_sev(event_register(name,"`short"))
, m_mev(event_register(name,"`med"))
, m_rlev(event_register(name,"`left"))
, m_rrev(event_register(name,"`right"))
{ 
	log_info(TAG,"rotary encoder %s at clk=%d,dt=%d,sw=%d",name,clk,dt,sw);
}


RotaryEncoder *RotaryEncoder::create(const char *name, xio_t clk, xio_t dt, xio_t sw, xio_cfg_pull_t pull)
{
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_in;
	cfg.cfg_intr = xio_cfg_intr_edges;
	cfg.cfg_pull = pull;
	if (0 > xio_config(clk,cfg)) {
		log_warn(TAG,"config clk@%u failed",clk);
		return 0;
	}
	if (0 > xio_config(dt,cfg)) {
		log_warn(TAG,"config dt@%u failed",dt);
		return 0;
	}
	if (sw != XIO_INVALID) {
		if (0 > xio_config(sw,cfg)) {
			log_warn(TAG,"config sw@%u failed",sw);
			sw = XIO_INVALID;
		}
	}
	RotaryEncoder *dev= new RotaryEncoder(name,clk,dt,sw);
	if (xio_set_intr(clk,clkIntr,dev)) {
		event_t fev = xio_get_fallev(clk);
		event_t rev = xio_get_riseev(clk);
		if (fev && rev) {
			Action *a = action_add(concat(name,"!clk_ev"),clk_ev,dev,0);
			event_callback(fev,a);
			event_callback(rev,a);
		} else {
			log_warn(TAG,"xio%u no irq or event",clk);
		}
	}
	if (xio_set_intr(dt,dtIntr,dev)) {
		event_t fev = xio_get_fallev(dt);
		event_t rev = xio_get_riseev(dt);
		if (fev && rev) {
			Action *a = action_add(concat(name,"!dt_ev"),dtIntr,dev,0);
			event_callback(fev,a);
			event_callback(rev,a);
		} else {
			log_warn(TAG,"xio%u no irq or event",dt);
		}
	}
	if (sw != XIO_INVALID)
		if (xio_set_intr(sw,swIntr,dev)) {
			event_t fev = xio_get_fallev(sw);
			event_t rev = xio_get_riseev(sw);
			if (fev && rev) {
				Action *a = action_add(concat(name,"!sw_ev"),sw_ev,dev,0);
				event_callback(fev,a);
				event_callback(rev,a);
			} else {
				log_warn(TAG,"xio%u no irq or event",sw);
			}
		}
	return dev;
}


void RotaryEncoder::sw_ev(void *arg)
{
	int32_t now = esp_timer_get_time() / 1000;
	RotaryEncoder *dev = static_cast<RotaryEncoder*>(arg);
	int sw = xio_get_lvl(dev->m_sw);
	if (sw) {
		if (dev->m_lsw == 0) {
			event_trigger(dev->m_rev);
			int dt = now - dev->m_ptime;
			log_dbug(TAG,"dt=%d",dt);
			if ((dt >= BUTTON_SHORT_START) && (dt < BUTTON_SHORT_END))
				event_trigger(dev->m_sev);
			else if ((dt >= BUTTON_MED_START) && (dt < BUTTON_MED_END))
				event_trigger(dev->m_mev);
			dev->m_ptime = 0;
		}
	} else {
		if (dev->m_lsw == 1) {
			event_trigger(dev->m_pev);
			dev->m_ptime = now;
		}
	}
	dev->m_lsw = sw;
}


// this is an ISR!
void RotaryEncoder::swIntr(void *arg)
{
	// no log_* from ISRs!
	int32_t now = esp_timer_get_time() / 1000;
	RotaryEncoder *dev = static_cast<RotaryEncoder*>(arg);
	int sw = xio_get_lvl(dev->m_sw);
	if (sw) {
		if (dev->m_lsw == 0) {
			event_isr_trigger(dev->m_rev);
			int dt = now - dev->m_ptime;
			if ((dt >= BUTTON_SHORT_START) && (dt < BUTTON_SHORT_END))
				event_isr_trigger(dev->m_sev);
			else if ((dt >= BUTTON_MED_START) && (dt < BUTTON_MED_END))
				event_isr_trigger(dev->m_mev);
			dev->m_ptime = 0;
		}
	} else {
		if (dev->m_lsw == 1) {
			event_isr_trigger(dev->m_pev);
			dev->m_ptime = now;
		}
	}
	dev->m_lsw = sw;
}


// left: d-c-sequence with d01,c00 d10,c11
// right c-d-sequence with c01,c10,d00,d11
// left->left: cxx/dxx,cxx/dxx
// right->right: cxx/dxx,cxx/dxx
// left->right: c-d-d-c,d-c-c-d
// left->right dCDcdD
// right->left cDCdc
// left->right if c = d, if C=>D
// right->left c=>D, C=d

/*	-2	-1	0
 *	links->links
 *		d01	c00
 *		d10	c11
 *	right->right
 *		c01	d11
 *		c10	d00
 *
 * 	left->right
 *		c10	d00
 *	c00	c01	d11
 *
 *	right->left
 *	d00	d10	c11
 *	d11	d01	c00
 *	c11	d01	c00
 *
 *	discard
 *	c01	
 *
 *	from picture:
 *	clockwise: 	ArBh,AfBl, BrAl,BfAh
 *	countercw:	ArBl,AfBh, BrAh,BfAl
 *
 *	a-edges:	l:ArBl,AfBh	r:ArBh,AfBl
 *	b-edges		l:BrAh,BfAl	r:BrAl,BfAh
 *
 *	sequences:
 *
 */

static const uint8_t LSeqD[] = {0x23,0x31,0x10,0x02};
static const uint8_t LSeq[] = {11,13,4,2};
static const uint8_t RSeqD[] = {0x01,0x13,0x32,0x20};
static const uint8_t RSeq[] = {1,5,14,8};
static const uint8_t Dirs[] = {0,1,1,1,1,0,2,2,0,2,0,0,1,0,2,0};	//1=R,2=L

// links 6,7,9,e
// rechts: 1,2,3,4,b,c,e

/*
 *
 * Links: 00 11 00 11
 * Rechts: 01 10 01 10
 * Links 3 12 3 12
 * Rechts: 6 9 6 9
 *
 * Messung:
 * Rechts 1011 1110 1100
 *
 */

const char *Hex = "0123456789abcdef";
void RotaryEncoder::clkIntr(void *arg)
{
	// no log_* from ISRs!
	RotaryEncoder *dev = static_cast<RotaryEncoder*>(arg);
	int dt = xio_get_lvl(dev->m_dt);
//	int clk = xio_get_lvl(dev->m_clk);
	uint8_t nst = (dev->m_lc << 1) | dt;
//	uart_tx_one_char(Hex[(dev->m_lst>>3)&1]);
//	uart_tx_one_char(Hex[(dev->m_lst>>2)&1]);
//	uart_tx_one_char(Hex[(dev->m_lst>>1)&1]);
//	uart_tx_one_char(Hex[(dev->m_lst>>0)&1]);
//	uart_tx_one_char(' ');
//	uart_tx_one_char('0'+dt);
//	uart_tx_one_char('0'+clk);
//	uart_tx_one_char(' ');
	if (nst == (dev->m_lst&3)) {
//		uart_tx_one_char('I');
//		uart_tx_one_char('\n');
		return;
	}
	nst |= (dev->m_lst << 2) & 0xc;
	dev->m_lst = nst;
//	uart_tx_one_char('>');
//	uart_tx_one_char(Hex[nst]);
//	uart_tx_one_char('D');
//	uart_tx_one_char(Hex[Dirs[nst]]);
//	uart_tx_one_char('\n');
	if (Dirs[nst] == 1)
		event_isr_trigger(dev->m_rrev);
	else if (Dirs[nst] == 2)
		event_isr_trigger(dev->m_rlev);
}


void RotaryEncoder::clk_ev(void *arg)
{
	// no log_* from ISRs!
	RotaryEncoder *dev = static_cast<RotaryEncoder*>(arg);
	int dt = xio_get_lvl(dev->m_dt);
	uint8_t nst = (dev->m_lc << 1) | dt;
	if (nst == (dev->m_lst&3)) {
		return;
	}
	nst |= (dev->m_lst << 2) & 0xc;
	dev->m_lst = nst;
	if (Dirs[nst] == 1)
		event_trigger(dev->m_rrev);
	else if (Dirs[nst] == 2)
		event_trigger(dev->m_rlev);
}


void RotaryEncoder::dtIntr(void *arg)
{
	// no log_* from ISRs!
	RotaryEncoder *dev = static_cast<RotaryEncoder*>(arg);
	int clk = xio_get_lvl(dev->m_clk);
	dev->m_lc = clk;
//	uart_tx_one_char('d');
//	uart_tx_one_char(Hex[clk]);
//	uart_tx_one_char('\n');
}

#endif
