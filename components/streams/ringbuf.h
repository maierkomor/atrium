/*
 *  Copyright (C) 2021-2022, Thomas Maier-Komor
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


#ifndef RINGBUF_H
#define RINGBUF_H

#include <assert.h>


template <typename T>
struct SlidingWindow
{
	typedef uint16_t idx_t;

	explicit SlidingWindow(size_t s)
	: m_data(new T[s])
	, m_size(s)
	{
		bzero(m_data,sizeof(T)*s);
	}

	void put(T d)
	{
		m_sum -= m_data[m_at];
		m_sum += d;
		m_data[m_at] = d;
		++m_at;
		m_at %= m_size;
	}

	unsigned sum() const
	{ return m_sum; }

	float avg() const
	{ return (T)((float)m_sum/(float)m_size); }

	private:
	SlidingWindow(const SlidingWindow &);
	SlidingWindow &operator = (const SlidingWindow &);
	T *m_data;
	idx_t m_size, m_at = 0;
	unsigned m_sum = 0;
};


/*
template <typename T>
class RingBuffer
{
	public:
	explicit RingBuffer(uint16_t s)
	: m_buf(new T[s])
	, m_size(s)
	{

	}


	~RingBuffer()
	{
		delete[] m_buf;
	}

	void clean(size_t s)
	{
		assert(s <= numFill());
		m_st += s;
		if (m_st >= m_size)
			m_st -= m_size;
	}

	int put(T v)
	{
		assert(m_in != m_out);


	}
	
	T get()
	{
		assert(m_out != m_size);
		uint16_t at = m_out;
		++m_out;
		if (m_out == m_in) {
			m_out = m_size;
		} else if (++m_out == m_size)
			m_out = 0;
		}
		return m_buf[at];

	}

	size_t numFree() const
	{
		return m_size - numFill();
	}

	size_t getSize() const
	{ return m_size; }

	int numFill() const
	{
		if (m_out == m_size)
			return 0;
		if (m_in > m_out)
			return m_in - m_out;
		return m_in + (m_size-m_out);
	}

	private:
	RingBuffer(const RingBuffer &);
	RingBuffer &operator = (const RingBuffer &);

	T *m_buf;
	// m_out = m_size if empty
	// m_out = m_in if full
	uint16_t m_size, m_in = 0, m_out = 0;
};
*/

/*
class RingBuffer
{
	public:
	explicit RingBuffer(uint16_t s);
	~RingBuffer();

	void clean(size_t s)
	{
		assert(s <= numFill());
		m_st += s;
		if (m_st >= m_size)
			m_st -= m_size;
	}

	unsigned write(char *t, size_t s)
	{
		assert(m_size - numFill() > s);
		size_t n = m_size-m_end;
		if (n > s) {
			memcpy(m_buf+m_end,t,s);
		} else {

		}
		m_end += s;
		if (m_end > m_size)
			m_end -= m_size;
	}


	void copyTo(struct *pbuf, unsigned poff, unsigned off, unsigned num);

	void resize(unsigned s);

	size_t numFree() const
	{
		return m_size - numFill();
	}

	size_t getSize() const
	{ return m_size; }

	int numFill() const
	{
		int n = m_end-m_st;
		if (n < 0)
			n += m_size;
		return n;
	}

	private:
	RingBuffer(const RingBuffer &);
	RingBuffer &operator = (const RingBuffer &);

	char *m_buf;
	uint16_t m_size, m_st = 0, m_end = 0;
};
*/

#endif
