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

const char winusb_inf[] = "ProviderName = \"libusb 1.0\"\n" \
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
	"[SourceDisksFiles.x86]\n" \
	"WinUSBCoInstaller2.dll=1\n" \
	"WdfCoInstaller01009.dll=1\n\n" \
	"[SourceDisksFiles.amd64]\n" \
	"WinUSBCoInstaller2.dll=2\n" \
	"WdfCoInstaller01009.dll=2\n";

const char libusb_inf[] = "ProviderName = \"libusb-win32\"\n" \
	"libusb0SvcDesc = \"libusb-win32 Driver Service\"\n" \
	"DiskName = \"libusb-win32 Device Install Disk\"\n" \
	"ClassName = \"libusb-win32 devices\"\n\n" \
	"[Version]\n" \
	"DriverVer = %Date%,1\n" \
	"Signature = \"$Windows NT$\"\n" \
	"Class = %ClassName%\n" \
	"ClassGuid = {EB781AAF-9C70-4523-A5DF-642A87ECA567}\n" \
	"Provider = %ProviderName%\n" \
	"CatalogFile = libusb0_device.cat\n\n" \
	"[ClassInstall32]\n" \
	"Addreg = libusb0DeviceClassReg\n\n" \
	"[libusb0DeviceClassReg]\n" \
	"HKR,,,0,%ClassName%\n" \
	"HKR,,Icon,,-20\n\n" \
	"[Manufacturer]\n" \
	"%ProviderName% = libusb0Device,NTx86,NTamd64\n\n" \
	"[libusb0Device.NTx86]\n" \
	"%DeviceName% = LIBUSB0_DEV, USB\\%DeviceID%\n\n" \
	"[libusb0Device.NTamd64]\n" \
	"%DeviceName% = LIBUSB0_DEV, USB\\%DeviceID%\n\n" \
	";--------------------------------------------------------------------------\n" \
	"; Files\n" \
	";--------------------------------------------------------------------------\n\n" \
	"[SourceDisksNames]\n" \
	"1 = %DiskName%,,,\\x86\n" \
	"2 = %DiskName%,,,\\amd64\n" \
	"[SourceDisksFiles.x86]\n" \
	"libusb0.sys = 1\n" \
	"libusb0.dll = 1\n\n" \
	"[SourceDisksFiles.amd64]\n" \
	"libusb0.sys = 2\n" \
	"libusb0.dll = 2\n\n" \
	"[DestinationDirs]\n" \
	"libusb_files_sys = 10,system32\\drivers\n" \
	"libusb_files_sys_x64 = 10,system32\\drivers\n" \
	"libusb_files_dll = 10,system32\n" \
	"libusb_files_dll_wow64 = 10,syswow64\n" \
	"libusb_files_dll_x64 = 10,system32\n\n" \
	"[libusb_files_sys]\n" \
	"libusb0.sys\n\n" \
	"[libusb_files_sys_x64]\n" \
	"libusb0.sys\n\n" \
	"[libusb_files_dll]\n" \
	"libusb0.dll\n\n" \
	"[libusb_files_dll_wow64]\n" \
	"libusb0.dll\n\n" \
	"[libusb_files_dll_x64]\n" \
	"libusb0.dll\n\n" \
	";--------------------------------------------------------------------------\n" \
	"; Device driver\n" \
	";--------------------------------------------------------------------------\n\n" \
	"[LIBUSB0_DEV]\n" \
	"CopyFiles = libusb_files_sys, libusb_files_dll\n" \
	"AddReg    = libusb_add_reg\n\n" \
	"[LIBUSB0_DEV.NT]\n" \
	"CopyFiles = libusb_files_sys, libusb_files_dll\n\n" \
	"[LIBUSB0_DEV.NTAMD64]\n" \
	"CopyFiles = libusb_files_sys_x64, libusb_files_dll_wow64, libusb_files_dll_x64\n\n" \
	"[LIBUSB0_DEV.HW]\n" \
	"DelReg = libusb_del_reg_hw\n" \
	"AddReg = libusb_add_reg_hw\n\n" \
	"[LIBUSB0_DEV.NT.HW]\n" \
	"DelReg = libusb_del_reg_hw\n" \
	"AddReg = libusb_add_reg_hw\n\n" \
	"[LIBUSB0_DEV.NTAMD64.HW]\n" \
	"DelReg = libusb_del_reg_hw\n" \
	"AddReg = libusb_add_reg_hw\n\n" \
	"[LIBUSB0_DEV.NT.Services]\n" \
	"AddService = libusb0, 0x00000002, libusb_add_service\n\n" \
	"[LIBUSB0_DEV.NTAMD64.Services]\n" \
	"AddService = libusb0, 0x00000002, libusb_add_service\n\n" \
	"[libusb_add_reg]\n" \
	"HKR,,DevLoader,,*ntkern\n" \
	"HKR,,NTMPDriver,,libusb0.sys\n\n" \
	"; Older versions of this .inf file installed filter drivers. They are not\n" \
	"; needed any more and must be removed\n" \
	"[libusb_del_reg_hw]\n" \
	"HKR,,LowerFilters\n" \
	"HKR,,UpperFilters\n\n" \
	"; Device properties\n" \
	"[libusb_add_reg_hw]\n" \
	"HKR,,SurpriseRemovalOK, 0x00010001, 1\n\n" \
	";--------------------------------------------------------------------------\n" \
	"; Services\n" \
	";--------------------------------------------------------------------------\n\n" \
	"[libusb_add_service]\n" \
	"DisplayName    = %libusb0SvcDesc%\n" \
	"ServiceType    = 1\n" \
	"StartType      = 3\n" \
	"ErrorControl   = 0\n" \
	"ServiceBinary  = %12%\\libusb0.sys\n";

const char* inf[2] = {winusb_inf, libusb_inf};
