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

#ifndef XIO_H
#define XIO_H

#if defined CONFIG_IDF_TARGET_ESP8266
#elif defined CONFIG_IDF_TARGET_ESP32
#elif defined CONFIG_IDF_TARGET_ESP32S2
#elif defined CONFIG_IDF_TARGET_ESP32S3
#elif defined CONFIG_IDF_TARGET_ESP32C3
#elif defined CONFIG_IDF_TARGET_ESP32C6
#else
#error include sdkconfig.h before xio.h
#endif

#include <stdint.h>
#include "event.h"


typedef enum xio_lvl_e {
	xio_lvl_0,
	xio_lvl_1,
	xio_lvl_hiz,
} xio_lvl_t;


typedef enum xio_intr_e {
	xio_intr_none,
	xio_intr_rise,
	xio_intr_fall,
	xio_intr_risefall,
	xio_intr_lvl0,
	xio_intr_lvl1,
} xio_intr_t;


typedef enum xio_cfg_io_e {
	xio_cfg_io_in,
	xio_cfg_io_out,
	xio_cfg_io_od,
	xio_cfg_io_keep,
} xio_cfg_io_t;


typedef enum xio_cfg_pull_e {
	xio_cfg_pull_none,
	xio_cfg_pull_up,
	xio_cfg_pull_down,
	xio_cfg_pull_updown,
	xio_cfg_pull_keep,
} xio_cfg_pull_t;


typedef enum xio_cfg_intr_e {
	xio_cfg_intr_disable	= 0,
	xio_cfg_intr_rise,
	xio_cfg_intr_fall,
	xio_cfg_intr_edges,
	xio_cfg_intr_lvl0,
	xio_cfg_intr_lvl1,
	xio_cfg_intr_keep,
} xio_cfg_intr_t;


typedef enum xio_cfg_wakeup_e {
	xio_cfg_wakeup_disable,
	xio_cfg_wakeup_enable,
	xio_cfg_wakeup_keep,
} xio_cfg_wakeup_t;


typedef enum xio_cfg_initlvl_e {
	xio_cfg_initlvl_low,
	xio_cfg_initlvl_high,
	xio_cfg_initlvl_keep,
} xio_cfg_initlvl_t;


typedef struct xio_cfg
{
	xio_cfg_io_t cfg_io : 2;
	uint8_t reserve0 : 2;
	xio_cfg_pull_t cfg_pull : 3;
	uint8_t reserve1 : 1;
	xio_cfg_intr_t cfg_intr : 3;
	uint8_t reserve2 : 1;
	xio_cfg_wakeup_t cfg_wakeup : 2;
	uint8_t reserve3 : 2;
	xio_cfg_initlvl_t cfg_initlvl : 2;
	uint8_t reserve4 : 2;
} xio_cfg_t;


#define XIOCFG_INIT { .cfg_io = xio_cfg_io_keep, .reserve0=0, .cfg_pull = xio_cfg_pull_keep, .reserve1=0, .cfg_intr = xio_cfg_intr_keep, .reserve2=0, .cfg_wakeup = xio_cfg_wakeup_keep, .reserve3=0, .cfg_initlvl = xio_cfg_initlvl_keep, .reserve4=0 }

extern const char *GpioDirStr[];
extern const char *GpioIntrTriggerStr[];
extern const char *GpioPullStr[];


#ifdef CONFIG_IOEXTENDERS

typedef uint16_t xio_t;

#define XIO_INVALID 0xffff

typedef enum xio_caps_e {
	xio_cap_none		= 0,
	xio_cap_pullup 		= 1 << 0,
	xio_cap_pulldown 	= 1 << 1,
	xio_cap_fallintr 	= 1 << 2,
	xio_cap_riseintr 	= 1 << 3,
	xio_cap_edgeintr 	= 1 << 4,
	xio_cap_lvl0intr 	= 1 << 5,
	xio_cap_lvl1intr 	= 1 << 6,
	xio_cap_od 		= 1 << 7,
	xio_cap_wakeup		= 1 << 8,
} xio_caps_t;


