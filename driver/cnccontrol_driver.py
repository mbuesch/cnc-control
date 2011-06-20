#!/usr/bin/env python
"""
#
# CNC-control - Host driver
#
# Copyright (C) 2011 Michael Buesch <m@bues.ch>
#
"""

import sys
import usb
import time
from datetime import datetime, timedelta


IDVENDOR	= 0x6666
IDPRODUCT	= 0xC8CC

EP_IN		= 0x82
EP_OUT		= 0x02
EP_IRQ		= 0x81

ALL_AXES	= "xyza"
AXIS2NUMBER	= dict(map(lambda x: (x[1], x[0]), enumerate(ALL_AXES)))
NUMBER2AXIS	= dict(enumerate(ALL_AXES))


def equal(a, b, threshold=0.00001):
	if type(a) == float or type(b) == float:
		return abs(float(a) - float(b)) <= threshold
	return a == b

def twos32(nr): # Convert 32bit two's complement value to python int.
	if nr & 0x80000000:
		return (-(~nr & 0xFFFFFFFF)) - 1
	return nr

def crc8(crc, data):
	crc = crc ^ data
	for i in range(0, 8):
		if crc & 0x01:
			crc = (crc >> 1) ^ 0x8C
		else:
			crc >>= 1
	return crc & 0xFF

def crc8Buf(crc, iterable):
	for b in iterable:
		crc = crc8(crc, b)
	return crc

class CNCCException(Exception): pass

class FixPt:
	FIXPT_FRAC_BITS		= 16

	def __init__(self, val):
		if isinstance(val, FixPt):
			self.raw = val.raw
			self.floatval = val.floatval
		elif type(val) == float:
			raw = (int(val * float(1 << self.FIXPT_FRAC_BITS)) + 1) & 0xFFFFFFFF
			self.raw = (raw & 0xFF, (raw >> 8) & 0xFF,
				    (raw >> 16) & 0xFF, (raw >> 24) & 0xFF)
			self.floatval = val
		else: # 32bit LE two's complement
			self.raw = tuple(val)
			raw = twos32(val[0] | (val[1] << 8) |\
				     (val[2] << 16) | (val[3] << 24))
			self.floatval = round(float(raw) / float(1 << self.FIXPT_FRAC_BITS), 4)

	def __repr__(self):
		return "0x%02X%02X%02X%02X -> %f" %\
			(self.raw[3], self.raw[2], self.raw[1], self.raw[0],
			self.floatval)

	def getRaw(self):
		return self.raw

	def __eq__(self, other):
		return self.raw == other.raw

	def __ne__(self, other):
		return self.raw != other.raw

class ControlMsg:
	# IDs
	CONTROL_PING			= 0
	CONTROL_RESET			= 1
	CONTROL_DEVFLAGS		= 2
	CONTROL_AXISUPDATE		= 3
	CONTROL_SPINDLEUPDATE		= 4
	CONTROL_FOUPDATE		= 5
	CONTROL_AXISENABLE		= 6
	CONTROL_ENTERBOOT		= 0xA0
	CONTROL_EXITBOOT		= 0xA1
	CONTROL_BOOT_WRITEBUF		= 0xA2
	CONTROL_BOOT_FLASHPG		= 0xA3

	# Flags
	CONTROL_FLG_BOOTLOADER		= 0x80

	# Targets
	TARGET_CPU			= 0
	TARGET_COPROC			= 1

	def __init__(self, id, flags):
		self.id = id
		self.flags = flags

	def getRaw(self):
		return [self.id & 0xFF, self.flags & 0xFF]

class ControlMsgPing(ControlMsg):
	def __init__(self, hdrFlags=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_PING, hdrFlags)

class ControlMsgReset(ControlMsg):
	def __init__(self, hdrFlags=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_RESET, hdrFlags)

class ControlMsgDevflags(ControlMsg):
	DEVICE_FLG_NODEBUG	= (1 << 0)
	DEVICE_FLG_VERBOSEDBG	= (1 << 1)
	DEVICE_FLG_ON		= (1 << 2)
	DEVICE_FLG_TWOHANDEN	= (1 << 3)

	def __init__(self, devFlagsMask, devFlagsSet, hdrFlags=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_DEVFLAGS, hdrFlags)
		self.devFlagsMask = devFlagsMask
		self.devFlagsSet = devFlagsSet

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.extend( [self.devFlagsMask & 0xFF, (self.devFlagsMask >> 8) & 0xFF] )
		raw.extend( [self.devFlagsSet & 0xFF, (self.devFlagsSet >> 8) & 0xFF] )
		return raw

