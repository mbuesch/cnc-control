#!/usr/bin/env python3
"""
#
# CNC-control
# LinuxCNC HAL module
#
# Copyright (C) 2011-2023 Michael Büsch <m@bues.ch>
#
"""

import sys
import os
import errno
import time
from datetime import datetime, timedelta
import hal
from hal import HAL_BIT, HAL_U32, HAL_S32, HAL_FLOAT
from hal import HAL_IN, HAL_OUT, HAL_RO, HAL_RW
from cnccontrol_driver import *


class Timekeeping:
	def __init__(self):
		self.update()

	def update(self):
		self.now = datetime.now()

class BitPoke:
	IDLE	= 0
	DUTY	= 1

	def __init__(self, h, tk, pin, cycleMsec=3):
		self.h = h
		self.tk = tk
		self.pin = pin
		self.timeout = tk.now
		self.cycleMsec = cycleMsec
		h[pin] = 0

	def __updateTimeout(self):
		self.timeout = self.tk.now + timedelta(milliseconds=self.cycleMsec)

	def update(self):
		# Returns DUTY or IDLE
		if self.h[self.pin]:
			if self.tk.now >= self.timeout:
				self.h[self.pin] = 0
				self.__updateTimeout()
		else:
			if self.tk.now >= self.timeout:
				return BitPoke.IDLE
		return BitPoke.DUTY

	def startDuty(self):
		self.h[self.pin] = 1
		self.__updateTimeout()

	def state(self):
		return self.h[self.pin]

class ValueBalance:
	def __init__(self, h, tk,
		     scalePin, incPin, decPin, feedbackPin):
		self.h = h
		self.scalePin = scalePin
		self.incBit = BitPoke(h, tk, incPin)
		self.decBit = BitPoke(h, tk, decPin)
		self.feedbackPin = feedbackPin

	def balance(self, targetValue):
		if self.incBit.update() != BitPoke.IDLE or\
		   self.decBit.update() != BitPoke.IDLE:
			return
		value = self.h[self.feedbackPin]
		if equal(value, targetValue):
			self.h[self.scalePin] = 0
			return
		diff = value - targetValue
		self.h[self.scalePin] = abs(diff)
		if diff > 0:
			self.decBit.startDuty()
		else:
			self.incBit.startDuty()

