#!/usr/bin/env python3
"""
#
# CNC-control - Host driver
#
# Copyright (C) 2011 Michael Buesch <m@bues.ch>
#
"""

import sys
import usb
import errno
import time
from datetime import datetime, timedelta


IDVENDOR	= 0x6666
IDPRODUCT	= 0xC8CC

EP_IN		= 0x82
EP_OUT		= 0x02
EP_IRQ		= 0x81

ALL_AXES	= "xyzuvwabc"
AXIS2NUMBER	= dict([(x[1], x[0]) for x in enumerate(ALL_AXES)])
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

class CNCCException(Exception):
	@classmethod
	def info(cls, message):
		print("CNC-Control:", message)

	@classmethod
	def warn(cls, message):
		print("CNC-Control WARNING:", message)

	@classmethod
	def error(cls, message):
		raise cls(message)

class CNCCFatal(CNCCException):
	pass

class FixPt(object):
	FIXPT_FRAC_BITS		= 16
	MIN_INT_LIMIT		= -(1 << (FIXPT_FRAC_BITS - 1))
	MIN_LIMIT		= float(MIN_INT_LIMIT) - 0.99999
	MAX_INT_LIMIT		= (1 << (FIXPT_FRAC_BITS - 1)) - 1
	MAX_LIMIT		= float(MAX_INT_LIMIT) + 0.99999

	def __init__(self, val):
		if isinstance(val, FixPt):
			self.raw = val.raw
			self.floatval = val.floatval
			return
		if type(val) == float:
			self.assertRepresentable(val)
			raw = (int(val * float(1 << self.FIXPT_FRAC_BITS)) + 1) & 0xFFFFFFFF
			self.raw = self.__int2raw(raw)
			self.floatval = val
			return
		if type(val) == int:
			self.assertRepresentable(val)
			self.raw = self.__int2raw(val << self.FIXPT_FRAC_BITS)
			self.floatval = float(val)
			return
		try: # Try 32bit LE two's complement
			self.raw = tuple(val)
			raw = twos32(val[0] | (val[1] << 8) |\
				     (val[2] << 16) | (val[3] << 24))
			self.floatval = round(float(raw) / float(1 << self.FIXPT_FRAC_BITS), 4)
		except (TypeError, IndexError) as e:
			CNCCException.error("FixPt TypeError")

	@classmethod
	def representable(cls, val):
		# Returns true, if the integer part is representable by FixPt
		ival = int(val)
		return ival > cls.MIN_INT_LIMIT if ival < 0 else\
		       ival < cls.MAX_INT_LIMIT

	@classmethod
	def assertRepresentable(cls, val):
		if cls.representable(val):
			return
		CNCCException.error("FixPt: Value %s is not representable" %\
				    str(val))

	@staticmethod
	def __int2raw(val):
		return (val & 0xFF, (val >> 8) & 0xFF,
			(val >> 16) & 0xFF, (val >> 24) & 0xFF)

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

	def isNegative(self):
		return bool(self.raw[3] & 0x80)

class ControlMsg(object):
	# IDs
	CONTROL_PING			= 0
	CONTROL_RESET			= 1
	CONTROL_DEVFLAGS		= 2
	CONTROL_AXISUPDATE		= 3
	CONTROL_SPINDLEUPDATE		= 4
	CONTROL_FOUPDATE		= 5
	CONTROL_AXISENABLE		= 6
	CONTROL_ESTOPUPDATE		= 7
	CONTROL_SETINCREMENT		= 8
	CONTROL_ENTERBOOT		= 0xA0
	CONTROL_EXITBOOT		= 0xA1
	CONTROL_BOOT_WRITEBUF		= 0xA2
	CONTROL_BOOT_FLASHPG		= 0xA3

	# Flags
	CONTROL_FLG_BOOTLOADER		= 0x80

	# Targets
	TARGET_CPU			= 0
	TARGET_COPROC			= 1

	def __init__(self, id, flags, seqno):
		self.id = id
		self.flags = flags
		self.reserved = 0
		self.seqno = seqno

	def setSeqno(self, seqno):
		self.seqno = seqno

	def getRaw(self):
		return [self.id & 0xFF, self.flags & 0xFF,
			self.reserved & 0xFF, self.seqno & 0xFF]

class ControlMsgPing(ControlMsg):
	def __init__(self, hdrFlags=0, hdrSeqno=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_PING,
				    hdrFlags, hdrSeqno)