class ControlMsgAxisupdate(ControlMsg):
	def __init__(self, pos, axis, hdrFlags=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_AXISUPDATE, hdrFlags)
		self.pos = FixPt(pos)
		self.axis = AXIS2NUMBER[axis]

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.extend(self.pos.getRaw())
		raw.append(self.axis & 0xFF)
		return raw

class ControlMsgSpindleupdate(ControlMsg):
	SPINDLE_OFF		= 0
	SPINDLE_CW		= 1
	SPINDLE_CCW		= 2

	def __init__(self, state, hdrFlags=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_SPINDLEUPDATE, hdrFlags)
		self.state = state

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.append(self.state & 0xFF)
		return raw

class ControlMsgFoupdate(ControlMsg):
	def __init__(self, percent, hdrFlags=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_FOUPDATE, hdrFlags)
		self.percent = percent

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.append(self.percent & 0xFF)
		return raw

class ControlMsgAxisenable(ControlMsg):
	def __init__(self, mask, hdrFlags=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_AXISENABLE, hdrFlags)
		self.mask = mask

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.append(self.mask & 0xFF)
		return raw

class ControlMsgEnterboot(ControlMsg):
	ENTERBOOT_MAGIC0		= 0xB0
	ENTERBOOT_MAGIC1		= 0x07

	def __init__(self, target, hdrFlags=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_ENTERBOOT, hdrFlags)
		self.magic = (
			ControlMsgEnterboot.ENTERBOOT_MAGIC0,
			ControlMsgEnterboot.ENTERBOOT_MAGIC1,
		)
		self.target = target

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.extend(self.magic)
		raw.append(self.target & 0xFF)
		return raw

class ControlMsgExitboot(ControlMsg):
	def __init__(self, target, hdrFlags=ControlMsg.CONTROL_FLG_BOOTLOADER):
		ControlMsg.__init__(self, ControlMsg.CONTROL_EXITBOOT, hdrFlags)
		self.target = target

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.append(self.target & 0xFF)
		return raw

class ControlMsgBootWritebuf(ControlMsg):
	DATA_MAX_BYTES = 32

	def __init__(self, offset, data,
		     hdrFlags=ControlMsg.CONTROL_FLG_BOOTLOADER):
		ControlMsg.__init__(self, ControlMsg.CONTROL_BOOT_WRITEBUF, hdrFlags)
		self.offset = offset
		self.size = len(data)
		self.crc = crc8Buf(0, data) ^ 0xFF
		nrPadding = ControlMsgBootWritebuf.DATA_MAX_BYTES - len(data)
		if nrPadding < 0:
			CNCCException("ControlMsg-BootWritebuf: invalid data length %d" %\
				(len(data)))
		self.data = data
		self.data.extend([0] * nrPadding)

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.extend( [self.offset & 0xFF, (self.offset >> 8) & 0xFF] )
		raw.append(self.size & 0xFF)
		raw.append(self.crc & 0xFF)
		raw.extend(self.data)
		return raw

class ControlMsgBootFlashpg(ControlMsg):
	def __init__(self, address, target,
		     hdrFlags=ControlMsg.CONTROL_FLG_BOOTLOADER):
		ControlMsg.__init__(self, ControlMsg.CONTROL_BOOT_FLASHPG, hdrFlags)
		self.address = address
		self.target = target

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.extend( [self.address & 0xFF, (self.address >> 8) & 0xFF] )
		raw.append(self.target & 0xFF)
		return raw

class ControlReply:
	MAX_SIZE		= 37

	# IDs
	REPLY_OK		= 0
	REPLY_ERROR		= 1
	REPLY_VAL16		= 2

	def __init__(self, id, flags):
		self.id = id
		self.flags = flags

	@staticmethod
	def parseRaw(raw):
		try:
			id = raw[0]
			flags = raw[1]
			if id == ControlReply.REPLY_OK:
				return ControlReplyOk()
			elif id == ControlReply.REPLY_ERROR:
				return ControlReplyError(raw[2])
			elif id == ControlReply.REPLY_VAL16:
				return ControlReplyVal16(raw[2] | (raw[3] << 8))
			else:
				CNCCException("Unknown ControlReply ID: %d" % id)
		except (IndexError, KeyError):
			raise CNCCException("Failed to parse ControlReply (%d bytes)" % len(raw))

	def __repr__(self):
		return "Unknown reply code"

	def isOK(self):
		return False

