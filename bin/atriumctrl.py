#!/usr/bin/python

#
#  Copyright (C) 2018-2020, Thomas Maier-Komor
#  Atrium Firmware Package for ESP
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.
#  

import socket
import sys
import threading
import time

Port = 12719

def receiver():
	port = 0
	localip = socket.gethostbyname(socket.getfqdn())
	t = threading.currentThread()
	while getattr(t,"do_run",True):
		if (port != Port):
			inp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
			inp.settimeout(0.3)
			inp.bind(("", Port))
			port = Port
		try:
			msg,addr = inp.recvfrom(4096)
		except socket.timeout:
			continue
		if addr[0] == localip:
			continue
		host = socket.gethostbyaddr(addr[0])
		print "%s (%s):\n%s\n" % (host[0],addr[0],msg)


def term(x,r):
	r.do_run = False
	r.join()
	exit(x)

def to_int(x):
	try:
		return int(x)
	except:
		return 0


receivethr = threading.Thread(target=receiver)
receivethr.do_run = True
receivethr.start()
out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
bc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
bc.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

while True:
	try:
		sys.stdout.write(">> ")
		line = sys.stdin.readline()
		if line:
			line = line.rstrip()
		else:
			term(0,receivethr)
	except EOFError:
		term(0,receivethr)
		break
	args = line.split(' ',1);
	com = args[0]
	# commands without arg
	if com == 'exit':
		term(0,receivethr)
	elif com == 'help':
		print "@<node> <command>     : send command to <node>"
		print "send <command>        : broadcast command to all nodes"
		print "port <p>              : set UDP port (default: 12719)"
		print "hwcfg <node> [<file>] : send hardware config to node"
		print "exit                  : terminate"

	if (len(args) == 1):
		print "error: missing argument or unknown command"
		continue
	# commands with arguments
	arg = args[1]
        arg += '\n'
	if com == 'port':
		if to_int(arg) != 0:
			Port = to_int(arg)
			continue
		try:
			Port = socket.getservbyname(arg,'udp')
			print "service %s is on port %u" % (arg,Port)
		except socket.error as e:
			print "error resolving service %s: %s" % (arg,e)
	elif com[0] == '@':
		node = com[1:]
	elif com == "send":
		try:
			bc.sendto(arg,("<broadcast>",Port))
		except socket.gaierror as e:
			print "error sending: %s" % arg,e
		time.sleep(0.5)
		continue
        elif com == "hwcfg":
		args = arg.split(' ',2)
		node = args[0]
		arg = 'hwconf parsexxd\r\n'
                if len(args) == 1:
			print "please input config hex, end with empty line"
			while line != '\n':
				line = sys.stdin.readline()
				arg += line.rstrip()
		else:
			try:
				f = open(args[1].rstrip(),"r")
				buf = f.read()
				f.close()
			except IOError, e:
				print e
				continue
			arg += buf.encode('hex')
		arg += '\r\n\r\n'
#		print "%u bytes" % len(arg)
	else:
		print "error unkown command '%s'" % com
		continue
#	print "sending to %s:%u" % (node,Port)
#	print "====="
#	print "%s" % arg
#	print "====="
	try:
		addr = (node,Port)
		out.sendto(arg,addr)
	except socket.gaierror as e:
		print "error sending: %s" % arg,e
	time.sleep(0.5)