class ControlMsgReset(ControlMsg):
	def __init__(self, hdrFlags=0, hdrSeqno=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_RESET,
				    hdrFlags, hdrSeqno)

class ControlMsgDevflags(ControlMsg):
	DEVICE_FLG_NODEBUG	= (1 << 0)
	DEVICE_FLG_VERBOSEDBG	= (1 << 1)
	DEVICE_FLG_ON		= (1 << 2)
	DEVICE_FLG_TWOHANDEN	= (1 << 3)
	DEVICE_FLG_USBLOGMSG	= (1 << 4)
	DEVICE_FLG_G53COORDS	= (1 << 5)

	def __init__(self, devFlagsMask, devFlagsSet, hdrFlags=0, hdrSeqno=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_DEVFLAGS,
				    hdrFlags, hdrSeqno)
		self.devFlagsMask = devFlagsMask
		self.devFlagsSet = devFlagsSet

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.extend( [self.devFlagsMask & 0xFF, (self.devFlagsMask >> 8) & 0xFF] )
		raw.extend( [self.devFlagsSet & 0xFF, (self.devFlagsSet >> 8) & 0xFF] )
		return raw

class ControlMsgAxisupdate(ControlMsg):
	def __init__(self, pos, axis, hdrFlags=0, hdrSeqno=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_AXISUPDATE,
				    hdrFlags, hdrSeqno)
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

	def __init__(self, state, hdrFlags=0, hdrSeqno=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_SPINDLEUPDATE,
				    hdrFlags, hdrSeqno)
		self.state = state

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.append(self.state & 0xFF)
		return raw

class ControlMsgFoupdate(ControlMsg):
	def __init__(self, percent, hdrFlags=0, hdrSeqno=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_FOUPDATE,
				    hdrFlags, hdrSeqno)
		self.percent = percent

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.append(self.percent & 0xFF)
		return raw

class ControlMsgAxisenable(ControlMsg):
	def __init__(self, mask, hdrFlags=0, hdrSeqno=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_AXISENABLE,
				    hdrFlags, hdrSeqno)
		self.mask = mask

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.extend( [self.mask & 0xFF, (self.mask >> 8) & 0xFF] )
		return raw

class ControlMsgEstopupdate(ControlMsg):
	def __init__(self, asserted, hdrFlags=0, hdrSeqno=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_ESTOPUPDATE,
				    hdrFlags, hdrSeqno)
		self.asserted = 1 if asserted else 0

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.append(self.asserted & 0xFF)
		return raw

class ControlMsgSetincrement(ControlMsg):
	MAX_INDEX	= 5
	MAX_INC_FLOAT	= 9.999

	def __init__(self, increment, index, hdrFlags=0, hdrSeqno=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_SETINCREMENT,
				    hdrFlags, hdrSeqno)
		self.increment = FixPt(increment)
		self.index = index
		if self.increment.isNegative():
			CNCCFatal.error("Invalid negative JOG increment %f at index %d" %\
				(self.increment.floatval, index))
		if self.increment.floatval > ControlMsgSetincrement.MAX_INC_FLOAT:
			CNCCFatal.error("JOG increment %f at index %d is too big. Max = %f" %\
				(self.increment.floatval, index, ControlMsgSetincrement.MAX_INC_FLOAT))

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.extend(self.increment.getRaw())
		raw.append(self.index & 0xFF)
		return raw

class ControlMsgEnterboot(ControlMsg):
	ENTERBOOT_MAGIC0		= 0xB0
	ENTERBOOT_MAGIC1		= 0x07

	def __init__(self, target, hdrFlags=0, hdrSeqno=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_ENTERBOOT,
				    hdrFlags, hdrSeqno)
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
	def __init__(self, target,
		     hdrFlags=ControlMsg.CONTROL_FLG_BOOTLOADER, hdrSeqno=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_EXITBOOT,
				    hdrFlags, hdrSeqno)
		self.target = target

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.append(self.target & 0xFF)
		return raw

