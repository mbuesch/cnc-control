"""
 *   Tiny USB stack - Descriptor table generator
 *
 *   Copyright (C) 2009-2011 Michael Buesch <m@bues.ch>
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2 as published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
"""

import sys
import atexit


# bcdUSB
USB_BCD_10			= 0x0100
USB_BCD_11			= 0x0110
USB_BCD_20			= 0x0200

USB_TYPE_STANDARD		= 0x00 << 5
USB_TYPE_CLASS			= 0x01 << 5
USB_TYPE_VENDOR			= 0x02 << 5
USB_TYPE_RESERVED		= 0x03 << 5

# Descriptor types
USB_DT_DEVICE			= 0x01
USB_DT_CONFIG			= 0x02
USB_DT_STRING			= 0x03
USB_DT_INTERFACE		= 0x04
USB_DT_ENDPOINT			= 0x05

# HID descriptor types
HID_DT_HID			= USB_TYPE_CLASS | 0x01
HID_DT_REPORT			= USB_TYPE_CLASS | 0x02
HID_DT_PHYSICAL			= USB_TYPE_CLASS | 0x03

# Descriptor sizes per descriptor type
USB_DT_DEVICE_SIZE		= 18
USB_DT_CONFIG_SIZE		= 9
USB_DT_INTERFACE_SIZE		= 9
USB_DT_ENDPOINT_SIZE		= 7
USB_DT_ENDPOINT_AUDIO_SIZE	= 9
USB_DT_HUB_NONVAR_SIZE		= 7

# from config descriptor bmAttributes
USB_CONFIG_ATT_ONE		= 1 << 7	# must be set
USB_CONFIG_ATT_SELFPOWER	= 1 << 6	# self powered
USB_CONFIG_ATT_WAKEUP		= 1 << 5	# can wakeup
USB_CONFIG_ATT_BATTERY		= 1 << 4	# battery powered

# Device and/or Interface Class codes
USB_CLASS_PER_INTERFACE		= 0	# for DeviceClass
USB_CLASS_AUDIO			= 1
USB_CLASS_COMM			= 2
USB_CLASS_HID			= 3
USB_CLASS_PRINTER		= 7
USB_CLASS_PTP			= 6
USB_CLASS_MASS_STORAGE		= 8
USB_CLASS_HUB			= 9
USB_CLASS_DATA			= 10
USB_CLASS_VENDOR_SPEC		= 0xff

# EP directions
USB_ENDPOINT_IN			= 0x80
USB_ENDPOINT_OUT		= 0x00

# EP bmAttributes
USB_ENDPOINT_XFERTYPE_MASK	= 0x03
USB_ENDPOINT_XFER_CONTROL	= 0
USB_ENDPOINT_XFER_ISOC		= 1
USB_ENDPOINT_XFER_BULK		= 2
USB_ENDPOINT_XFER_INT		= 3
USB_ENDPOINT_MAX_ADJUSTABLE	= 0x80



class Descriptor(object):
	STRDESC = 0xFF

	def __init__(self, parentDevice=None):
		self.parentDevice = parentDevice
		self.attributes = {}
		self.index = 0

	def getParentDevice(self):
		if self.parentDevice:
			return self.parentDevice
		return self

	@staticmethod
	def name2type(name):
		if name.startswith("bcd") or\
		   name.startswith("id") or\
		   name.startswith("w"):
			return 16
		if name.startswith("i"):
			return Descriptor.STRDESC
		if name.startswith("bm") or\
		   name.startswith("b"):
			return 8
		raise Exception("Could not determine type from name '%s'" % name)

	def __createValue(self, name, value):
		if self.name2type(name) == Descriptor.STRDESC:
			sd = StringDescriptor(value)
			self.getParentDevice().addStringDescriptor(sd)
			return sd
		return value

	def __destroyValue(self, name, value):
		if self.name2type(name) == Descriptor.STRDESC:
			self.getParentDevice().removeStringDescriptor(value)

	def alloc(self, name, value):
		if name in self.attributes:
			raise Exception("Attribute " + name + " double allocation")
		self.attributes[name] = [self.index, self.__createValue(name, value)]
		self.index += 1

	def set(self, name, value):
		if not name in self.attributes:
			raise Exception("Attribute " + name + " not found")
		self.__destroyValue(name, self.attributes[name][1])
		self.attributes[name][1] = self.__createValue(name, value)

	def getValue(self, name):
		if not name in self.attributes:
			raise Exception("Attribute " + name + " not found")
		(index, value) = self.attributes[name]
		return value

	def getList(self):
		# Returns a list of (name, value) tuples.
		l = [ None ] * len(self.attributes)
		for attrName in self.attributes:
			(index, value) = self.attributes[attrName]
			l[index] = (attrName, value)
		return l

