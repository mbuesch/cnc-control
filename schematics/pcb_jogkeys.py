#!/usr/bin/env python
"""
#
# CNC-control
# Jogkeys PCB drilling PYNC program
#
# Copyright (C) 2011 Michael Buesch <mb@bues.ch>
#
"""

from pync import *


DEPTH	= 0.3
FEED	= 80


G0(Z=5)
S(15000) -M3

def d(X, Y):
	G0(X=X, Y=Y, Z=5)
	G0(Z=0.5)
	G1(Z=-DEPTH) -F(FEED)
	G0(Z=5)

d(1.38, 10.06)
d(3.67, 10.06)
d(8.75, 10.06)
d(13.83, 7.52)
d(22.48, 8.83)
d(19.94, 13.88)
d(19.94, 16.17)
d(19.94, 21.25)
d(22.48, 26.33)
d(25.02, 26.33)
d(27.56, 28.62)
d(27.56, 21.25)
d(27.56, 16.17)
d(27.56, 11.12)
d(25.02, 8.83)
d(31.38, 10.06)
d(33.67, 7.52)
d(38.75, 10.06)
d(43.83, 10.06)
d(46.12, 2.44)
d(43.83, 2.44)
d(38.75, 2.44)
d(33.67, 4.98)
d(27.56, 3.75)
d(27.56, -1.33)
d(19.94, -3.62)
d(19.94, -1.33)
d(19.94, 3.75)
d(16.12, 2.44)
d(13.83, 4.98)
d(8.75, 2.44)
d(3.67, 2.44)
