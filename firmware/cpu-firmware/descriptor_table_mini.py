from descriptor_generator import *

device.set("bcdUSB",			USB_BCD_11)
device.set("bMaxPacketSize0",		16)
device.set("bcdDevice",			0x0100)
device.set("iProduct",			u"CNC Remote Control BOOT")

config0 = Configuration(device)
config0.set("bmAttributes",		USB_CONFIG_ATT_ONE)
config0.set("bMaxPower",		400 // 2)

interface0 = Interface(config0)
interface0.set("bAlternateSetting",	0)
interface0.set("bInterfaceClass",	USB_CLASS_VENDOR_SPEC)

ep2in = Endpoint(interface0)
ep2in.set("bEndpointAddress",		2 | USB_ENDPOINT_IN)
ep2in.set("bmAttributes",		USB_ENDPOINT_XFER_BULK)
ep2in.set("wMaxPacketSize",		64)

ep2out = Endpoint(interface0)
ep2out.set("bEndpointAddress",		2 | USB_ENDPOINT_OUT)
ep2out.set("bmAttributes",		USB_ENDPOINT_XFER_BULK)
ep2out.set("wMaxPacketSize",		64)
