/*
 * INF templates for WinUSB/libusb automated driver installation
 * Copyright (c) 2010 Pete Batard <pbatard@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#pragma once

const char winusb_inf[] = "Date = \"03/08/2010\"\n\n" \
	"ProviderName = \"libusb 1.0\"\n" \
	"WinUSB_SvcDesc = \"WinUSB Driver Service\"\n" \
	"DiskName = \"libusb (WinUSB) Device Install Disk\"\n" \
	"ClassName = \"libusb (WinUSB) devices\"\n\n" \
	"[Version]\n" \
	"DriverVer = %Date%,1\n" \
	"Signature = \"$Windows NT$\"\n" \
	"Class = %ClassName%\n" \
	"ClassGuid = {78a1c341-4539-11d3-b88d-00c04fad5171}\n" \
	"Provider = %ProviderName%\n" \
	"CatalogFile = libusb_device.cat\n\n" \
	"[ClassInstall32]\n" \
	"Addreg = WinUSBDeviceClassReg\n\n" \
	"[WinUSBDeviceClassReg]\n" \
	"HKR,,,0,%ClassName%\n" \
	"HKR,,Icon,,-20\n\n" \
	"[Manufacturer]\n" \
	"%ProviderName% = libusbDevice_WinUSB,NTx86,NTamd64\n\n" \
	"[libusbDevice_WinUSB.NTx86]\n" \
	"%DeviceName% = USB_Install, USB\\%DeviceID%\n\n" \
	"[libusbDevice_WinUSB.NTamd64]\n" \
	"%DeviceName% = USB_Install, USB\\%DeviceID%\n\n" \
	"[USB_Install]\n" \
	"Include=winusb.inf\n" \
	"Needs=WINUSB.NT\n\n" \
	"[USB_Install.Services]\n" \
	"Include=winusb.inf\n" \
	"AddService=WinUSB,0x00000002,WinUSB_ServiceInstall\n\n" \
	"[WinUSB_ServiceInstall]\n" \
	"DisplayName     = %WinUSB_SvcDesc%\n" \
	"ServiceType     = 1\n" \
	"StartType       = 3\n" \
	"ErrorControl    = 1\n" \
	"ServiceBinary   = %12%\\WinUSB.sys\n\n" \
	"[USB_Install.Wdf]\n" \
	"KmdfService=WINUSB, WinUsb_Install\n\n" \
	"[WinUSB_Install]\n" \
	"KmdfLibraryVersion=1.9\n\n" \
	"[USB_Install.HW]\n" \
	"AddReg=Dev_AddReg\n\n" \
	"[Dev_AddReg]\n" \
	"HKR,,DeviceInterfaceGUIDs,0x10000,%DeviceGUID%\n\n" \
	"[USB_Install.CoInstallers]\n" \
	"AddReg=CoInstallers_AddReg\n" \
	"CopyFiles=CoInstallers_CopyFiles\n\n" \
	"[CoInstallers_AddReg]\n" \
	"HKR,,CoInstallers32,0x00010000,\"WdfCoInstaller01009.dll,WdfCoInstaller\",\"WinUSBCoInstaller2.dll\"\n\n" \
	"[CoInstallers_CopyFiles]\n" \
	"WinUSBCoInstaller2.dll\n" \
	"WdfCoInstaller01009.dll\n\n" \
	"[DestinationDirs]\n" \
	"CoInstallers_CopyFiles=11\n\n" \
	"[SourceDisksNames]\n" \
	"1 = %DiskName%,,,\\x86\n" \
	"2 = %DiskName%,,,\\amd64\n" \
	"3 = %DiskName%,,,\\ia64\n\n" \
	"[SourceDisksFiles.x86]\n" \
	"WinUSBCoInstaller2.dll=1\n" \
	"WdfCoInstaller01009.dll=1\n\n" \
	"[SourceDisksFiles.amd64]\n" \
	"WinUSBCoInstaller2.dll=2\n" \
	"WdfCoInstaller01009.dll=2\n\n" \
	"[SourceDisksFiles.ia64]\n";

const char libusb_inf[] = "NOT IMPLEMENTED YET\n";

const char* inf[2] = {winusb_inf, libusb_inf};