typedef void (*xio_intrhdlr_t)(void*);

struct XioCluster
{
	virtual ~XioCluster();	// intentionally not included

	virtual int get_lvl(uint8_t io)
	{ return -1; }

	virtual int get_out(uint8_t io)
	{ return get_lvl(io); }

	virtual int setm(uint32_t values,uint32_t mask)
	{ return -1; }

	virtual int set_hi(uint8_t io)
	{ return set_lvl(io,xio_lvl_1); }
	
	virtual int set_hiz(uint8_t io)
	{ return set_lvl(io,xio_lvl_hiz); }
	
	virtual int set_lo(uint8_t io)
	{ return set_lvl(io,xio_lvl_0); }

	virtual int config(uint8_t io, xio_cfg_t)
	{ return -1; }

	virtual int set_lvl(uint8_t io, xio_lvl_t v)
	{ return -1; }

	virtual int set_intr(uint8_t,xio_intrhdlr_t,void*)
	{ return -1; }

	virtual int set_intr_a(xio_t)
	{ return -1; }

	virtual int set_intr_b(xio_t)
	{ return -1; }

	virtual int hold(uint8_t io)
	{ return -1; }

	virtual int unhold(uint8_t io)
	{ return -1; }

//	virtual int intr_enable(uint8_t)
//	{ return -1; }

//	virtual int intr_disable(uint8_t)
//	{ return -1; }

	virtual const char *getName() const
	{ return 0; }

	virtual unsigned numIOs() const
	{ return 0; }

	virtual int get_dir(uint8_t) const
	{ return -1; }

	virtual event_t get_fallev(uint8_t io)
	{ return 0; }

	virtual event_t get_riseev(uint8_t io)
	{ return 0; }

	static unsigned numClusters()
	{ return NumInstances; }

	static XioCluster **getClusters()
	{ return Instances; }

	int attach(uint8_t);
	int getBase() const;

	static XioCluster *getInstance(const char *name);
	static XioCluster *getCluster(xio_t);

	protected:
	XioCluster();
	static unsigned getInstanceId(XioCluster *);

	uint8_t m_id, m_base;

	static XioCluster **Instances;
	static uint8_t *IdMap;
	static uint8_t NumInstances, TotalIOs;
};

int xio_config(xio_t x, xio_cfg_t c);
int xio_get_lvl(xio_t x);
int xio_get_dir(xio_t x);
int xio_set_hi(xio_t x);
int xio_set_lo(xio_t x);
int xio_set_lvl(xio_t x, xio_lvl_t l);
int xio_set_intr(xio_t x, xio_intrhdlr_t h, void *arg);
int xio_hold(xio_t x);
int xio_unhold(xio_t x);
//int xio_intr_enable(xio_t x);
//int xio_intr_disable(xio_t x);
event_t xio_get_fallev(xio_t x);
event_t xio_get_riseev(xio_t x);


#else // !CONFIG_IOEXTENDERS	###############################################


#include <driver/gpio.h>

#define xio_t gpio_num_t
#define XIO_INVALID GPIO_NUM_MAX

#define xio_set_hi(x) gpio_set_level(x,1)
#define xio_set_lo(x) gpio_set_level(x,0)
#define xio_set_lvl(x,y) gpio_set_level(x,(unsigned)y)
#define xio_get_lvl gpio_get_level
#define xio_get_dir gpio_get_direction
#define xio_set_intr gpio_isr_handler_add
#define xio_intr_enable gpio_intr_enable
#define xio_intr_disable gpio_intr_disable
#define xio_get_fallev(x) 0
#define xio_get_riseev(x) 0
#define xio_hold(x)	{}
#define xio_unhold(x)	{}
int xio_config(xio_t x, xio_cfg_t c);

#endif // CONFIG_IOEXTENDERS

#endif