class ControlMsgBootWritebuf(ControlMsg):
	DATA_MAX_BYTES = 32

	def __init__(self, offset, data,
		     hdrFlags=ControlMsg.CONTROL_FLG_BOOTLOADER, hdrSeqno=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_BOOT_WRITEBUF,
				    hdrFlags, hdrSeqno)
		self.offset = offset
		self.size = len(data)
		self.crc = crc8Buf(0, data) ^ 0xFF
		nrPadding = ControlMsgBootWritebuf.DATA_MAX_BYTES - len(data)
		if nrPadding < 0:
			CNCCException.error("ControlMsg-BootWritebuf: invalid data length %d" %\
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
		     hdrFlags=ControlMsg.CONTROL_FLG_BOOTLOADER, hdrSeqno=0):
		ControlMsg.__init__(self, ControlMsg.CONTROL_BOOT_FLASHPG,
				    hdrFlags, hdrSeqno)
		self.address = address
		self.target = target

	def getRaw(self):
		raw = ControlMsg.getRaw(self)
		raw.extend( [self.address & 0xFF, (self.address >> 8) & 0xFF] )
		raw.append(self.target & 0xFF)
		return raw

class ControlReply(object):
	MAX_SIZE		= 6

	# IDs
	REPLY_OK		= 0
	REPLY_ERROR		= 1
	REPLY_VAL16		= 2

	def __init__(self, id, flags, seqno):
		self.id = id
		self.flags = flags
		self.reserved = 0
		self.seqno = seqno

	@staticmethod
	def parseRaw(raw):
		try:
			id = raw[0]
			flags = raw[1]
			#reserved = raw[2]
			seqno = raw[3]
			raw = raw[4:]
			if id == ControlReply.REPLY_OK:
				return ControlReplyOk(hdrFlags=flags, hdrSeqno=seqno)
			elif id == ControlReply.REPLY_ERROR:
				return ControlReplyError(raw[0],
							 hdrFlags=flags, hdrSeqno=seqno)
			elif id == ControlReply.REPLY_VAL16:
				return ControlReplyVal16(raw[0] | (raw[1] << 8),
							 hdrFlags=flags, hdrSeqno=seqno)
			else:
				CNCCException.error("Unknown ControlReply ID: %d" % id)
		except (IndexError, KeyError):
			CNCCException.error("Failed to parse ControlReply (%d bytes)" % len(raw))

	def __repr__(self):
		return "Unknown reply code"

	def isOK(self):
		return False

class ControlReplyOk(ControlReply):
	def __init__(self, hdrFlags=0, hdrSeqno=0):
		ControlReply.__init__(self, ControlReply.REPLY_OK,
				      hdrFlags, hdrSeqno)

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

	def __init__(self, code, hdrFlags=0, hdrSeqno=0):
		ControlReply.__init__(self, ControlReply.REPLY_ERROR,
				      hdrFlags, hdrSeqno)
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
	def __init__(self, value, hdrFlags=0, hdrSeqno=0):
		ControlReply.__init__(self, ControlReply.REPLY_VAL16,
				      hdrFlags, hdrSeqno)
		self.value = value

	def __repr__(self):
		return "0x%04X" % self.value

	def isOK(self):
		return True

class ControlIrq(object):
	MAX_SIZE		= 14

	# IDs
	IRQ_JOG			= 0
	IRQ_JOG_KEEPALIFE	= 1
	IRQ_SPINDLE		= 2
	IRQ_FEEDOVERRIDE	= 3
	IRQ_DEVFLAGS		= 4
	IRQ_HALT		= 5
	IRQ_LOGMSG		= 6

	# Flags
	IRQ_FLG_TXQOVR		= (1 << 0)
	IRQ_FLG_PRIO		= (1 << 1)
	IRQ_FLG_DROPPABLE	= (1 << 2)

	def __init__(self, id, flags, seqno):
		self.id = id
		self.flags = flags
		self.reserved = 0
		self.seqno = seqno

	@staticmethod
	def parseRaw(raw):
		try:
			id = raw[0]
			flags = raw[1]
			#reserved = raw[2]
			seqno = raw[3]
			raw = raw[4:]
			if id == ControlIrq.IRQ_JOG:
				return ControlIrqJog(raw[0:4], raw[4:8], raw[8], raw[9],
						     hdrFlags=flags, hdrSeqno=seqno)
			elif id == ControlIrq.IRQ_JOG_KEEPALIFE:
				return ControlIrqJogKeepalife(hdrFlags=flags, hdrSeqno=seqno)
			elif id == ControlIrq.IRQ_SPINDLE:
				return ControlIrqSpindle(raw[0],
							 hdrFlags=flags, hdrSeqno=seqno)
			elif id == ControlIrq.IRQ_FEEDOVERRIDE:
				return ControlIrqFeedoverride(raw[0],
							      hdrFlags=flags, hdrSeqno=seqno)
			elif id == ControlIrq.IRQ_DEVFLAGS:
				return ControlIrqDevflags(raw[0:2],
							  hdrFlags=flags, hdrSeqno=seqno)
			elif id == ControlIrq.IRQ_HALT:
				return ControlIrqHalt(hdrFlags=flags, hdrSeqno=seqno)
			elif id == ControlIrq.IRQ_LOGMSG:
				return ControlIrqLogmsg(raw[0:10],
							hdrFlags=flags, hdrSeqno=seqno)
			else:
				CNCCException.error("Unknown ControlIrq ID: %d" % id)
		except (IndexError, KeyError):
			CNCCException.error("Failed to parse ControlIrq (%d bytes)" % len(raw))

	def __repr__(self):
		return "Unknown interrupt"

