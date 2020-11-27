#!/usr/bin/python3

#IRUSB control

#(c) 2020 by Malte Marwedel

#This program is free software; you can redistribute it and/or modify it under
#the terms of the GNU General Public License as published by the Free Software
#Foundation; either version 2 of the License, or (at your option) any later
#version.
#This program is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE. See the GNU General Public License for more details.

#You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
# Street, Fifth Floor, Boston, MA 02110-1301, USA.

#version 0.9

def sendIr(dev, protocolStr, addressStr, commandStr, flagsStr='0'):
	protocol = int(protocolStr, 0)
	if not (0 <= protocol < (1<<8)):
		print('Error, protocol >' + protocolStr + '< out of range')
		return(False)
	address = int(addressStr, 0)
	if not (0 <= address < (1<<16)):
		print('Error, address >' + addressStr + '< out of range')
		return(False)
	command = int(commandStr, 0)
	if not (0 <= command < (1<<32)):
		print('Error, command >' + dommandStr + '< out of range')
		return(False)
	flags = int(flagsStr, 0)
	if not (0 <= flags < (1<<8)):
		print('Error, flags >' + flagsStr + '< out of range')
		return(False)
	irmpdata = bytearray(8)
	irmpdata[0] = protocol
	irmpdata[1] = address % 256
	irmpdata[2] = address // 256
	irmpdata[3] = command % 256
	irmpdata[4] = (command >> 8) % 256
	irmpdata[5] = (command >> 16) % 256
	irmpdata[6] = (command >> 24) % 256
	irmpdata[7] = flags
	dev.ctrl_transfer(bmRequestSend, 1, 0, 0, irmpdata)
	return True

#delay in [ms], maximum 2^16 - 1
def sendDelay(dev, delayStr):
	delay = int(delayStr, 0)
	if delay < 65536:
		toSend = bytearray(2)
		toSend[0] = delay % 256;
		toSend[1] = delay // 256;
		dev.ctrl_transfer(bmRequestSend, 2, 0, 0, toSend)
		return(True)
	else:
		print('Error, delay >' + delayStr + '< out of range')
		return(False)

def sendLed(dev, ledStateStr):
	if ledStateStr == 'off':
		wValue = 0
	elif ledStateStr == 'blink':
		wValue = 1
	elif ledStateStr == 'on':
		wValue = 2
	else:
		print('Error, led mode >' + ledStateStr + '< invalid')
		return(False)
	dev.ctrl_transfer(bmRequestSend, 0, wValue, 0, 0)
	return(True)

def sendReset(dev):
	dev.ctrl_transfer(bmRequestSend, 4, 0, 0, 0)
	return(True)

def recIr(dev):
	lastRx = dev.ctrl_transfer(bmRequestRecv, 3, 0, 0, 8)
	protocol = lastRx[0]
	address = (lastRx[2] << 8) | lastRx[1]
	command = (lastRx[6] << 24) | (lastRx[5] << 16) | (lastRx[4] << 8) | lastRx[3]
	flags = lastRx[7]
	if protocol > 0:
		print('Protocol:' + str(protocol) + ' Address:' + hex(address) + ' Command:' + hex(command) + ' Flags:' + hex(flags))


import sys
from os.path import expanduser

parameters = len(sys.argv)