class StringDescriptor(object):
	def __init__(self, string=None, ccode=None, strId=None):
		if ccode is None:
			ccode = []
			uni = string.encode("UTF-16-LE")
			for i in range(0, len(uni), 2):
				pfx = ""
				if i != 0 and i % 8 == 0:
					pfx = "\n\t"
				ccode.append("%s0x%02X, 0x%02X" %\
					(pfx, ord(uni[i]), ord(uni[i+1])))
			self.ccode = ", ".join(ccode)
			self.text = string
		else:
			self.ccode = ccode
			self.text = ccode
		self.strId = strId

	def getId(self):
		return self.strId

	def setId(self, strId):
		self.strId = strId

	def getText(self):
		return self.text

	def getCCode(self):
		return self.ccode

class Device(Descriptor):
	def __init__(self):
		Descriptor.__init__(self)
		self.configs = []
		self.stringDescs = []

		# Only one language for now
		langSd = StringDescriptor(ccode="0x09, 0x04", strId=0) # Language ID (US)
		self.addStringDescriptor(langSd)

		self.alloc("bLength", USB_DT_DEVICE_SIZE)
		self.alloc("bDescriptorType", USB_DT_DEVICE)
		self.alloc("bcdUSB", USB_BCD_11)
		self.alloc("bDeviceClass", 0)
		self.alloc("bDeviceSubClass", 0)
		self.alloc("bDeviceProtocol", 0)
		self.alloc("bMaxPacketSize0", 0)
		self.alloc("idVendor", 0x6666)
		self.alloc("idProduct", 0x1337)
		self.alloc("bcdDevice", 0)
		self.alloc("iManufacturer", "")
		self.alloc("iProduct", "")
		self.alloc("iSerialNumber", "")
		self.alloc("bNumConfigurations", 0)

	def addConfiguration(self, configuration):
		self.configs.append(configuration)

	def addStringDescriptor(self, sd):
		self.stringDescs.append(sd)

	def removeStringDescriptor(self, sd):
		self.stringDescs.remove(sd)

	def __setStringDescriptorIds(self):
		nextId = 1
		for sd in self.stringDescs:
			if sd.getId() is not None:
				continue
			sd.setId(nextId)
			nextId += 1

	def __autoConfig(self):
		self.set("bNumConfigurations", len(self.configs))
		configNr = 1
		interfNr = 0
		for config in self.configs:
			totalLength = config.getValue("bLength")
			config.set("bConfigurationValue", configNr)
			configNr += 1
			config.set("bNumInterfaces", len(config.interfaces))
			for interface in config.interfaces:
				totalLength += interface.getValue("bLength")
				interface.set("bInterfaceNumber", interfNr)
				interfNr += 1
				interface.set("bNumEndpoints", len(interface.endpoints))
				for ep in interface.endpoints:
					totalLength += ep.getValue("bLength")
				if interface.hiddevice:
					totalLength += interface.hiddevice.getValue("bLength")
			config.set("wTotalLength", totalLength)

	def __attrParse(self, attr):
		(name, value) = attr
		attrType = Descriptor.name2type(name)
		if attrType == Descriptor.STRDESC:
			sd = value
			value = "0x%02X, " % sd.getId()
		elif attrType == 8:
			value = "0x%02X, " % int(value)
		elif attrType == 16:
			value = int(value)
			value = "0x%02X, 0x%02X, " % (value & 0xFF, (value >> 8) & 0xFF)
		else:
			raise Exception("Unknown type (%s)" % str(attrType))
		return (name, attrType, value)

	def __attrDump(self, attrList):
		s = []
		count = 0
		for attr in attrList:
			(name, attrType, value) = self.__attrParse(attr)
			s.append(value)
			if attrType == 16:
				count += 2
			else:
				count += 1
			if count % 10 == 0 and count != 0:
				s.append("\n\t")
		return ("".join(s), count)

	def __repr__(self):
		self.__setStringDescriptorIds()
		self.__autoConfig()
		s = [ "/*** THIS FILE IS GENERATED. DO NOT EDIT! ***/\n\n" ]
		s.append("struct descriptor_ptr {\n")
		s.append("\tconst void * USB_PROGMEM ptr;\n")
		s.append("\tuint8_t size;\n")
		s.append("};\n\n")
		s.append("static const uint8_t USB_PROGMEM device_descriptor[] = {\n\t")
		(string, cnt) = self.__attrDump(self.getList())
		s.append(string)
		s.append("\n};\n\n")

		configNr = 0
		for config in self.configs:
			count = 0
			s.append("static const uint8_t USB_PROGMEM config%d_descriptor[] = {\n\t" % configNr)
			(string, cnt) = self.__attrDump(config.getList())
			s.append(string)
			count += cnt
			s.append("\n")
			for interface in config.interfaces:
				s.append("\t/* Interface */\n\t")
				(string, cnt) = self.__attrDump(interface.getList())
				s.append(string)
				count += cnt
				s.append("\n")
				if interface.hiddevice:
					s.append("\t/* HID Device */\n\t")
					(string, cnt) = self.__attrDump(interface.hiddevice.getList())
					s.append(string)
					s.append("\n\t#define HID_DEVICE_DESC_OFFSET\t%d" % count)
					count += cnt
					s.append("\n")
				for ep in interface.endpoints:
					s.append("\t/* Endpoint */\n\t")
					(string, cnt) = self.__attrDump(ep.getList())
					s.append(string)
					count += cnt
					s.append("\n")
			s.append("};\n\n")
			configNr += 1

		s.append("static const struct descriptor_ptr USB_PROGMEM config_descriptor_ptrs[] = {\n")
		for i in range(0, len(self.configs)):
			s.append("\t{\n")
			s.append("\t\t.ptr\t= config%d_descriptor,\n" % i)
			s.append("\t\t.size\t= sizeof(config%d_descriptor),\n" % i)
			s.append("\t},\n")
		s.append("};\n\n")

		for sd in self.stringDescs:
			s.append("\n/* %d: " % sd.getId())
			s.append(sd.getText())
			s.append(" */\n")
			s.append("static const char USB_PROGMEM string%d_descriptor[] = {\n" % sd.getId())
			s.append("\t%s\n};\n" % sd.getCCode())
		s.append("\n")

		s.append("static const struct descriptor_ptr USB_PROGMEM string_descriptor_ptrs[] = {\n")
		for sd in self.stringDescs:
			s.append("\t{\n")
			s.append("\t\t.ptr\t= string%d_descriptor,\n" % sd.getId())
			s.append("\t\t.size\t= sizeof(string%d_descriptor),\n" % sd.getId())
			s.append("\t},\n")
		s.append("};\n\n")

		return "".join(s)

	def dump(self):
		print self