class ControlIrqJog(ControlIrq):
	IRQ_JOG_CONTINUOUS	= (1 << 0)
	IRQ_JOG_RAPID		= (1 << 1)

	def __init__(self, increment, velocity, axis, flags,
		     hdrFlags=0, hdrSeqno=0):
		ControlIrq.__init__(self, ControlIrq.IRQ_JOG,
				    hdrFlags, hdrSeqno)
		self.increment = FixPt(increment)
		self.velocity = FixPt(velocity)
		self.axis = NUMBER2AXIS[axis]
		self.jogFlags = flags

	def __repr__(self):
		return "JOG interrupt (nr%d): %s, %s, %s, 0x%X" %\
			(self.seqno, str(self.increment),
			 str(self.velocity), self.axis, self.jogFlags)

class ControlIrqJogKeepalife(ControlIrq):
	def __init__(self, hdrFlags=0, hdrSeqno=0):
		ControlIrq.__init__(self, ControlIrq.IRQ_JOG_KEEPALIFE,
				    hdrFlags, hdrSeqno)

	def __repr__(self):
		return "JOG KEEPALIFE interrupt (nr%d)" % (self.seqno)

class ControlIrqSpindle(ControlIrq):
	def __init__(self, state, hdrFlags=0, hdrSeqno=0):
		ControlIrq.__init__(self, ControlIrq.IRQ_SPINDLE,
				    hdrFlags, hdrSeqno)
		self.state = state

	def __repr__(self):
		return "SPINDLE interrupt (nr%d): %d" % (self.seqno, self.state)

class ControlIrqFeedoverride(ControlIrq):
	def __init__(self, state, hdrFlags=0, hdrSeqno=0):
		ControlIrq.__init__(self, ControlIrq.IRQ_FEEDOVERRIDE,
				    hdrFlags, hdrSeqno)
		self.state = state

	def __repr__(self):
		return "FEEDOVERRIDE interrupt (nr%d): %d" % (self.seqno, self.state)

class ControlIrqDevflags(ControlIrq):
	def __init__(self, devFlags, hdrFlags=0, hdrSeqno=0):
		ControlIrq.__init__(self, ControlIrq.IRQ_DEVFLAGS,
				    hdrFlags, hdrSeqno)
		self.devFlags = devFlags[0] | (devFlags[1] << 8)

	def __repr__(self):
		return "DEVFLAGS interrupt (nr%d): %04X" % (self.seqno, self.devFlags)

class ControlIrqHalt(ControlIrq):
	def __init__(self, hdrFlags=0, hdrSeqno=0):
		ControlIrq.__init__(self, ControlIrq.IRQ_HALT,
				    hdrFlags, hdrSeqno)

	def __repr__(self):
		return "HALT interrupt (nr%d)" % (self.seqno)

class ControlIrqLogmsg(ControlIrq):
	def __init__(self, msg, hdrFlags=0, hdrSeqno=0):
		ControlIrq.__init__(self, ControlIrq.IRQ_LOGMSG,
				    hdrFlags, hdrSeqno)
		self.msg = "".join([c if type(c) == str else chr(c) for c in msg])

	def __repr__(self):
		return "LOGMSG interrupt (nr%d)" % (self.seqno)

