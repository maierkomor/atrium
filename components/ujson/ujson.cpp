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

#include "ujson.h"
#include "stream.h"

#include <assert.h>
#include <math.h>



void JsonElement::toStream(stream &o) const
{
	o.print("\"");
	o.print(m_name);
	o.print("\":");
	writeValue(o);
}


void JsonElement::writeValue(stream &o) const
{

}



/*
void JsonBool::toStream(stream &o) const
{
	o.write("\"");
	o.print(m_name);
	o.write("\":");
	writeValue(o);
}
*/


void JsonBool::writeValue(stream &o) const
{
	if (m_value)
		o.print("true");
	else
		o.print("false");
}


/*
void JsonInt::toStream(stream &o) const
{
	o.write("\"");
	o.print(m_name);
	o.write("\":");
	o << m_value;
}
*/

void JsonInt::writeValue(stream &o) const
{
	o << m_value;
}


void JsonFloat::writeValue(stream &o) const
{
	if (isnan(m_value))
		o << "\"NaN\"";
	else
		o << m_value;
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
	if (JsonElement *e = m_childs) {
		e->toStream(o);
		while ((e = e->next()) != 0) {
			o << ',';
			e->toStream(o);
		}
	}
	o << '}';
}


void JsonObject::append(JsonElement *e)
{
	if (m_childs == 0) {
		m_childs = e;
		return;
	}
	JsonElement *a = m_childs;
	while (JsonElement *n = a->next())
		a = n;
	a->setNext(e);
}


JsonElement *JsonObject::get(const char *n) const
{
	JsonElement *e = m_childs;
	while (e && strcmp(e->name(),n)) {
		if (JsonObject *o = e->toObject()) {
			if (JsonElement *c = o->get(n))
				return c;
		}
		e = e->next();
	}
	return e;
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


JsonInt *JsonObject::add(const char *n, int32_t v)
{
	JsonInt *r = new JsonInt(n,v);
	append(r);
	return r;
}


JsonFloat *JsonObject::add(const char *n, float v)
{
	JsonFloat *r = new JsonFloat(n,v);
	append(r);
	return r;
}


JsonString *JsonObject::add(const char *n, const char *v)
{
	JsonString *r = new JsonString(n,v);
	append(r);
	return r;
}


void JsonDegC::writeValue(stream &o) const
{
	if (isnan(m_value)) {
		o << "NaN";
	} else {
		o << m_value;
		o.write(" \u00b0C",5);
	}
}


void JsonHumid::toStream(stream &o) const
{
	if (isnan(m_value))
		return;
	JsonElement::toStream(o);
        o << m_value;
        o.write(" %\"",3);
}


void JsonPress::toStream(stream &o) const
{
	if (isnan(m_value))
		return;
	JsonElement::toStream(o);
        o << m_value;
        o.write(" hPa\"",3);
}


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
