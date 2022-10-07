/*
 *  Copyright (C) 2020-2022, Thomas Maier-Komor
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


int ow_setup()
{
	if (!HWConf.has_onewire()) {
		log_dbug(TAG,"not configured");
		return 0;
	}
	const OneWireConfig &cfg = HWConf.onewire();
	OneWire::create(cfg.gpio(),cfg.pullup(),cfg.power());
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
			d->attach(RTData);
		}
	}
	log_dbug(TAG,"setup done");
	return 0;
}


const char *onewire(Terminal &term, int argc, const char *args[])
{
	OneWire *ow = OneWire::getInstance();
	if (ow == 0) {
		return "Not configured.";
	}
	if (argc != 2)
		return "Invalid number of arguments.";
	if (!strcmp(args[1],"scan"))
		return ow_scan() ? "Failed." : 0;
	if (!strcmp(args[1],"read"))
		return ow->readRom() ? "Failed." : 0;
	if (!strcmp(args[1],"reset"))
		return ow->resetBus() ? "Failed." : 0;
	if (!strcmp(args[1],"name")) {
		if (argc != 4)
			return "Invalid number of arguments.";
		uint64_t id = strtoll(args[2],0,0);
		if (id == 0)
			return "Invalid argument #2.";
		OwDevice *d = OwDevice::getDevice(id);
		for (auto &c : *Config.mutable_owdevices()) {
			if (c.id() == id) {
				c.set_name(args[3]);
				if (d) 
					d->setName(c.name().c_str());
				return 0;
			}
		}
		return "Unknown id.";
	}
	if (!strcmp(args[1],"list")) {
		OwDevice *d = OwDevice::firstDevice();
		while (d) {
			uint64_t id = d->getId();
			term.printf(IDFMT " %-10s %s\n",IDARG(id),d->deviceType(),d->getName());
			d = d->getNext();
		}
		return 0;
	}
	if (!strcmp(args[1],"power")) {
		ow->resetBus();
		ow->writeByte(0xb4);	// read power mode
		uint8_t b = ow->readByte();
		term.printf("%02x\n",(unsigned)b);
		return 0;
	}
	return "Invalid argument #1.";
}

#endif // CONFIG_ONEWIRE
