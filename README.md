libwdi: A Windows Driver Installation library for USB devices
=============================================================

[![Build status](https://ci.appveyor.com/api/projects/status/j85bn7r8sds2coda?svg=true)](https://ci.appveyor.com/project/haata/libwdi)


Main features
-------------

* Automated inf creation, using reported USB device name
* Automated catalog file creation and signing, using autogenerated certificate
* Automated driver files extraction, for both 32 and 64 bit platforms
* Automated driver installation, including UAC elevation where necessary
* Single library embedding all the required files
* Supports all Windows platform from Windows XP to Windows 10

Additional features
-------------------

* Embedding of WinUSB, libusb0.sys or libusbK.sys, USB Serial (CDC) or your own 
  USB drivers (eg. WHQL)
* Full locale support with UTF-8 API strings and UTF-16 autogenerated inf files
* Resolution of USB Vendor IDs, based on the data maintained by Stephen J. Gowdy 
  at http://www.linux-usb.org/usb.ids
* Fully Open Source (LGPL v3), with multiple sample applications
* Supports MinGW32, MinGW-w64, Visual Studio, WDK

Installation and Compilation
----------------------------

See: https://github.com/pbatard/libwdi/wiki/Install

API usage
---------

See: https://github.com/pbatard/libwdi/wiki/Usage

FAQ
---

See: https://github.com/pbatard/libwdi/wiki/FAQ
