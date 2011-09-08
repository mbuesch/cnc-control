#!/usr/bin/env python
"""
#
# CNC-control
# EMC2 HAL module
#
# Copyright (C) 2011 Michael Buesch <m@bues.ch>
#
"""

import sys
import os
import time
from datetime import datetime, timedelta
import hal
from hal import HAL_BIT, HAL_U32, HAL_S32, HAL_FLOAT
from hal import HAL_IN, HAL_OUT, HAL_RO, HAL_RW
from cnccontrol_driver import *


class Context:
	def __init__(self, h, cncc):
		self.h = h
		self.cncc = cncc

		self.foBalance = ValueBalance(h,
				scalePin="feed-override.scale",
				incPin="feed-override.inc",
				decPin="feed-override.dec",
				feedbackPin="feed-override.value")

		self.incJogPlus = {}
		self.incJogMinus = {}
		for ax in ALL_AXES:
			self.incJogPlus[ax] = BitPoke(h, "jog.%s.inc-plus" % ax)
			self.incJogMinus[ax] = BitPoke(h, "jog.%s.inc-minus" % ax)

		self.programStop = BitPoke(h, "program.stop", cycleMsec=100)

		self.spindleStart = BitPoke(h, "spindle.start")
		self.spindleStop = BitPoke(h, "spindle.stop")

class BitPoke:
	def __init__(self, h, pin, cycleMsec=15):
		self.h = h
		self.pin = pin
		self.timeout = datetime.now()
		self.cycleMsec = cycleMsec
		h[pin] = 0

	def __updateTimeout(self, now):
		self.timeout = now + timedelta(0, 0, self.cycleMsec * 1000)

	def update(self):
		# Returns True, if idle
		now = datetime.now()
		if self.h[self.pin]:
			if now >= self.timeout:
				self.h[self.pin] = 0
				self.__updateTimeout(now)
		else:
			if now >= self.timeout:
				return True
		return False

	def startDuty(self):
		self.h[self.pin] = 1
		self.__updateTimeout(datetime.now())

	def state(self):
		return self.h[self.pin]

class ValueBalance:
	def __init__(self, h,
		     scalePin, incPin, decPin, feedbackPin):
		self.h = h
		self.scalePin = scalePin
		self.incBit = BitPoke(h, incPin)
		self.decBit = BitPoke(h, decPin)
		self.feedbackPin = feedbackPin

	def balance(self, targetValue):
		if not self.incBit.update() or\
		   not self.decBit.update():
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

def updatePins(ctx):
	h = ctx.h
	cncc = ctx.cncc

	cncc.setEstopState(h["machine.estop-active"])
	if not h["machine.is-on"] or\
	   not cncc.deviceIsTurnedOn():
		return

	# Halt the program, if requested
	if cncc.haveMotionHaltRequest():
		ctx.programStop.startDuty()
	ctx.programStop.update()

	# Update master spindle state
	if ctx.spindleStart.update() and\
	   ctx.spindleStop.update():
		direction = cncc.getSpindleCommand()
		if direction < 0: # backward
			if not h["spindle.runs-bwd"]:
				h["spindle.forward"] = 0
				h["spindle.reverse"] = 1
				ctx.spindleStart.startDuty()
		elif direction > 0: # forward
			if not h["spindle.runs-fwd"]:
				h["spindle.forward"] = 1
				h["spindle.reverse"] = 0
				ctx.spindleStart.startDuty()
		else: # stop
			if h["spindle.runs-fwd"] or h["spindle.runs-bwd"]:
				h["spindle.forward"] = 0
				h["spindle.reverse"] = 0
				ctx.spindleStop.startDuty()
	if h["spindle.runs-bwd"]:
		cncc.setSpindleState(-1)
	elif h["spindle.runs-fwd"]:
		cncc.setSpindleState(1)
	else:
		cncc.setSpindleState(0)

	# Update feed override state
	foValue = cncc.getFeedOverrideState(h["feed-override.min-value"],
					    h["feed-override.max-value"])
	ctx.foBalance.balance(foValue)
	cncc.setFeedOverrideState(h["feed-override.value"] * 100)

	# Update jog states
	velocity = h["jog.velocity-rapid"]
	jogParams = {}
	for ax in ALL_AXES:
		if not h["axis.%s.enable" % ax]:
			continue
		(direction, incremental, vel) = cncc.getJogState(ax)
		if not equal(direction, 0.0):
			if vel < 0: # Rapid move
				vel = h["jog.velocity-rapid"]
			velocity = min(velocity, vel)
		jogParams[ax] = (direction, incremental)
	h["jog.velocity"] = velocity
	for ax in jogParams:
		ctx.incJogPlus[ax].update()
		ctx.incJogMinus[ax].update()
		(direction, incremental) = jogParams[ax]
		if incremental and not equal(direction, 0.0):
			h["jog.%s.minus" % ax] = 0
			h["jog.%s.plus" % ax] = 0
			if not ctx.incJogPlus[ax].state() and\
			   not ctx.incJogMinus[ax].state():
				h["jog.%s.inc" % ax] = abs(direction)
				if direction > 0:
					ctx.incJogPlus[ax].startDuty()
				else:
					ctx.incJogMinus[ax].startDuty()
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
	for ax in ALL_AXES:
		if not h["axis.%s.enable" % ax]:
			continue
		cncc.setAxisPosition(ax, h["axis.%s.pos" % ax])

def deviceInitialize(h, cncc):
	# CNC-Control USB device connected. Initialize it.
	cncc.setDebugging(h["config.debug"], h["config.usblogmsg"])
	cncc.setTwohandEnabled(h["config.twohand"])
	for i in range(0, ControlMsgSetincrement.MAX_INDEX + 1):
		cncc.setIncrementAtIndex(i, h["jog.increment.%d" % i])
	axes = map(lambda ax: ax if h["axis.%s.enable" % ax] else "",
		   ALL_AXES)
	cncc.setEnabledAxes(filter(None, axes))

def createPins(h):
	# Device config
	h.newparam("config.twohand", HAL_BIT, HAL_RW)
	h.newparam("config.debug", HAL_U32, HAL_RW)
	h.newparam("config.usblogmsg", HAL_BIT, HAL_RW)

	# Machine state
	h.newpin("machine.is-on", HAL_BIT, HAL_IN)
	h.newpin("machine.estop-active", HAL_BIT, HAL_IN)

	# Axis
	for ax in ALL_AXES:
		h.newparam("axis.%s.enable" % ax, HAL_BIT, HAL_RW)
		h.newpin("axis.%s.pos" % ax, HAL_FLOAT, HAL_IN)

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

def eventloop(h, cncc):
	ctx = Context(h, cncc)
	while 1:
		try:
			if not cncc.probe():
				time.sleep(1)
				continue

			deviceInitialize(h, cncc)
			while 1: # Event loop
				if not cncc.eventWait():
					break
				updatePins(ctx)
		except (CNCCException), e:
			print "CNC-Control exception: " + str(e)

def main():
	try:
#		try:
#			os.nice(-15)
#		except (OSError), e:
#			print "WARNING: Failed to renice cnccontrol HAL module:", str(e)
		h = hal.component("cnccontrol")
		createPins(h)
		cncc = CNCControl(verbose=True)
		h.ready()
		eventloop(h, cncc)
	except (CNCCException), e:
		print "CNC-Control exception: " + str(e)
		return 1
	except (KeyboardInterrupt), e:
		return 0

if __name__ == "__main__":
	sys.exit(main())