class JogState(object):
	KEEPALIFE_TIMEOUT = 0.3
	STOPDATA = (FixPt(0.0), False, FixPt(0.0))

	def __init__(self):
		self.reset()

	def get(self):
		direction, incremental, velocity =\
			self.__direction, self.__incremental, self.__velocity
		if not equal(direction.floatval, 0.0):
			if datetime.now() > self.__timeout:
				CNCCException.warn("Jog keepalife timer expired")
				self.reset()
				return self.STOPDATA
		return (direction, incremental, velocity)

	def set(self, direction, incremental, velocity):
		self.__direction, self.__incremental, self.__velocity =\
			direction, incremental, velocity
		self.keepAlife()

	def reset(self):
		self.set(self.STOPDATA[0], self.STOPDATA[1], self.STOPDATA[2])

	def keepAlife(self):
		self.__timeout = datetime.now() +\
			timedelta(seconds=self.KEEPALIFE_TIMEOUT)

class CNCControl(object):
	def __init__(self, verbose=False):
		self.deviceAvailable = False
		self.verbose = verbose

	@staticmethod
	def __haveEndpoint(interface, epAddress):
		found = [ep for ep in interface.endpoints if ep.address == epAddress]
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
			CNCCException.error("Failed to ping the application: %s" % str(reply))
		# Check if we're in the bootloader code
		ping = ControlMsgPing(hdrFlags=ControlMsg.CONTROL_FLG_BOOTLOADER)
		reply = self.controlMsgSyncReply(ping)
		if reply.isOK():
			return True
		if reply.id == ControlReply.REPLY_ERROR and\
		   reply.code != ControlReplyError.CTLERR_CONTEXT:
			CNCCException.error("Failed to ping the bootloader: %s" % str(reply))
		CNCCException.error("Unknown PING error")

	def probe(self):
		if self.deviceAvailable:
			return True
		self.__initializeData()
		try:
			self.usbdev = self.__findDevice(IDVENDOR, IDPRODUCT)
			if not self.usbdev:
				return False

			time.sleep(0.1)
			self.usbh = self.usbdev.open()
			self.usbh.reset()
			time.sleep(0.2)
			config = self.usbdev.configurations[0]
			interface = config.interfaces[0][0]

			self.usbh.setConfiguration(config)
			self.usbh.claimInterface(interface)
			self.usbh.setAltInterface(interface)
			self.__epClearHalt(interface, EP_IN)
			self.__epClearHalt(interface, EP_OUT)
			self.__epClearHalt(interface, EP_IRQ)
		except (usb.USBError) as e:
			self.__usbError(e, fatal=True, origin="init")
		self.__devicePlug()
		return True

	def deviceReset(self):
		self.__initializeData()
		reply = self.controlMsgSyncReply(ControlMsgReset())
		if not reply.isOK():
			CNCCException.error("Failed to reset the device state")
		reply = self.controlMsgSyncReply(ControlMsgDevflags(0, 0))
		if not reply.isOK():
			CNCCException.error("Failed to read device flags")
		self.__interpretDevFlags(reply.value)

	def deviceAppPing(self):
		return self.controlMsgSyncReply(ControlMsgPing()).isOK()

	def reconnect(self, timeoutSec=15):
		if not self.deviceAvailable:
			return False
		timeout = datetime.now() + timedelta(seconds=timeoutSec)
		self.__deviceUnplug()
		while timeout > datetime.now():
			try:
				if self.probe():
					return True
			except (CNCCException) as e:
				pass
			time.sleep(0.05)
		return False

	def __initializeData(self):
		self.messageSequenceNumber = 0
		self.deviceIsOn = False
		self.g53coords = False
		self.estop = False
		self.motionHaltRequest = False
		self.axisPositions = { }
		self.axisPosUpdatePending = { }
		self.lastAxisPosUpdate = { }
		self.jogStates = { }
		for ax in ALL_AXES:
			self.axisPositions[ax] = FixPt(0.0)
			self.axisPosUpdatePending[ax] = False
			self.lastAxisPosUpdate[ax] = datetime(1970, 1, 1)
			self.jogStates[ax] = JogState()
		self.foState = 0
		self.spindleCommand = 0
		self.spindleState = 0
		self.feedOverridePercent = 0
		self.logMsgBuf = ""

	def __interpretDevFlags(self, devFlags):
		if devFlags & ControlMsgDevflags.DEVICE_FLG_ON:
			if not self.deviceIsOn:
				CNCCException.info("turned ON")
				self.deviceIsOn = True
		else:
			if self.deviceIsOn:
				CNCCException.info("turned OFF")
				self.deviceIsOn = False
		self.g53coords = bool(devFlags & ControlMsgDevflags.DEVICE_FLG_G53COORDS)

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
			CNCCException.info("device connected")

	def __deviceUnplug(self):
		if self.deviceAvailable:
			self.deviceAvailable = False
			CNCCException.info("device disconnected")

	def __deviceUnplugException(self, message):
		self.__deviceUnplug()
		CNCCFatal.error(message)

	def __usbError(self, usbException, fatal=False, origin=None):
		if usbException.errno == errno.ENODEV or\
		   str(usbException).lower().find("no such device") >= 0:
			self.__deviceUnplugException("Unplug exception")
		cls = CNCCFatal if fatal else CNCCException
		cls.error("USB error%s: %s" %(
			  ((" (%s)" % origin) if origin else ""),
			  str(usbException)))

	def eventWait(self, timeoutMs=30):
		if not self.deviceAvailable:
			self.__deviceUnplugException()
		try:
			data = self.usbh.interruptRead(EP_IRQ, ControlIrq.MAX_SIZE,
						       timeoutMs)
		except (usb.USBError) as e:
			if not e.errno:
				return False # Timeout. No event.
			self.__usbError(e, origin="eventWait")
		if data:
			self.__handleInterrupt(data)
		return True

	def __handleInterrupt(self, rawData):
		irq = ControlIrq.parseRaw(rawData)
		if irq.flags & ControlIrq.IRQ_FLG_TXQOVR:
			CNCCException.warn("Interrupt queue overflow detected")
