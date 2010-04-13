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

/*
 * Device information structure, use by libwdi functions
 */
struct wdi_device_info {
	/** Pointer to the next element in the chained list */
	struct wdi_device_info *next;
	/** Microsoft's device URI string */
	char* device_id;
	/** Yet another Microsoft ID */
	char* hardware_id;
	/** USB Device description, usually provided by the device irself */
	char* desc;
	/** Windows' driver (service) name */
	char* driver;
	/** USB VID */
	unsigned short vid;
	/** USB PID */
	unsigned short pid;
	/** Optional USB Interface Number. Negative if none */
	short mi;
};

/*
 * Type of driver to install
 */
enum wdi_driver_type {
	WDI_WINUSB,
	WDI_LIBUSB		// NOT IMPLEMENTED!
};

/*
 * Log level
 */
enum usbi_log_level {
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARNING,
	LOG_LEVEL_ERROR,
};

/*
 * Error codes. Most libwdi functions return 0 on success or one of these
 * codes on failure.
 * You can use wdi_strerror() to retrieve a short string description of
 * a wdi_error enumeration value.
 */
enum wdi_error {
	/** Success (no error) */
	WDI_SUCCESS = 0,

	/** Input/output error */
	WDI_ERROR_IO = -1,

	/** Invalid parameter */
	WDI_ERROR_INVALID_PARAM = -2,

	/** Access denied (insufficient permissions) */
	WDI_ERROR_ACCESS = -3,

	/** No such device (it may have been disconnected) */
	WDI_ERROR_NO_DEVICE = -4,

	/** Entity not found */
	WDI_ERROR_NOT_FOUND = -5,

	/** Resource busy */
	WDI_ERROR_BUSY = -6,

	/** Operation timed out */
	WDI_ERROR_TIMEOUT = -7,

	/** Overflow */
	WDI_ERROR_OVERFLOW = -8,

	/** Another installation is pending */
	WDI_ERROR_PENDING_INSTALLATION = -9,

	/** System call interrupted (perhaps due to signal) */
	WDI_ERROR_INTERRUPTED = -10,

	/** Could not acquire resource (Insufficient memory, etc) */
	WDI_ERROR_RESOURCE = -11,

	/** Operation not supported or unimplemented on this platform */
	WDI_ERROR_NOT_SUPPORTED = -12,

	/** Entity already exists */
	WDI_ERROR_EXISTS = -13,

	/** Cancelled by user */
	WDI_USER_CANCEL = -14,

	/** Other error */
	WDI_ERROR_OTHER = -99

	/* IMPORTANT: when adding new values to this enum, remember to
	   update the wdi_strerror() function implementation! */
};

/*
 * returns a driver_info list of USB devices
 * parameter: driverless_only - boolean
 */
const char* wdi_strerror(enum wdi_error errcode);
struct wdi_device_info* wdi_create_list(bool driverless_only);
void wdi_destroy_list(struct wdi_device_info* list);
int wdi_create_inf(struct wdi_device_info* device_info, char* path, enum wdi_driver_type type);
int wdi_install_driver(char *path, struct wdi_device_info* device_info);
DWORD wdi_read_logger(char* buffer, DWORD length);
int wdi_register_logger(HWND hWnd, UINT message);