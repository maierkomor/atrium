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

#include "ujson.h"
#include "stream.h"

#include <assert.h>
#include <math.h>
#include <algorithm>

using namespace std;


void JsonElement::toStream(stream &o) const
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


void JsonElement::writeValue(stream &o) const
{

}


void JsonBool::writeValue(stream &o) const
{
	if (m_value)
		o.write("true",4);
	else
		o.write("false",5);
}


bool JsonNumber::isValid() const
{
	return !isnan(m_value) && !isinf(m_value);
}


void JsonNumber::writeValue(stream &o) const
{
	if (isnan(m_value)) {
		if (m_dim)
			o.write("NaN",3);
		else
			o.write("\"NaN\"",5);
	} else {
		o << m_value;
	}
}


JsonString::JsonString(const char *name, const char *v)
: JsonElement(name)
, m_value(strdup(v))
{
}


JsonString::~JsonString()
{
	free(m_value);
}


void JsonString::toStream(stream &o) const
{
	o.printf("\"%s\":\"",m_name);
	writeValue(o);
	o << '"';
}


void JsonString::writeValue(stream &o) const
{
	o.print(m_value);
}


void JsonString::set(const char *v)
{
	size_t l = strlen(v);
	char *x = (char*)realloc(m_value,l+1);
	assert(x);
	m_value = x;
	memcpy(x,v,l+1);
}


void JsonObject::toStream(stream &o) const
{

	if (m_name) {
		o.printf("\"%s\":{",m_name);
	} else {
		o << '{';
	}
	if (unsigned n = m_childs.size()) {
		unsigned c = 0;
		for (;;) {
			JsonElement *e = m_childs[c];
			e->toStream(o);
			if (++c == n)
				break;
			o << ',';
		}
	}
	o << '}';
}


void JsonObject::append(JsonElement *e)
{
	if (JsonObject *o = e->toObject())
		o->m_parent = this;
	m_childs.push_back(e);
}


JsonElement *JsonObject::get(const char *n) const
{
	for (JsonElement *e : m_childs) {
		if (0 == strcmp(e->name(),n)) {
			if (JsonNumber *n = e->toNumber()) {
				if (!isnan(n->get()))
					return e;
			} else {
				return e;
			}
		} else if (JsonObject *o = e->toObject()) {
			if (JsonElement *c = o->get(n))
				return c;
		}
	}
	return 0;
}


JsonElement *JsonObject::find(const char *n) const
{
	const JsonObject *o = this;
	while (o->m_parent)
		o = o->m_parent;
	return o->get(n);
}


JsonObject *JsonObject::add(const char *n)
{
	JsonObject *r = new JsonObject(n);
	append(r);
	return r;
}


JsonBool *JsonObject::add(const char *n, bool v)
{
	JsonBool *r = new JsonBool(n,v);
	append(r);
	return r;
}


JsonNumber *JsonObject::add(const char *n, double v)
{
	JsonNumber *r = new JsonNumber(n,v);
	append(r);
	return r;
}


JsonString *JsonObject::add(const char *n, const char *v)
{
	JsonString *r = new JsonString(n,v);
	append(r);
	return r;
}

void JsonObject::remove(JsonElement *x)
{
	auto e = m_childs.end();
	auto i = std::find(m_childs.begin(),e,x);
	if (i != e)
		m_childs.erase(i,i+1);
}

/*
JsonElement *ujson_forward(JsonElement *e, const char *basename, const char *subname)
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
	JsonObject *o = new JsonObject("object");
	o->add("bool",true);
	o->add("int",16);
	o->add("str0","somestring");
	char buf[16];
	sprintf(buf,"relay%d",17);
	o->add("str1",buf);
	bzero(buf,sizeof(buf));
	o->add("f",3.1f);
	o->append(new JsonDegC("f2",3.1));

	string s;
	strstream ss(s);
	o->toStream(ss);

	cout << s << endl;
}
#endif // TEST_MODULE