class CNCControlHAL(CNCControl):
	def __init__(self, halName="cnccontrol"):
		CNCControl.__init__(self, verbose=True)

		self.tk = Timekeeping()

		self.h = hal.component(halName)
		self.__createHalPins()
		self.h.ready()

		self.foBalance = ValueBalance(self.h, self.tk,
				scalePin="feed-override.scale",
				incPin="feed-override.inc",
				decPin="feed-override.dec",
				feedbackPin="feed-override.value")

		self.incJogPlus = {}
		self.incJogMinus = {}
		for ax in ALL_AXES:
			self.incJogPlus[ax] = BitPoke(self.h, self.tk,
						      "jog.%s.inc-plus" % ax)
			self.incJogMinus[ax] = BitPoke(self.h, self.tk,
						       "jog.%s.inc-minus" % ax)

		self.programStop = BitPoke(self.h, self.tk,
					   "program.stop", cycleMsec=100)

		self.spindleStart = BitPoke(self.h, self.tk, "spindle.start")
		self.spindleStop = BitPoke(self.h, self.tk, "spindle.stop")

	def __checkLinuxCNC(self):
		for fn in ("/tmp/linuxcnc.lock",
			   "/tmp/linuxcnc.print"):
			try:
				os.stat(fn)
				return True
			except OSError as e:
				if e.errno in {errno.EPERM, errno.EACCES}:
					return True
		print("CNC-Control: LinuxCNC doesn't seem to be running")
		raise KeyboardInterrupt

	def __createHalPins(self):
		h = self.h

		# Device config
		h.newparam("config.twohand", HAL_BIT, HAL_RW)
		h.newparam("config.debug", HAL_U32, HAL_RW)
		h.newparam("config.debugperf", HAL_BIT, HAL_RW)
		h.newparam("config.usblogmsg", HAL_BIT, HAL_RW)

		# Machine state
		h.newpin("machine.on", HAL_BIT, HAL_IN)
		h.newpin("machine.estop.active", HAL_BIT, HAL_IN)
		h.newpin("machine.mode.jog", HAL_BIT, HAL_IN)
		h.newpin("machine.mode.mdi", HAL_BIT, HAL_IN)
		h.newpin("machine.mode.auto", HAL_BIT, HAL_IN)

		# Axis
		for ax in ALL_AXES:
			h.newparam("axis.%s.enable" % ax, HAL_BIT, HAL_RW)
			h.newpin("axis.%s.pos.machine-coords" % ax, HAL_FLOAT, HAL_IN)
			h.newpin("axis.%s.pos.user-coords" % ax, HAL_FLOAT, HAL_IN)

		# Jogging
		h.newpin("jog.velocity", HAL_FLOAT, HAL_OUT)
		h.newparam("jog.velocity-rapid", HAL_FLOAT, HAL_RW)
		for i in range(0, ControlMsgSetincrement.MAX_INDEX + 1):
			h.newparam("jog.increment.%d" % i, HAL_FLOAT, HAL_RW)
		for ax in ALL_AXES:
			h.newpin("jog.%s.minus" % ax, HAL_BIT, HAL_OUT)
			h.newpin("jog.%s.plus" % ax, HAL_BIT, HAL_OUT)
			h.newpin("jog.%s.inc" % ax, HAL_FLOAT, HAL_OUT)
			h.newpin("jog.%s.inc-plus" % ax, HAL_BIT, HAL_OUT)
			h.newpin("jog.%s.inc-minus" % ax, HAL_BIT, HAL_OUT)

		# Master spindle
		h.newpin("spindle.runs-bwd", HAL_BIT, HAL_IN)
		h.newpin("spindle.runs-fwd", HAL_BIT, HAL_IN)
		h.newpin("spindle.forward", HAL_BIT, HAL_OUT)
		h.newpin("spindle.reverse", HAL_BIT, HAL_OUT)
		h.newpin("spindle.start", HAL_BIT, HAL_OUT)
		h.newpin("spindle.stop", HAL_BIT, HAL_OUT)

		# Feed override
		h.newpin("feed-override.scale", HAL_FLOAT, HAL_OUT)
		h.newpin("feed-override.dec", HAL_BIT, HAL_OUT)
		h.newpin("feed-override.inc", HAL_BIT, HAL_OUT)
		h.newpin("feed-override.value", HAL_FLOAT, HAL_IN)
		h.newparam("feed-override.min-value", HAL_FLOAT, HAL_RW)
		h.newparam("feed-override.max-value", HAL_FLOAT, HAL_RW)

		# Program control
		h.newpin("program.stop", HAL_BIT, HAL_OUT)

	def __resetHalOutputPins(self):
		h = self.h

		# Jogging
		h["jog.velocity"] = 0
		for ax in ALL_AXES:
			h["jog.%s.minus" % ax] = 0
			h["jog.%s.plus" % ax] = 0
			h["jog.%s.inc" % ax] = 0
			h["jog.%s.inc-plus" % ax] = 0
			h["jog.%s.inc-minus" % ax] = 0

		# Master spindle
		h["spindle.forward"] = 0
		h["spindle.reverse"] = 0
		h["spindle.start"] = 0
		h["spindle.stop"] = 0

		# Feed override
		h["feed-override.dec"] = 0
		h["feed-override.inc"] = 0
		h["feed-override.scale"] = 0

		# Program control
		h["program.stop"] = 0

	def __deviceInitialize(self):
		# CNC-Control USB device connected. Initialize it.
		h = self.h
		self.deviceReset()
		self.setDebugging(h["config.debug"], h["config.usblogmsg"])
		self.setTwohandEnabled(h["config.twohand"])
		for i in range(0, ControlMsgSetincrement.MAX_INDEX + 1):
			self.setIncrementAtIndex(i, h["jog.increment.%d" % i])
		axes = [ax if h["axis.%s.enable" % ax] else "" for ax in ALL_AXES]
		self.setEnabledAxes([_f for _f in axes if _f])

	def __pingDevice(self):
		for i in range(0, 3):
			if self.deviceAppPing():
				break
		else:
			CNCCFatal.error("Failed to ping the device")

	def __updatePins(self):
		h = self.h

		self.setEstopState(h["machine.estop.active"])
		if not h["machine.on"] or\
		   not self.deviceIsTurnedOn():
			self.__resetHalOutputPins()
			return

		# Halt the program, if requested
		if self.haveMotionHaltRequest():
			self.programStop.startDuty()
		self.programStop.update()

		# Update master spindle state
		if self.spindleStart.update() == BitPoke.IDLE and\
		   self.spindleStop.update() == BitPoke.IDLE and\
		   h["machine.mode.jog"]:
			direction = self.getSpindleCommand()
			if direction < 0: # backward
				if not h["spindle.runs-bwd"]:
					h["spindle.forward"] = 0
					h["spindle.reverse"] = 1
					self.spindleStart.startDuty()
			elif direction > 0: # forward
				if not h["spindle.runs-fwd"]:
					h["spindle.forward"] = 1
					h["spindle.reverse"] = 0
					self.spindleStart.startDuty()
			else: # stop
				if h["spindle.runs-fwd"] or h["spindle.runs-bwd"]:
					h["spindle.forward"] = 0
					h["spindle.reverse"] = 0
					self.spindleStop.startDuty()
		if h["spindle.runs-bwd"]:
			self.setSpindleState(-1)
		elif h["spindle.runs-fwd"]:
			self.setSpindleState(1)
		else:
			self.setSpindleState(0)

		# Update feed override state
		foValue = self.getFeedOverrideState(h["feed-override.min-value"],
						    h["feed-override.max-value"])
		self.foBalance.balance(foValue)
		self.setFeedOverrideState(h["feed-override.value"] * 100)

		# Update jog states
		velocity = h["jog.velocity-rapid"]
		jogParams = {}
		for ax in ALL_AXES:
			if not h["axis.%s.enable" % ax]:
				continue
			(direction, incremental, vel) = self.getJogState(ax)
			if not equal(direction, 0.0):
				if vel < 0: # Rapid move
					vel = h["jog.velocity-rapid"]
				velocity = min(velocity, vel)
			jogParams[ax] = (direction, incremental)
		h["jog.velocity"] = velocity
		for ax in jogParams:
			self.incJogPlus[ax].update()
			self.incJogMinus[ax].update()
			if not h["machine.mode.jog"]:
				h["jog.%s.minus" % ax] = 0
				h["jog.%s.plus" % ax] = 0
				continue
			(direction, incremental) = jogParams[ax]
			if incremental and not equal(direction, 0.0):
				h["jog.%s.minus" % ax] = 0
				h["jog.%s.plus" % ax] = 0
				if not self.incJogPlus[ax].state() and\
				   not self.incJogMinus[ax].state():
					h["jog.%s.inc" % ax] = abs(direction)
					if direction > 0:
						self.incJogPlus[ax].startDuty()
					else:
						self.incJogMinus[ax].startDuty()
			else:
				if direction < -0.00001: # backward
					h["jog.%s.plus" % ax] = 0
					h["jog.%s.minus" % ax] = 1
				elif direction > 0.00001: # forward
					h["jog.%s.minus" % ax] = 0
					h["jog.%s.plus" % ax] = 1
				else: # stop
					h["jog.%s.minus" % ax] = 0
					h["jog.%s.plus" % ax] = 0

		# Update axis states
		g53coords = self.wantG53Coords()
		for ax in ALL_AXES:
			if not h["axis.%s.enable" % ax]:
				continue
			if g53coords:
				pos = h["axis.%s.pos.machine-coords" % ax]
			else:
				pos = h["axis.%s.pos.user-coords" % ax]
			self.setAxisPosition(ax, pos)

	def __eventLoop(self):
		avgRuntime = 0
		lastRuntimePrint = -1
		lastPing = -1
		timeDebug = bool(self.h["config.debugperf"])
		while self.__checkLinuxCNC():
			self.tk.update()
			start = self.tk.now
			try:
				if start.second != lastPing:
					lastPing = start.second
					self.__pingDevice()
				self.eventWait()
				self.tk.update()
				# Update pins, even if we didn't receive an event.
				self.__updatePins()
			except CNCCFatal as e:
				raise # Drop out of event loop and re-probe device.
			except CNCCException as e:
				print("CNC-Control error: " + str(e))
			if not timeDebug:
				continue
			self.tk.update()
			runtime = (self.tk.now - start).microseconds
			avgRuntime = (avgRuntime + runtime) // 2
			if start.second != lastRuntimePrint:
				lastRuntimePrint = start.second
				print("Average event loop runtime = %.1f milliseconds" %\
					(float(avgRuntime) / 1000))

	def probeLoop(self):
		self.__resetHalOutputPins()
		while self.__checkLinuxCNC():
			try:
				if self.probe():
					self.__deviceInitialize()
					self.__eventLoop()
				else:
					time.sleep(0.2)
			except CNCCFatal as e:
				print("CNC-Control fatal error: " + str(e))
			except CNCCException as e:
				print("CNC-Control error: " + str(e))
			self.__resetHalOutputPins()

def main():
	try:
		try:
			os.nice(-20)
		except OSError as e:
			print("WARNING: Failed to renice cnccontrol HAL module:", str(e))
		cncc = CNCControlHAL()
		cncc.probeLoop()
	except CNCCException as e:
		print("CNC-Control: Unhandled exception: " + str(e))
		return 1
	except KeyboardInterrupt as e:
		print("CNC-Control: shutdown")
		return 0

if __name__ == "__main__":
	sys.exit(main())
