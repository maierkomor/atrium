/*
 *  Copyright (C) 2017-2021, Thomas Maier-Komor
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

#ifdef CONFIG_TELNET

#define TELNET_PORT 23

#include "binformats.h"
#include "globals.h"
#include "inetd.h"
#include "log.h"
#include "netsvc.h"
#include "settings.h"
#include "shell.h"
#include "support.h"
#include "terminal.h"

#include <esp_err.h>
#include <lwip/tcp.h>

#include <string.h>
#include <strings.h>
#include <lwip/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef write
#undef write
#endif

#ifdef read
#undef read
#endif

/************* RFC854 - telnet: *************************************
      NAME               CODE              MEANING

      SE                  240    End of subnegotiation parameters.
      NOP                 241    No operation.
      Data Mark           242    The data stream portion of a Synch.
                                 This should always be accompanied
                                 by a TCP Urgent notification.
      Break               243    NVT character BRK.
      Interrupt Process   244    The function IP.
      Abort output        245    The function AO.
      Are You There       246    The function AYT.
      Erase character     247    The function EC.
      Erase Line          248    The function EL.
      Go ahead            249    The GA signal.
      SB                  250    Indicates that what follows is
                                 subnegotiation of the indicated
                                 option.
      WILL (option code)  251    Indicates the desire to begin
                                 performing, or confirmation that
                                 you are now performing, the
                                 indicated option.
      WON'T (option code) 252    Indicates the refusal to perform,
                                 or continue performing, the
                                 indicated option.
      DO (option code)    253    Indicates the request that the
                                 other party perform, or
                                 confirmation that you are expecting
                                 the other party to perform, the
                                 indicated option.
      DON'T (option code) 254    Indicates the demand that the
                                 other party stop performing,
                                 or confirmation that you are no
                                 longer expecting the other party
                                 to perform, the indicated option.
      IAC                 255    Data Byte 255.
*********************************************************************/

#define RFC854_SE	240
#define RFC854_NOP	241
#define RFC854_DM	252
#define RFC854_BREAK	243
#define RFC854_INTR	244
#define RFC854_ABORT	245
#define RFC854_AYT	246
#define RFC854_EC	247
#define RFC854_EL	248
#define RFC854_GA	249
#define RFC854_SB	250
#define RFC854_WILL	251
#define RFC854_WONT	252
#define RFC854_DO	253
#define RFC854_DONT	254
#define RFC854_IAC	255

// subnegotiation actions
#define SN_IS		0
#define SN_SEND		1
#define SN_INFO		2

#define ESC		0x1b

using namespace std;

static const char TAG[] = "telnetd";
static const char ConSetup[] = {
	RFC854_IAC,RFC854_WILL,1,	// echo
	RFC854_IAC,RFC854_DONT,1,	// echo
	RFC854_IAC,RFC854_DO,3,		// suppress go ahead
};
static const char NOP[] = {RFC854_IAC,RFC854_NOP};

class Telnet : public Terminal
{
	public:
	explicit Telnet(int c)
	: Terminal(true)
	, m_con(c)
	, m_lastsent(uptime())
	, m_doecho(false)
	, m_newline(false)
	, m_state(XMDATA)
	, m_cin(m_cmd)
	, m_error(0)
	{
		write(ConSetup,sizeof(ConSetup));
	}

	~Telnet()
	{
		close(m_con);
	}

	int process_input(char in);

	// inherited from Terminal
	int write(const char *, size_t);
	int read(char *, size_t n, bool = true);
	int get_ch(char *);

	private:
	Telnet(const Telnet &);
	Telnet& operator = (const Telnet &);

	int m_con;
	uint32_t m_lastsent;
	bool m_doecho, m_newline;
	typedef enum { XMDATA=0, XMIAC, XMDO, XMDONT, XMWILL, XMWONT, XMSN } xfer_state_t;
	xfer_state_t m_state;
	char m_cmd[64], *m_cin;
	const char *m_error;
};


int Telnet::write(const char *b, size_t l)
{
	int n = send(m_con,b,l,0);
	if (n < 0)
		m_error = strneterr(m_con);
	m_lastsent = uptime();
	return n;
}


