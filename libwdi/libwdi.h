/*
 * Library for WinUSB/libusb automated driver installation
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

#include <windows.h>
#if !defined(bool)
#define bool BOOL
#endif
#if !defined(true)
#define true TRUE
#endif
#if !defined(false)
#define false FALSE
#endif

#define USE_WINUSB                  0
#define USE_LIBUSB                  1

struct wdi_device_info {
	struct wdi_device_info *next;
	char* device_id;	// device URI
	char* desc;
	char* driver;
	char vid[9];
	char pid[9];
	char mi[6];
};

/*
 * returns a driver_info list of USB devices
 * parameter: driverless_only - boolean
 */
struct wdi_device_info* wdi_create_list(bool driverless_only);
void wdi_destroy_list(struct wdi_device_info* list);
int wdi_create_inf(struct wdi_device_info* drv_info, char* path, int type);
int wdi_run_installer(char *path, char *dev_inst);
int wdi_update_drivers(void);