if parameters == 2:
	if (sys.argv[1] == '--help'):
		print('IRUSB control')
		print('(c) 2020 by Malte Marwedel. Licensed under GPL v2 or later')
		print('Usage:')
		print('--help:        Prints this screen')
		print('--version:     Prints the version')
		print('For IR commands, LED commands and delay commands, there is a queue on the other side with up to 16 entries')
		print('<Protocol Number> <Address> <Command>: Sends an IR command with flags 0')
		print('<Protocol Number> <Address> <Command> <flags>: Sends an IR command')
		print('  Numbers can be in decimal or hexadecimal.')
		print('  Example: 2 0xff00 2')
		print('led on:        Enables the red LED')
		print('led blink:     Lets the red LED flash')
		print('led off:       Disables the red LED')
		print('delay <value>: Adds a delay of [ms] to the command queue. Maximum is 65565ms.')
		print('irusbreset:    Resets the device. Ignoring the command queue.')
		print('Call without parameter: Requests received ir commands from IRUSB every 250ms. Terminate with CTRL+C.')
		print('')
		print('You can add an alias file in your ~ directory named .irusbAliases')
		print('Write YourCoolCommand <Protocol Number> <Address> <Command> <flags> in one line.')
		print('  Example: LightDarker 2 0xff00 2 0')
		print('           LightBrighter 2 0xff00 0 0')
		print('Then you can simply call YourCoolCommand as parameter.')
		print('The delay and LED commands are supported too.')
		print('If you define an alias more than once, both commands are executed. By this way, complex scripts can be defined.')
		print('The only way to abort commands waiting in the queue is to reset the device.')
		print('')
		print('Return codes:')
		print('0: Command sent')
		print('1: Invalid command')
		print('2: python USB library not installed')
		print('3: USB device not found')
		sys.exit(0)
	if (sys.argv[1] == '--version'):
		print('Version 0.9')
		sys.exit(0)

try:
	import usb.core
	import usb.util

except:
	print('Ouch, can not import usb.core and usb.util')
	print('Try: pip3 install pyusb')
	print('Or as root: apt install python3-usb')
	sys.exit(2)

import time

# find our device
dev = usb.core.find(idVendor=0x1209, idProduct=0x0001)

# was it found?
if dev is None:
	print('Error, device not found')
	sys.exit(3)

# set the active configuration. With no arguments, the first
# configuration will be the active one
dev.set_configuration()

#For ctrl_transfer: bmRequestType, bmRequest, wValue and wIndex, data array or its length

#IRUSB control
bmRequestSend = 0x40
bmRequestRecv = 0xC0

success = False

if parameters == 1:
	print('Requesting IR commands until program termination')
	while True:
		recIr(dev)
		time.sleep(0.25)
elif parameters == 2:
	if sys.argv[1] == 'irusbreset':
		success = sendReset(dev)
	else:
		aliasFile = open(expanduser("~") + '/.irusbAliases', 'r')
		aliases = aliasFile.readlines()
		aliasFile.close()
		tokenFound = False
		for a in aliases:
			tokens = a.split()
			numTokens = len(tokens)
			if numTokens >= 3:
				if tokens[0] == sys.argv[1]:
					if numTokens == 5:
						success = sendIr(dev, tokens[1], tokens[2], tokens[3], tokens[4])
						tokenFound = True
					elif numTokens == 3:
						if tokens[1] == 'delay':
							success = sendDelay(dev, tokens[2])
							tokenFound = True
						elif tokens[1] == 'led':
							success = sendLed(dev, tokens[2])
							tokenFound = True
						else:
							print('Error, unknown command >' + tokens[1] + '<')
					else:
						print('Error, unsupported number of parameters')
		if tokenFound == False:
			print('Error, command not supported. Defined aliases:')
			acceptedAliases = []
			for a in aliases:
				tokens = a.split()
				if len(tokens) >= 3:
					if tokens[0] not in acceptedAliases:
						acceptedAliases.append(tokens[0])
			for a in acceptedAliases:
				print('  ' + a)
elif parameters == 3:
	if sys.argv[1] == 'led':
		success = sendLed(dev, sys.argv[2])
	elif sys.argv[1] == 'delay':
		success = sendDelay(dev, sys.argv[2])
	else:
		print('Error, command not supported')
elif parameters == 4:
	success = sendIr(dev, sys.argv[1], sys.argv[2], sys.argv[3])
elif parameters == 5:
	success = sendIr(dev, sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])
else:
	print('Error, invalid number of arguments. Try --help')

if success == False:
	sys.exit(1)
