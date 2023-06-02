#!/usr/bin/env python2
"""
#   CNC-remote-control
#   Admin tool
#
#   Copyright (C) 2011 Michael Buesch <m@bues.ch>
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License
#   version 2 as published by the Free Software Foundation.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
"""

import sys
import time
import getopt
from cnccontrol_driver import *


# Hardware constants
CPU_BOOT_OFFSET		= 0x7000
CPU_APP_SIZE		= CPU_BOOT_OFFSET
CPU_PAGE_SIZE		= 0x80
COPROC_BOOT_OFFSET	= 0x1800
COPROC_PAGE_SIZE	= 0x40
COPROC_APP_SIZE		= COPROC_BOOT_OFFSET


def arg2bool(arg):
	arg = arg.lower()
	if arg in ("yes", "true"):
		return True
	if arg in ("no", "false"):
		return False
	try:
		return bool(int(arg))
	except ValueError:
		pass
	return False

class IHEXParser(object):
	TYPE_DATA = 0
	TYPE_EOF  = 1
	TYPE_ESAR = 2
	TYPE_SSAR = 3
	TYPE_ELAR = 4
	TYPE_SLAR = 5

	def __init__(self, ihexfile, imagesize):
		image = [0xFF] * imagesize
		try:
			lines = file(ihexfile, "rb").readlines()
			hiAddr = 0
			for line in lines:
				line = line.strip()
				if not line:
					continue
				if len(line) < 11 or (len(line) - 1) % 2 != 0:
					raise CNCCException("Invalid ihex file format (length error)")
				if line[0] != ':':
					raise CNCCException("Invalid ihex file format (magic error)")
				count = int(line[1:3], 16)
				if len(line) != count * 2 + 11:
					raise CNCCException("Invalid ihex file format (count error)")
				addr = (int(line[3:5], 16) << 8) | int(line[5:7], 16)
				addr |= hiAddr << 16
				recordType = int(line[7:9], 16)
				checksum = 0
				for i in range(1, len(line), 2):
					b = int(line[i:i+2], 16)
					checksum = (checksum + b) & 0xFF
				if checksum != 0:
					raise CNCCException("Invalid ihex file format (checksum error)")

				if recordType == self.TYPE_EOF:
					break
				if recordType == self.TYPE_ELAR:
					if count != 2:
						raise CNCCException("Invalid ihex file format (inval ELAR)")
					hiAddr = (int(line[9:11], 16) << 8) | int(line[11:13], 16)
					continue
				if recordType == self.TYPE_DATA:
					if len(image) < addr + count:
						raise CNCCException("Ihex data outside of image bounds")
					for i in range(9, 9 + count * 2, 2):
						image[(i - 9) / 2 + addr] = int(line[i:i+2], 16)
					continue
				raise CNCCException("Invalid ihex file format (unsup type %d)" % recordType)
		except (ValueError) as e:
			raise CNCCException("Invalid ihex file format (digit format)")
		except (IOError) as e:
			raise CNCCException("Failed to read file %s: %s" % (ihexfile, str(e)))
		self.image = image

class Context(object):
	def __init__(self):
		self.cncc = None

	def getCNCC(self):
		if not self.cncc:
			self.cncc = CNCControl()
			if not self.cncc.probe():
				raise CNCCException("Did not find CNC "
					"Control device on USB bus")
		return self.cncc

def handle_cpu_context(context, arg):
	cncc = context.getCNCC()
	if cncc.deviceRunsBootloader():
		print("The CPU is running in BOOTLOADER code")
	else:
		print("The CPU is running in APPLICATION code")

def handle_verbose_debug(context, enable):
	enable = arg2bool(enable)
	cncc = context.getCNCC()
	devFlagsSet = 0
	if enable:
		print("Enabling verbose debug mode...")
		devFlagsSet = ControlMsgDevflags.DEVICE_FLG_VERBOSEDBG
	else:
		print("Disabling verbose debug mode...")
	msg = ControlMsgDevflags(ControlMsgDevflags.DEVICE_FLG_VERBOSEDBG,
				 devFlagsSet)
	reply = cncc.controlMsgSyncReply(msg)
	if not reply.isOK():
		raise CNCCException("Failed to set device flags: %s" % str(reply))
	print("Device flags:", reply)

def handle_enterboot(context, arg):
	print("Entering bootloader...")
	cncc = context.getCNCC()
	if not cncc.deviceRunsBootloader():
		# Enter CPU bootloader
		msg = ControlMsgEnterboot(ControlMsg.TARGET_CPU)
		cncc.controlMsg(msg)
		time.sleep(0.3)
		if not cncc.reconnect():
			raise CNCCException("Failed to enter CPU bootloader. "
				"The USB device did not reconnect.")
	# Ping the bootloader
	ping = ControlMsgPing(hdrFlags=ControlMsg.CONTROL_FLG_BOOTLOADER)
	reply = cncc.controlMsgSyncReply(ping)
	if not reply.isOK():
		raise CNCCException("Failed to ping the CPU bootloader")
	# Enter coprocessor bootloader
	msg = ControlMsgEnterboot(ControlMsg.TARGET_COPROC,
				  hdrFlags=ControlMsg.CONTROL_FLG_BOOTLOADER)
	reply = cncc.controlMsgSyncReply(msg, timeoutMs=1000)
	if not reply.isOK():
		raise CNCCException("Failed to enter coprocessor bootloader: %s" % str(reply))

