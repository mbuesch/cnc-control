from descriptor_generator import *

device.set("bcdUSB",			USB_BCD_11)
device.set("bMaxPacketSize0",		16)
device.set("bcdDevice",			0x0100)
device.set("iManufacturer",		u"Michael Buesch")
device.set("iProduct",			u"CNC Remote Control")
device.set("iSerialNumber",		u"1")

config0 = Configuration(device)
config0.set("iConfiguration",		u"Configuration 1")
config0.set("bmAttributes",		USB_CONFIG_ATT_ONE)
config0.set("bMaxPower",		300 // 2)

interface0 = Interface(config0)
interface0.set("bAlternateSetting",	0)
interface0.set("bInterfaceClass",	USB_CLASS_VENDOR_SPEC)
interface0.set("iInterface",		u"Interface 1")

ep1in = Endpoint(interface0)
ep1in.set("bEndpointAddress",		1 | USB_ENDPOINT_IN)
ep1in.set("bmAttributes",		USB_ENDPOINT_XFER_INT)
ep1in.set("wMaxPacketSize",		16)
ep1in.set("bInterval",			20)

ep2in = Endpoint(interface0)
ep2in.set("bEndpointAddress",		2 | USB_ENDPOINT_IN)
ep2in.set("bmAttributes",		USB_ENDPOINT_XFER_BULK)
ep2in.set("wMaxPacketSize",		64)

ep2out = Endpoint(interface0)
ep2out.set("bEndpointAddress",		2 | USB_ENDPOINT_OUT)
ep2out.set("bmAttributes",		USB_ENDPOINT_XFER_BULK)
ep2out.set("wMaxPacketSize",		64)