class ControlReplyOk(ControlReply):
	def __init__(self, hdrFlags=0):
		ControlReply.__init__(self, ControlReply.REPLY_OK, hdrFlags)

	def __repr__(self):
		return "Ok"

	def isOK(self):
		return True

class ControlReplyError(ControlReply):
	# Error codes
	CTLERR_UNDEFINED	= 0
	CTLERR_COMMAND		= 1
	CTLERR_SIZE		= 2
	CTLERR_BUSY		= 3
	CTLERR_PERMISSION	= 4
	CTLERR_INVAL		= 5
	CTLERR_CONTEXT		= 6
	CTLERR_CHECKSUM		= 7
	CTLERR_CMDFAIL		= 8

	def __init__(self, code, hdrFlags=0):
		ControlReply.__init__(self, ControlReply.REPLY_ERROR, hdrFlags)
		self.code = code

	def __repr__(self):
		code2text = {
			self.CTLERR_UNDEFINED	: "Undefined error",
			self.CTLERR_COMMAND	: "Command not implemented",
			self.CTLERR_SIZE	: "Payload size error",
			self.CTLERR_BUSY	: "Device is busy",
			self.CTLERR_PERMISSION	: "Permission denied",
			self.CTLERR_INVAL	: "Invalid parameters",
			self.CTLERR_CONTEXT	: "Not supported in this context",
			self.CTLERR_CHECKSUM	: "Data checksum error",
			self.CTLERR_CMDFAIL	: "Command failed",
		}
		try:
			return code2text[self.code]
		except KeyError:
			return "Unknown error"

class ControlReplyVal16(ControlReply):
	def __init__(self, value, hdrFlags=0):
		ControlReply.__init__(self, ControlReply.REPLY_VAL16, hdrFlags)
		self.value = value

	def __repr__(self):
		return "0x%04X" % self.value

	def isOK(self):
		return True

class ControlIrq:
	MAX_SIZE		= 12

	# IDs
	IRQ_JOG			= 0
	IRQ_JOG_KEEPALIFE	= 1
	IRQ_SPINDLE		= 2
	IRQ_FEEDOVERRIDE	= 3
	IRQ_DEVFLAGS		= 4
	IRQ_HALT		= 5

	# Flags
	IRQ_FLG_TXQOVR		= (1 << 0)
	IRQ_FLG_PRIO		= (1 << 1)
	IRQ_FLG_DROPPABLE	= (1 << 2)

	def __init__(self, id, flags):
		self.id = id
		self.flags = flags

	@staticmethod
	def parseRaw(raw):
		try:
			id = raw[0]
			flags = raw[1]
			if id == ControlIrq.IRQ_JOG:
				return ControlIrqJog(raw[2:6], raw[6:10], raw[10], raw[11])
			elif id == ControlIrq.IRQ_JOG_KEEPALIFE:
				return ControlIrqJogKeepalife()
			elif id == ControlIrq.IRQ_SPINDLE:
				return ControlIrqSpindle(raw[2])
			elif id == ControlIrq.IRQ_FEEDOVERRIDE:
				return ControlIrqFeedoverride(raw[2])
			elif id == ControlIrq.IRQ_DEVFLAGS:
				return ControlIrqDevflags(raw[2:4])
			elif id == ControlIrq.IRQ_HALT:
				return ControlIrqHalt()
			else:
				raise CNCCException("Unknown ControlIrq ID: %d" % id)
		except (IndexError, KeyError):
			raise CNCCException("Failed to parse ControlIrq (%d bytes)" % len(raw))

	def __repr__(self):
		return "Unknown interrupt"

class ControlIrqJog(ControlIrq):
	IRQ_JOG_CONTINUOUS	= (1 << 0)
	IRQ_JOG_RAPID		= (1 << 1)

	def __init__(self, increment, velocity, axis, flags, hdrFlags=0):
		ControlIrq.__init__(self, ControlIrq.IRQ_JOG, hdrFlags)
		self.increment = FixPt(increment)
		self.velocity = FixPt(velocity)
		self.axis = NUMBER2AXIS[axis]
		self.jogFlags = flags

	def __repr__(self):
		return "JOG interrupt: %s, %s, %s, 0x%X" %\
			(str(self.increment), str(self.velocity), self.axis, self.jogFlags)

