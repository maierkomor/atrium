/*
 *  Copyright (C) 2020, Thomas Maier-Komor
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

#include "dataflow.h"
#include "func.h"
#include "log.h"
#include "stream.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <math.h>
#include <string.h>

#include <map>

using namespace std;


static char TAG[]="signal";
static DataSignal *Signals = 0;
static SemaphoreHandle_t Mtx = 0;
static multimap<DataSignal *,Function *> Triggers;


DataSignal::DataSignal(const char *name, const char *dim)
: m_name(name)
, m_dim(dim)
{
	if (Mtx == 0)
		Mtx = xSemaphoreCreateMutex();
	xSemaphoreTake(Mtx,portMAX_DELAY);
	/*
	DataSignal *s = Signals;
	while (s) {
		if (!strcmp(s->m_name,name)) {
			log_warn(TAG,"duplicate signal name %s",name);
			break;
		}
		s =  s->m_next;
	}
	*/
	m_next = Signals;
	Signals = this;
	xSemaphoreGive(Mtx);
}


void DataSignal::addFunction(Function *f)
{
	log_dbug(TAG,"%s -> %s",m_name,f->m_name);
	Triggers.emplace(this,f);
}


int DataSignal::initFrom(const char *)
{
	abort();
	return 1;
}


void DataSignal::process()
{
	log_dbug(TAG,"sinks of %s",m_name);
	auto i = Triggers.lower_bound(this);
	auto e = Triggers.end();
	while (i != e) {
		if (i->first != this)
			return;
		Function *f = i->second;
		log_dbug(TAG,"signal %s triggers function %s",m_name,f->m_name);
		(*f)(this);
		++i;
	}
}


// Complexity: O(n)
DataSignal *DataSignal::getSignal(const char *n)
{
	DataSignal *s = Signals;
	while (s && strcmp(n,s->m_name))
		s = s->m_next;
	return s;
}


DataSignal *DataSignal::first()
{
	return Signals;
}


/*
void BoolSignal::setValue(bool v)
{
	m_value = v;
	process();
}


void BoolSignal::toStream(stream &s)
{
	s << m_value;
}
*/

int IntSignal::initFrom(const char *s)
{
	char *e;
	long long ll = strtoll(s,&e,0);
	if (e == s)
		return 1;
	setValue(ll);
	return 0;
}


void IntSignal::setValue(int32_t v)
{
	log_dbug(TAG,"%s=%ld",m_name,v);
	m_value = v;
	process();
}


int FloatSignal::initFrom(const char *s)
{
	char *e;
	double d = strtod(s,&e);
	if (e == s)
		return 1;
	setValue(d);
	return 0;
}


void FloatSignal::setValue(float v)
{
	log_dbug(TAG,"%s=%f",m_name,v);
	m_value = v;
	process();
}


void DataSignal::toStream(stream &s)
{
}


void IntSignal::toStream(stream &s)
{
	s << m_value;
}


void FloatSignal::toStream(stream &s)
{
	s << m_value;
}


bool FloatSignal::isValid() const
{
	return !isnan(m_value) && !isinf(m_value);
}


int df_setup()
{
	Mtx = xSemaphoreCreateMutex();

	return 0;
}
