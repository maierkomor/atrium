/*
 *  Copyright (C) 2020-2025, Thomas Maier-Komor
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

#ifdef CONFIG_ONEWIRE

#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "onewire.h"
#include "owdevice.h"
#include "swcfg.h"
#include "terminal.h"


#define TAG MODULE_OWB


static int ow_scan()
{
	int r = OneWire::getInstance()->scanBus();
	OwDevice *d = OwDevice::firstDevice();
	auto *owdevs = Config.mutable_owdevices();
	while (d) {
		bool exist = false;
		uint64_t id = d->getId();
		for (const auto &cfg : *owdevs) {
			if (cfg.id() == id) {
				exist = true;
				if (cfg.has_name())
					d->setName(cfg.name().c_str());
				break;
			}
		}
		if (!exist) {
			OwDeviceConfig *c = Config.add_owdevices();
			c->set_name(d->getName());
			c->set_id(id);
			d->attach(RTData);
		}
		d = d->getNext();
	}
	return r;
}


void ow_setup()
{
	if (!HWConf.has_onewire()) {
		log_dbug(TAG,"not configured");
		return;
	}
	const OneWireConfig &cfg = HWConf.onewire();
	if (OneWire::create(cfg.gpio(),cfg.pullup(),cfg.power())) {
		auto *owdevs = Config.mutable_owdevices();
		for (const auto &cfg : *owdevs) {
			if (cfg.has_name()) {
				OwDevice *d = OwDevice::getDevice(cfg.id());
				if (d) {
					d->setName(cfg.name().c_str());
				} else {
					OwDevice::create(cfg.id(),cfg.name().c_str());
					d = OwDevice::getDevice(cfg.id());
				}
				if (d)
					d->attach(RTData);
			}
		}
		log_dbug(TAG,"setup done");
	} else {
		log_dbug(TAG,"no bus");
	}
}


static OwDevice *get_device(const char *arg)
{
	uint8_t addr[8];
	if (8 == sscanf(arg,"%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx"
			, addr+0, addr+1, addr+2, addr+3
			, addr+4, addr+5, addr+6, addr+7)) {
		uint64_t id
			= (uint64_t) addr[0]<<0
			| (uint64_t) addr[1]<<8
			| (uint64_t) addr[2]<<16
			| (uint64_t) addr[3]<<24
			| (uint64_t) addr[4]<<32
			| (uint64_t) addr[5]<<40
			| (uint64_t) addr[6]<<48
			| (uint64_t) addr[7]<<56
			;
		return OwDevice::getDevice(id);
	} else {
		char *e;
		long idx = strtol(arg,&e,0);
		OwDevice *d = OwDevice::firstDevice();
		while (idx) {
			--idx;
			if (0 == d)
				return 0;
			d = d->getNext();
		} 
		return d;
	}
}


const char *onewire(Terminal &term, int argc, const char *args[])
{
	OneWire *ow = OneWire::getInstance();
	if (ow == 0) {
		return "Not configured.";
	}
	if (argc == 2) {
		if (!strcmp(args[1],"scan"))
			return ow_scan() ? "Failed." : 0;
		else if (!strcmp(args[1],"read"))
			return ow->readRom() ? "Failed." : 0;
		else if (!strcmp(args[1],"reset"))
			return ow->resetBus() ? "Failed." : 0;
		else if (!strcmp(args[1],"list")) {
			OwDevice *d = OwDevice::firstDevice();
			unsigned idx = 0;
			while (d) {
				uint64_t id = d->getId();
				term.printf("%2u " IDFMT " %-10s %s\n",idx,IDARG(id),d->deviceType(),d->getName());
				++idx;
				d = d->getNext();
			}
			return 0;
		} else if (!strcmp(args[1],"power")) {
			ow->resetBus();
			ow->writeByte(0xb4);	// read power mode
			uint8_t b;
			ow->readBytes(&b,sizeof(b));
			term.printf("%02x\n",(unsigned)b);
			return 0;
		} else {
			return "Invalid argument #1.";
		}
	} else if (argc == 4) {
		if (!strcmp(args[1],"name")) {
			OwDevice *d = get_device(args[2]);
			if (0 == d)
				return "Unknown id.";
			d->setName(args[3]);
			uint64_t id = d->getId();
			for (auto &c : *Config.mutable_owdevices()) {
				if (c.id() == id) {
					c.set_name(args[3]);
					return 0;
				}
			}
			return "Failed partially.";
		} else {
			return "Invalid argument #1.";
		}
	} else {
		return "Invalid number of arguments.";
	}
	return 0;
}

#endif // CONFIG_ONEWIRE