int Telnet::read(char *buf, size_t s, bool block)
{
	int n = recv(m_con,buf,s, block ? 0 : MSG_DONTWAIT);
	if (n >= 0)
		return n;
	m_error = strneterr(m_con);
	return -1;
}



int Telnet::get_ch(char *oc)
{
	//log_info(TAG,"char %d",c);
	while (1) {
		int n = recv(m_con,oc,1,0);
		char c = *oc;
		if (n < 0)
			return -1;
		switch (m_state) {
		case XMDATA:
			if (c == RFC854_IAC) {
				m_state = XMIAC;
			} else if (c == 0) {
			} else if (c == 4) {	// CTRL-d logout
				log_info(TAG,"ctrl-d logout");
				char resp[] = {RFC854_IAC,RFC854_WILL,18};
				write(resp,sizeof(resp));
				return -1;
			} else if (c == 9) {	// horizontal tab
			} else if (c == 11) {	// vertical tab
			} else {
				//log_dbug(TAG,"input 0x%x",(unsigned)c);
				return 1;
			}
			break;
		case XMIAC:
			if (c == RFC854_DO)
				m_state = XMDO;
			else if (c == RFC854_DONT)
				m_state = XMDONT;
			else if (c == RFC854_WILL)
				m_state = XMWILL;
			else if (c == RFC854_WONT)
				m_state = XMWONT;
			else if (c == RFC854_SB)
				m_state = XMSN;
			else if (c == RFC854_NOP)
				;
			else
				log_info(TAG,"unexpected code %d in IAC",c);
			break;
		case XMDO:
			if (c == 1) {	// ECHO
				m_doecho = true;
				char resp[] = {RFC854_IAC,RFC854_WILL,1};
				write(resp,sizeof(resp));
				//log_dbug(TAG,"received DO ECHO");
			} else if (c == 3) {	// subpress go-ahead
				char resp[] = {RFC854_IAC,RFC854_WILL,3};
				write(resp,sizeof(resp));
				//log_dbug(TAG,"received DO SUPPRESS GO-AHEAD");
			} else if (c == 18) {	// logout
				char resp[] = {RFC854_IAC,RFC854_WILL,18};
				write(resp,sizeof(resp));
				return -1;
			} else {
				// default: we don't support it so we answer with WONT
				char resp[] = {RFC854_IAC,RFC854_WONT,c};
				write(resp,sizeof(resp));
				log_info(TAG,"received unknown DO %d, answering WONT %d",c,c);
			}
			m_state = XMDATA;
			break;
		case XMDONT:
			if (c == 1) {	// ECHO
				m_doecho = false;
				char resp[] = {RFC854_IAC,RFC854_WONT,1};
				write(resp,sizeof(resp));
				//log_dbug(TAG,"received DONT ECHO");
			} else {
				// default: we don't support it so we answer with WONT
				char resp[] = {RFC854_IAC,RFC854_WONT,c};
				write(resp,sizeof(resp));
				log_info(TAG,"received unknown DONT %d",c);
			}
			m_state = XMDATA;
			break;
		case XMWILL:
			//log_dbug(TAG,"received WILL %d",c);
			m_state = XMDATA;
			break;
		case XMWONT:
			//log_dbug(TAG,"received WONT %d",c);
			m_state = XMDATA;
			break;
		case XMSN:
			if (c == RFC854_SE) {
				log_info(TAG,"ending subnegotiation");
				m_state = XMDATA;
				break;
			} else {
				log_info(TAG,"subnegotiation %d",c);
			}
			break;
		default:
			log_error(TAG,"BUG invalid state");
			return -1;
		}
		if (uptime() - m_lastsent > 1000) {
			write(NOP,sizeof(NOP));
		}
	}
}


static void telnet_session(void *arg)
{
	int con = (int)arg;
	log_info(TAG,"starting session");
	Telnet session(con);
	if (!Config.has_pass_hash())
		session.setPrivLevel(1);
	shell(session);
	close(con);
	log_info(TAG,"session terminated");
	vTaskDelete(0);
}


#ifdef CONFIG_IDF_TARGET_ESP32
#define stack_size 4096
#else
#define stack_size 2560
#endif

int telnet_setup()
{
	return listen_port(TELNET_PORT,m_tcp,telnet_session,"telnetd","_telnet",8,stack_size);
}


#endif	// CONFIG_TELNET