def handle_exitboot(context, arg):
	print("Exiting bootloader...")
	cncc = context.getCNCC()
	if not cncc.deviceRunsBootloader():
		return
	# Exit coprocessor bootloader
	msg = ControlMsgExitboot(ControlMsg.TARGET_COPROC)
	reply = cncc.controlMsgSyncReply(msg, timeoutMs=1000)
	if not reply.isOK():
		print("Failed to exit coprocessor bootloader: %s" % str(reply))
	# Exit CPU bootloader
	msg = ControlMsgExitboot(ControlMsg.TARGET_CPU)
	cncc.controlMsg(msg)
	time.sleep(0.3)
	if not cncc.reconnect():
		raise CNCCException("Failed to exit CPU bootloader. "
			"The USB device did not reconnect.")

def __flashImage(context, ihexfile, offset, size, pageSize, targetMCU):
	cncc = context.getCNCC()
	p = IHEXParser(ihexfile, size)
	for pageAddr in range(offset, size, pageSize):
		page = p.image[pageAddr:pageAddr+min(pageSize, size - pageAddr)]
		for chunkOffset in range(0, len(page), ControlMsgBootWritebuf.DATA_MAX_BYTES):
			chunkSize = min(ControlMsgBootWritebuf.DATA_MAX_BYTES,
					len(page) - chunkOffset)
			chunk = page[chunkOffset:chunkOffset+chunkSize]
			msg = ControlMsgBootWritebuf(chunkOffset, chunk)
			reply = cncc.controlMsgSyncReply(msg)
			if not reply.isOK():
				raise CNCCException("Failed to write image data to "
					"flash buffer: %s" % str(reply))
		msg = ControlMsgBootFlashpg(pageAddr, targetMCU)
		reply = cncc.controlMsgSyncReply(msg, timeoutMs=2500)
		if not reply.isOK():
			raise CNCCException("Failed to flash page: %s" % str(reply))

def handle_flash_cpu(context, ihexfile):
	print("Flashing CPU image")
	__flashImage(context, ihexfile, 0, CPU_APP_SIZE, CPU_PAGE_SIZE,
		     ControlMsg.TARGET_CPU)

def handle_flash_coprocessor(context, ihexfile):
	print("Flashing coprocessor image")
	__flashImage(context, ihexfile, 0, COPROC_APP_SIZE, COPROC_PAGE_SIZE,
		     ControlMsg.TARGET_COPROC)

def usage():
	print("admin.py [OPTIONS]")
	print("")
	print(" -c|--cpu-context            Find out the CPU context (boot or app)")
	print("")
	print(" -V|--verbose-debug BOOL     Enable/disable verbose debugging messages.")
	print("")
	print(" -b|--enterboot              Enter the CPU and coproc bootloader")
	print(" -x|--exitboot               Exit the CPU and coproc bootloader")
	print(" -f|--flash-cpu IHEX         Flash an ihex file to the CPU")
	print(" -F|--flash-coproc IHEX      Flash an ihex file to the coprocessor")

def main():
	actions = []

	try:
		(opts, args) = getopt.getopt(sys.argv[1:],
			"hcV:bxf:F:",
			[ "help", "cpu-context", "verbose-debug=", "enterboot", "exitboot",
			  "flash-cpu=", "flash-coproc=", ])
	except getopt.GetoptError:
		usage()
		return 1
	for (o, v) in opts:
		if o in ("-h", "--help"):
			usage()
			return 0
		if o in ("-c", "--cpu-context"):
			actions.append( ["cpu-context", v] )
		if o in ("-V", "--verbose-debug"):
			actions.append( ["verbose-debug", v] )
		if o in ("-b", "--enterboot"):
			actions.append( ["enterboot", v] )
		if o in ("-x", "--exitboot"):
			actions.append( ["exitboot", v] )
		if o in ("-f", "--flash-cpu"):
			actions.append( ["flash-cpu", v] )
		if o in ("-F", "--flash-coproc"):
			actions.append( ["flash-coproc", v] )

	handlers = {
		"cpu-context"	: handle_cpu_context,
		"verbose-debug"	: handle_verbose_debug,
		"enterboot"	: handle_enterboot,
		"exitboot"	: handle_exitboot,
		"flash-cpu"	: handle_flash_cpu,
		"flash-coproc"	: handle_flash_coprocessor,
	}

	try:
		if not actions:
			print("No action specified")
			return 1
		context = Context()
		for action in actions:
			handler = handlers[action[0]]
			handler(context, action[1])
	except (CNCCException) as e:
		print("CNC Control exception: %s" % str(e))
		return 1
	return 0

if __name__ == "__main__":
	sys.exit(main())