class ControlIrqJogKeepalife(ControlIrq):
	def __init__(self, hdrFlags=0):
		ControlIrq.__init__(self, ControlIrq.IRQ_JOG_KEEPALIFE, hdrFlags)

	def __repr__(self):
		return "JOG KEEPALIFE interrupt"

class ControlIrqSpindle(ControlIrq):
	def __init__(self, state, hdrFlags=0):
		ControlIrq.__init__(self, ControlIrq.IRQ_SPINDLE, hdrFlags)
		self.state = state

	def __repr__(self):
		return "SPINDLE interrupt: %d" % (self.state)

class ControlIrqFeedoverride(ControlIrq):
	def __init__(self, state, hdrFlags=0):
		ControlIrq.__init__(self, ControlIrq.IRQ_FEEDOVERRIDE, hdrFlags)
		self.state = state

	def __repr__(self):
		return "FEEDOVERRIDE interrupt: %d" % (self.state)

class ControlIrqDevflags(ControlIrq):
	def __init__(self, devFlags, hdrFlags=0):
		ControlIrq.__init__(self, ControlIrq.IRQ_DEVFLAGS, hdrFlags)
		self.devFlags = devFlags[0] | (devFlags[1] << 8)

	def __repr__(self):
		return "DEVFLAGS interrupt: %04X" % (self.devFlags)

class ControlIrqHalt(ControlIrq):
	def __init__(self, hdrFlags=0):
		ControlIrq.__init__(self, ControlIrq.IRQ_HALT, hdrFlags)

	def __repr__(self):
		return "HALT interrupt"

class JogState:
	KEEPALIFE_TIMEOUT = 0.3

	def __init__(self):
		self.reset()

	def get(self):
		if datetime.now() > self.__timeout:
			return (FixPt(0.0), False, FixPt(0.0))
		return (self.__direction,
			self.__incremental,
			self.__velocity)

	def set(self, direction, incremental, velocity):
		self.__direction, self.__incremental, self.__velocity =\
			direction, incremental, velocity
		self.keepAlife()

	def reset(self):
		self.set(FixPt(0.0), False, FixPt(0.0))

	def keepAlife(self):
		self.__timeout = datetime.now() +\
			timedelta(seconds=self.KEEPALIFE_TIMEOUT)

