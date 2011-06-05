#!/usr/bin/env python
"""
#
# CNC-control
# Frontplate PYNC program
#
# Copyright (C) 2011 Michael Buesch <mb@bues.ch>
#
"""

from pync import *

r	= 1.19		# Tool radius
t	= 3.5		# Depth
s	= 1.8		# Step
FEED	= 100
ZFEED	= 80

G0(Z=5) -G64(0.05)
S(15000) -M3

def jogButtons(z):
	G0(X=25+r+2, Y=-18.25-r-2, Z=5)
	G0(Z=1)
	G1(Z=z) -F(ZFEED)
	G1(X=25+r, Y=-18.25-r) -F(FEED)
	G1(Y=-35.75-r)
	G1(X=20+r)
	G1(Y=-48.25+r)
	G1(X=25+r)
	G1(Y=-65.75+r)
	G1(X=37.5-r)
	G1(Y=-48.25+r)
	G1(X=55-r)
	G1(Y=-35.75-r)
	G1(X=37.5-r)
	G1(Y=-18.25-r)
	G1(X=25+r)
	G1(X=25+r+2, Y=-18.25-r-2)
	G0(Z=5)

def dials(z):
	G0(X=50, Y=-64)
	G0(Z=1)
	G1(Z=z) -F(ZFEED)
	G0(Z=5)
	G0(X=50, Y=-20)
	G0(Z=1)
	G1(Z=z)
	G0(Z=5)

def selectButtons(z):
	G0(X=63+r+2, Y=-10.75-r-2, Z=5)
	G0(Z=1)
	G1(Z=z) -F(ZFEED)
	G1(X=63+r, Y=-10.75-r) -F(FEED)
	G1(Y=-73.25+r)
	G1(X=80.5-r)
	G1(Y=-10.75-r)
	G1(X=63+r)
	G1(X=63+r+2, Y=-10.75-r-2)
	G0(Z=5)

def softKeys(z):
	G0(X=89+r+2, Y=-18.25-r-2, Z=5)
	G0(Z=1)
	G1(Z=z) -F(ZFEED)
	G1(X=89+r, Y=-18.25-r) -F(FEED)
	G1(Y=-35.75-r)
	G1(X=84+r)
	G1(Y=-48.25+r)
	G1(X=89+r)
	G1(Y=-65.75+r)
	G1(X=101.5-r)
	G1(Y=-18.25-r)
	G1(X=89+r)
	G1(X=89+r+2, Y=-18.25-r-2)
	G0(Z=5)

dials(-t)
z=0
while not equal(z, -t):
	z = max(-t, z-s)
	jogButtons(z)
	selectButtons(z)
	softKeys(z)