class Configuration(Descriptor):
	def __init__(self, device):
		Descriptor.__init__(self, device)
		self.device = device
		device.addConfiguration(self)
		self.interfaces = []

		self.alloc("bLength", USB_DT_CONFIG_SIZE)
		self.alloc("bDescriptorType", USB_DT_CONFIG)
		self.alloc("wTotalLength", 0)
		self.alloc("bNumInterfaces", 0)
		self.alloc("bConfigurationValue", 0)
		self.alloc("iConfiguration", "")
		self.alloc("bmAttributes", 0)
		self.alloc("bMaxPower", 500 / 2)

	def addInterface(self, interface):
		self.interfaces.append(interface)

class Interface(Descriptor):
	def __init__(self, configuration):
		Descriptor.__init__(self, configuration.getParentDevice())
		self.configuration = configuration
		configuration.addInterface(self)
		self.endpoints = []
		self.hiddevice = None

		self.alloc("bLength", USB_DT_INTERFACE_SIZE)
		self.alloc("bDescriptorType", USB_DT_INTERFACE)
		self.alloc("bInterfaceNumber", 0)
		self.alloc("bAlternateSetting", 0)
		self.alloc("bNumEndpoints", 0)
		self.alloc("bInterfaceClass", 0)
		self.alloc("bInterfaceSubClass", 0)
		self.alloc("bInterfaceProtocol", 0)
		self.alloc("iInterface", "")

	def addEndpoint(self, endpoint):
		self.endpoints.append(endpoint)

	def addHIDDevice(self, hidDevice):
		if (self.hiddevice):
			raise Exception("Only one HID device")
		self.hiddevice = hidDevice

class Endpoint(Descriptor):
	def __init__(self, interface):
		Descriptor.__init__(self, interface.getParentDevice())
		self.interface = interface
		interface.addEndpoint(self)

		self.alloc("bLength", USB_DT_ENDPOINT_SIZE)
		self.alloc("bDescriptorType", USB_DT_ENDPOINT)
		self.alloc("bEndpointAddress", 0)
		self.alloc("bmAttributes", 0)
		self.alloc("wMaxPacketSize", 0)
		self.alloc("bInterval", 0)

class HIDDevice(Descriptor):
	def __init__(self, interface):
		Descriptor.__init__(self, interface.getParentDevice())
		self.interface = interface
		interface.addHIDDevice(self)

		self.alloc("bLength", 9)
		self.alloc("bDescriptorType", HID_DT_HID)
		self.alloc("bcdHID", 0x0111)
		self.alloc("bCountryCode", 0)
		self.alloc("bNumDescriptors", 0)
		self.alloc("bClassDescriptorType", 0)
		self.alloc("wClassDescriptorLength", 0)

device = Device()
try:
	device.set("idVendor", int(sys.argv[1], 16))
	device.set("idProduct", int(sys.argv[2], 16))
except (ValueError, IndexError), e:
	pass

atexit.register(device.dump)