class CNCControl:
	def __init__(self, verbose=False):
		self.deviceAvailable = False
		self.verbose = verbose

	@staticmethod
	def __haveEndpoint(interface, epAddress):
		found = filter(lambda ep: ep.address == epAddress,
			       interface.endpoints)
		return bool(found)

	def __epClearHalt(self, interface, epAddress):
		if self.__haveEndpoint(interface, epAddress):
			self.usbh.clearHalt(epAddress)

	def deviceRunsBootloader(self):
		# Check if we're in application code
		ping = ControlMsgPing(hdrFlags=0)
		reply = self.controlMsgSyncReply(ping)
		if reply.isOK():
			return False
		if reply.id == ControlReply.REPLY_ERROR and\
		   reply.code != ControlReplyError.CTLERR_CONTEXT:
			raise CNCCException("Failed to ping the application: %s" % str(reply))
		# Check if we're in the bootloader code
		ping = ControlMsgPing(hdrFlags=ControlMsg.CONTROL_FLG_BOOTLOADER)
		reply = self.controlMsgSyncReply(ping)
		if reply.isOK():
			return True
		if reply.id == ControlReply.REPLY_ERROR and\
		   reply.code != ControlReplyError.CTLERR_CONTEXT:
			raise CNCCException("Failed to ping the bootloader: %s" % str(reply))
		raise CNCCException("Unknown PING error")

	def probe(self, sendInit=True):
		if self.deviceAvailable:
			return True
		self.__initializeData()
		try:
			self.usbdev = self.__findDevice(IDVENDOR, IDPRODUCT)
			if not self.usbdev:
				return False

			self.usbh = self.usbdev.open()
			self.usbh.reset()
			time.sleep(0.1)
			config = self.usbdev.configurations[0]
			interface = config.interfaces[0][0]

			self.usbh.setConfiguration(config)
			self.usbh.claimInterface(interface)
			self.usbh.setAltInterface(interface)
			self.__epClearHalt(interface, EP_IN)
			self.__epClearHalt(interface, EP_OUT)
			self.__epClearHalt(interface, EP_IRQ)
		except (usb.USBError), e:
			self.__usbError(e)
		self.__devicePlug()

		if sendInit and not self.deviceRunsBootloader():
			reply = self.controlMsgSyncReply(ControlMsgReset())
			if not reply.isOK():
				raise CNCCException("Failed to reset the device state")
			reply = self.controlMsgSyncReply(ControlMsgDevflags(0, 0))
			if not reply.isOK():
				raise CNCCException("Failed to read device flags")
			devFlags = reply.value
			if devFlags & ControlMsgDevflags.DEVICE_FLG_ON:
				self.deviceIsOn = True
		return True

	def reconnect(self, timeout=5000):
		if not self.deviceAvailable:
			return False
		self.__deviceUnplug()
		while timeout > 0:
			try:
				if self.probe():
					return True
			except (CNCCException), e:
				pass
			timeout -= 50
			time.sleep(0.05)
		return False

	def __initializeData(self):
		self.deviceIsOn = False
		self.motionHaltRequest = False
		self.axisPositions = { }
		self.jogStates = { }
		for ax in ALL_AXES:
			self.axisPositions[ax] = FixPt(0.0)
			self.jogStates[ax] = JogState()
		self.foState = 0
		self.spindleCommand = 0
		self.spindleState = 0
		self.feedOverridePercent = 0

	@staticmethod
	def __findDevice(idVendor, idProduct):
		for bus in usb.busses():
			for device in bus.devices:
				if device.idVendor == idVendor and\
				   device.idProduct == idProduct:
					return device
		return None

	def __devicePlug(self):
		self.deviceAvailable = True
		if self.verbose:
			print "CNC Control device connected."

	def __deviceUnplug(self):
		if self.deviceAvailable:
			self.deviceAvailable = False

	def __usbError(self, usbException):
		self.__deviceUnplug()
		raise CNCCException("USB error: " + str(usbException))

	def eventWait(self, timeout=50):
		if not self.deviceAvailable:
			return False
		try:
			data = self.usbh.interruptRead(EP_IRQ, ControlIrq.MAX_SIZE,
						       timeout)
		except (usb.USBError), e:
			if not e.errno:
				return True
			self.__usbError(e)
		irq = ControlIrq.parseRaw(data)
		if irq.flags & ControlIrq.IRQ_FLG_TXQOVR:
			print "CNC Control WARNING: Interrupt queue overflow detected"
		print irq
		if irq.id == ControlIrq.IRQ_JOG:
			cont = bool(irq.jogFlags & ControlIrqJog.IRQ_JOG_CONTINUOUS)
			velocity = irq.velocity
			if irq.jogFlags & ControlIrqJog.IRQ_JOG_RAPID:
				velocity = FixPt(-1.0)
			state = self.jogStates[irq.axis]
			state.set(direction = irq.increment,
				  incremental = not cont,
				  velocity = velocity)
		elif irq.id == ControlIrq.IRQ_JOG_KEEPALIFE:
			map(lambda ax: self.jogStates[ax].keepAlife(),
			    self.jogStates)
		elif irq.id == ControlIrq.IRQ_SPINDLE:
			irq2direction = {
				ControlMsgSpindleupdate.SPINDLE_OFF:	0,
				ControlMsgSpindleupdate.SPINDLE_CW:	1,
				ControlMsgSpindleupdate.SPINDLE_CCW:	-1,
			}
			self.spindleCommand = irq2direction[irq.state]
		elif irq.id == ControlIrq.IRQ_FEEDOVERRIDE:
			self.foState = irq.state
		elif irq.id == ControlIrq.IRQ_DEVFLAGS:
			if irq.devFlags & ControlMsgDevflags.DEVICE_FLG_ON:
				print "CNC Control was turned ON"
				self.deviceIsOn = True
			else:
				print "CNC Control was turned OFF"
				self.deviceIsOn = False
		elif irq.id == ControlIrq.IRQ_HALT:
			self.motionHaltRequest = True
			for jogState in self.jogStates.values():
				jogState.reset()
			self.spindleCommand = 0
		else:
			print "Unhandled IRQ:", irq
		return True

	def controlMsg(self, msg, timeout=300):
		try:
			rawData = msg.getRaw()
			size = self.usbh.bulkWrite(EP_OUT, rawData, timeout)
			if len(rawData) != size:
				raise CNCCException("Only wrote %d bytes of %d bytes "
					"bulk write" % (size, len(rawData)))
		except (usb.USBError), e:
			self.__usbError(e)

	def controlReply(self, timeout=300):
		try:
			data = self.usbh.bulkRead(EP_IN, ControlReply.MAX_SIZE, timeout)
		except (usb.USBError), e:
			self.__usbError(e)
		return ControlReply.parseRaw(data)

	def controlMsgSyncReply(self, msg, timeout=300):
		self.controlMsg(msg, timeout)
		return self.controlReply(timeout)

	def setTwohandEnabled(self, enable):
		if not self.deviceAvailable:
			return
		flg = 0
		if enable:
			flg = ControlMsgDevflags.DEVICE_FLG_TWOHANDEN
		msg = ControlMsgDevflags(ControlMsgDevflags.DEVICE_FLG_TWOHANDEN, flg)
		reply = self.controlMsgSyncReply(msg)
		if not reply.isOK():
			raise CNCCException("Failed to set Twohand flag")

	def setDebugging(self, debug):
		# 0 => disabled, 1 => enabled, 2 => verbose
		if not self.deviceAvailable:
			return
		flg = ControlMsgDevflags.DEVICE_FLG_NODEBUG
		if debug >= 1:
			flg &= ~ControlMsgDevflags.DEVICE_FLG_NODEBUG
		if debug >= 2:
			flg |= ControlMsgDevflags.DEVICE_FLG_VERBOSEDBG
		msg = ControlMsgDevflags(ControlMsgDevflags.DEVICE_FLG_NODEBUG |
					 ControlMsgDevflags.DEVICE_FLG_VERBOSEDBG,
					 flg)
		reply = self.controlMsgSyncReply(msg)
		if not reply.isOK():
			raise CNCCException("Failed to set debugging flags")

	def deviceIsTurnedOn(self):
		if not self.deviceAvailable:
			return False
		return self.deviceIsOn

	def haveMotionHaltRequest(self):
		if not self.deviceAvailable:
			return False
		halt = self.motionHaltRequest
		self.motionHaltRequest = False
		return halt

	def getSpindleCommand(self):
		# Returns -1, 0 or 1 for reverse, stop or forward.
		if not self.deviceAvailable:
			return 0
		return self.spindleCommand

	def setSpindleState(self, direction):
		if not self.deviceAvailable:
			return
		if self.spindleState == direction:
			return
		self.spindleState = direction
		direction2state = {
			0:	ControlMsgSpindleupdate.SPINDLE_OFF,
			1:	ControlMsgSpindleupdate.SPINDLE_CW,
			-1:	ControlMsgSpindleupdate.SPINDLE_CCW,
		}
		msg = ControlMsgSpindleupdate(direction2state[direction])
		reply = self.controlMsgSyncReply(msg)
		if not reply.isOK():
			raise CNCCException("Failed to send spindle update")

	def getFeedOverrideState(self, minValue, maxValue):
		# Returns override state in percent (float)
		if not self.deviceAvailable:
			return 0
		inRange = 256
		outRange = maxValue - minValue
		mult = outRange / inRange
		return self.foState * mult + minValue

	def setFeedOverrideState(self, percent):
		# Sends the current FO percentage state to the device
		if not self.deviceAvailable:
			return
		if self.feedOverridePercent == percent:
			return # No change
		msg = ControlMsgFoupdate(int(round(percent)))
		reply = self.controlMsgSyncReply(msg)
		if not reply.isOK():
			raise CNCCException("Failed to send feed override state")
		self.feedOverridePercent = percent

	def setAxisPosition(self, axis, position):
		# Update axis position on device
		if not self.deviceAvailable:
			return
		pos = FixPt(position)
		if pos == self.axisPositions[axis]:
			return # No change
		msg = ControlMsgAxisupdate(pos, axis)
		reply = self.controlMsgSyncReply(msg)
		if not reply.isOK():
			raise CNCCException("Axis update failed: %s" % str(reply))
		self.axisPositions[axis] = pos

	def setEnabledAxes(self, axes):
		# Set the enabled axes.
		if not self.deviceAvailable:
			return
		mask = 0
		for ax in axes:
			mask |= (1 << AXIS2NUMBER[ax])
		msg = ControlMsgAxisenable(mask)
		reply = self.controlMsgSyncReply(msg)
		if not reply.isOK():
			raise CNCCException("Failed to set axis mask: %s" % str(reply))

	def getJogState(self, axis):
		# Returns (direction, incremental, velocity)
		if not self.deviceAvailable:
			return (0, False, 0)
		state = self.jogStates[axis]
		(direction, incremental, velocity) = state.get()
		retval = (direction.floatval,
			  incremental,
			  velocity.floatval)
		if incremental:
			state.reset()
		return retval
