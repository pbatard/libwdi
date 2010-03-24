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

#include "../installer/installer.h"

#define MAX_DESC_LENGTH             128
#define MAX_PATH_LENGTH             128
#define MAX_KEY_LENGTH              256
#define ERR_BUFFER_SIZE             256
#define MAX_GUID_STRING_LENGTH      40
#define USE_WINUSB                  0
#define USE_LIBUSB                  1

struct driver_info {
	struct driver_info *next;
	char* device_id;
	char* desc;
	char vid[9];
	char pid[9];
	char mi[6];
};

struct driver_info *list_driverless(void);
char* guid_to_string(const GUID guid);
int create_inf(struct driver_info* drv_info, char* path, int type);
int run_installer(char *path, char *dev_inst);
int update_drivers(void);
