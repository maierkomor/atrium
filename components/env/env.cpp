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

#include "env.h"
#include "stream.h"

#include <assert.h>
#include <math.h>
#include <algorithm>

using namespace std;

EnvElement::~EnvElement()
{
	free(m_name);
}


void EnvElement::toStream(stream &o) const
{
	o.print("\"");
	o.print(m_name);
	o.print("\":");
	if (m_dim) {
		o.write("\"",1);
		writeValue(o);
		o.write(" ",1);
		o.print(m_dim);
		o.write("\"",1);
	} else {
		writeValue(o);
	}
}


void EnvElement::writeValue(stream &o) const
{
	abort();
}


void EnvBool::writeValue(stream &o) const
{
	if (m_value)
		o.write("true",4);
	else
		o.write("false",5);
}


void EnvNumber::writeValue(stream &o) const
{
	if (isValid()) {
		o << get();
	} else if (m_dim) {
		o.write("NaN",3);
	} else {
		o.write("\"NaN\"",5);
	}
}


EnvString::EnvString(const char *name, const char *v, const char *dim)
: EnvElement(name,dim)
, m_value(strdup(v))
{
}


EnvString::~EnvString()
{
	free(m_value);
}


void EnvString::toStream(stream &o) const
{
	o.printf("\"%s\":\"",m_name);
	writeValue(o);
	o << '"';
}


void EnvString::writeValue(stream &o) const
{
	o.print(get());
}


void EnvString::set(const char *v)
{
	size_t l = strlen(v);
	set(v,l+1);
}


void EnvString::set(const char *v, size_t l)
{
	char *x = (char*)realloc(m_value,l + (v[l-1] != 0));
	assert(x);
	m_value = x;
	memcpy(x,v,l);
	if (v[l-1])
		x[l] = 0;
}


void EnvObject::toStream(stream &o) const
{

	if (m_name) {
		o.printf("\"%s\":{",m_name);
	} else {
		o << '{';
	}
	if (unsigned n = m_childs.size()) {
		unsigned c = 0;
		for (;;) {
			EnvElement *e = m_childs[c];
			e->toStream(o);
			if (++c == n)
				break;
			o << ',';
		}
	}
	o << '}';
}


void EnvObject::append(EnvElement *e)
{
	if (EnvObject *o = e->toObject())
		o->m_parent = this;
	m_childs.push_back(e);
}


EnvElement *EnvObject::get(const char *n) const
{
	for (EnvElement *e : m_childs) {
		if (0 == strcmp(e->name(),n)) {
			if (EnvNumber *n = e->toNumber()) {
				if (!isnan(n->get()))
					return e;
			} else {
				return e;
			}
		} else if (EnvObject *o = e->toObject()) {
			if (EnvElement *c = o->get(n))
				return c;
		}
	}
	return 0;
}


EnvElement *EnvObject::find(const char *n) const
{
	const EnvObject *o = this;
	while (o->m_parent)
		o = o->m_parent;
	return o->get(n);
}


EnvObject *EnvObject::add(const char *n)
{
	EnvObject *r = new EnvObject(n);
	append(r);
	return r;
}


EnvBool *EnvObject::add(const char *n, bool v, const char *dim)
{
	EnvBool *r = new EnvBool(n,v,dim);
	append(r);
	return r;
}


EnvNumber *EnvObject::add(const char *n, double v, const char *dim)
{
	EnvNumber *r = new EnvNumber(n,v,dim);
	append(r);
	return r;
}


EnvString *EnvObject::add(const char *n, const char *v, const char *dim)
{
	EnvString *r = new EnvString(n,v,dim);
	append(r);
	return r;
}

void EnvObject::remove(EnvElement *x)
{
	auto e = m_childs.end();
	auto i = std::find(m_childs.begin(),e,x);
	if (i != e)
		m_childs.erase(i,i+1);
}

/*
EnvElement *ujson_forward(EnvElement *e, const char *basename, const char *subname)
{
	assert(e);
	size_t l = strlen(basename);
	do {
		e = e->next();
	} while (e && (memcmp(e->name(),basename,l) || strcmp(e->name()+l,subname)));
	assert(e);
	return e;
}
*/


#ifdef TEST_MODULE
#include <string>
#include <iostream>
#include "strstream.h"
using namespace std;

int main()
{
	EnvObject *o = new EnvObject("object");
	o->add("bool",true);
	o->add("int",16);
	o->add("str0","somestring");
	char buf[16];
	sprintf(buf,"relay%d",17);
	o->add("str1",buf);
	bzero(buf,sizeof(buf));
	o->add("f",3.1f);
	o->append(new EnvDegC("f2",3.1));

	string s;
	strstream ss(s);
	o->toStream(ss);

	cout << s << endl;
}
#endif // TEST_MODULE