#		print irq
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
			list(map(lambda ax: self.jogStates[ax].keepAlife(),
			    self.jogStates))
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
			self.__interpretDevFlags(irq.devFlags)
		elif irq.id == ControlIrq.IRQ_HALT:
			self.motionHaltRequest = True
			for jogState in list(self.jogStates.values()):
				jogState.reset()
			self.spindleCommand = 0
		elif irq.id == ControlIrq.IRQ_LOGMSG:
			msg = self.logMsgBuf + irq.msg
			msg = msg.split('\n')
			while len(msg) > 1:
				print("[dev debug]:", msg[0])
				msg = msg[1:]
			self.logMsgBuf = msg[0]
		else:
			CNCCException.warn("Unhandled IRQ: " + str(irq))

	def controlMsg(self, msg, timeoutMs=300):
		try:
			msg.setSeqno(self.messageSequenceNumber)
			self.messageSequenceNumber = (self.messageSequenceNumber + 1) & 0xFF

			rawData = msg.getRaw()
			size = self.usbh.bulkWrite(EP_OUT, rawData, timeoutMs)
			if len(rawData) != size:
				CNCCException.error("Only wrote %d bytes of %d bytes "
					"bulk write" % (size, len(rawData)))
		except (usb.USBError) as e:
			self.__usbError(e, origin="controlMsg")

	def controlReply(self, timeoutMs=300):
		try:
			data = self.usbh.bulkRead(EP_IN, ControlReply.MAX_SIZE, timeoutMs)
		except (usb.USBError) as e:
			self.__usbError(e, origin="controlReply")
		return ControlReply.parseRaw(data)

	def controlMsgSyncReply(self, msg, timeoutMs=300):
		self.controlMsg(msg, timeoutMs)
		reply = self.controlReply(timeoutMs)
		if msg.seqno != reply.seqno:
			CNCCException.error("Got invalid reply sequence number: %d vs %d" %\
				(msg.seqno, reply.seqno))
		return reply

	def setTwohandEnabled(self, enable):
		if not self.deviceAvailable:
			self.__deviceUnplugException()
		flg = 0
		if enable:
			flg = ControlMsgDevflags.DEVICE_FLG_TWOHANDEN
		msg = ControlMsgDevflags(ControlMsgDevflags.DEVICE_FLG_TWOHANDEN, flg)
		reply = self.controlMsgSyncReply(msg)
		if not reply.isOK():
			CNCCException.error("Failed to set Twohand flag")

	def setIncrementAtIndex(self, index, increment):
		if not self.deviceAvailable:
			self.__deviceUnplugException()
		if equal(increment, 0.0):
			increment = FixPt(0)
		msg = ControlMsgSetincrement(increment, index)
		reply = self.controlMsgSyncReply(msg)
		if not reply.isOK():
			CNCCException.error("Failed to set increment %f at index %d: %s" %\
				(increment, index, str(reply)))

	def setDebugging(self, debugLevel, usbMessages):
		# 0 => disabled, 1 => enabled, 2 => verbose
		if not self.deviceAvailable:
			self.__deviceUnplugException()
		flg = ControlMsgDevflags.DEVICE_FLG_NODEBUG
		if debugLevel >= 1:
			flg &= ~ControlMsgDevflags.DEVICE_FLG_NODEBUG
		if debugLevel >= 2:
			flg |= ControlMsgDevflags.DEVICE_FLG_VERBOSEDBG
		if usbMessages:
			flg |= ControlMsgDevflags.DEVICE_FLG_USBLOGMSG
		msg = ControlMsgDevflags(ControlMsgDevflags.DEVICE_FLG_NODEBUG |
					 ControlMsgDevflags.DEVICE_FLG_VERBOSEDBG |
					 ControlMsgDevflags.DEVICE_FLG_USBLOGMSG,
					 flg)
		reply = self.controlMsgSyncReply(msg)
		if not reply.isOK():
			CNCCException.error("Failed to set debugging flags")

	def setEstopState(self, asserted):
		# Send the estop state to the device
		if not self.deviceAvailable:
			self.__deviceUnplugException()
		if asserted == self.estop:
			return # No change
		msg = ControlMsgEstopupdate(asserted)
		reply = self.controlMsgSyncReply(msg)
		if not reply.isOK():
			CNCCException.error("Failed to send ESTOP update")
		self.estop = asserted

	def deviceIsTurnedOn(self):
		if not self.deviceAvailable:
			self.__deviceUnplugException()
		return self.deviceIsOn

	def haveMotionHaltRequest(self):
		if not self.deviceAvailable:
			self.__deviceUnplugException()
		halt = self.motionHaltRequest
		self.motionHaltRequest = False
		return halt

	def getSpindleCommand(self):
		# Returns -1, 0 or 1 for reverse, stop or forward.
		if not self.deviceAvailable:
			self.__deviceUnplugException()
		return self.spindleCommand

	def setSpindleState(self, direction):
		if not self.deviceAvailable:
			self.__deviceUnplugException()
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
			CNCCException.error("Failed to send spindle update")

	def getFeedOverrideState(self, minValue, maxValue):
		# Returns override state in percent (float)
		if not self.deviceAvailable:
			self.__deviceUnplugException()
		inRange = 256
		outRange = maxValue - minValue
		mult = outRange / inRange
		return self.foState * mult + minValue

	def setFeedOverrideState(self, percent):
		# Sends the current FO percentage state to the device
		if not self.deviceAvailable:
			self.__deviceUnplugException()
		if self.feedOverridePercent == percent:
			return # No change
		msg = ControlMsgFoupdate(int(round(percent)))
		reply = self.controlMsgSyncReply(msg)
		if not reply.isOK():
			CNCCException.error("Failed to send feed override state")
		self.feedOverridePercent = percent

	def setAxisPosition(self, axis, position):
		# Update axis position on device
		if not self.deviceAvailable:
			self.__deviceUnplugException()
		pos = FixPt(position)
		if pos != self.axisPositions[axis]:
			self.axisPositions[axis] = pos
			self.axisPosUpdatePending[axis] = True
		if self.axisPosUpdatePending[axis]:
			now = datetime.now()
			if now < self.lastAxisPosUpdate[axis] + timedelta(seconds=0.1):
				return # Not yet
			msg = ControlMsgAxisupdate(pos, axis)
			reply = self.controlMsgSyncReply(msg)
			if not reply.isOK():
				CNCCException.error("Axis update failed: %s" % str(reply))
			self.axisPosUpdatePending[axis] = False
			self.lastAxisPosUpdate[axis] = now

	def wantG53Coords(self):
		return self.g53coords

	def setEnabledAxes(self, axes):
		# Set the enabled axes.
		if not self.deviceAvailable:
			self.__deviceUnplugException()
		mask = 0
		for ax in axes:
			mask |= (1 << AXIS2NUMBER[ax])
		msg = ControlMsgAxisenable(mask)
		reply = self.controlMsgSyncReply(msg)
		if not reply.isOK():
			CNCCException.error("Failed to set axis mask: %s" % str(reply))

	def getJogState(self, axis):
		# Returns (direction, incremental, velocity)
		if not self.deviceAvailable:
			self.__deviceUnplugException()
		state = self.jogStates[axis]
		(direction, incremental, velocity) = state.get()
		retval = (direction.floatval,
			  incremental,
			  velocity.floatval)
		if incremental:
			state.reset()
		return retval
