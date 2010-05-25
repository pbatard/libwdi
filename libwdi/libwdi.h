/*
 * Library for WinUSB/libusb automated driver installation
 * Copyright (c) 2010 Pete Batard <pbatard@gmail.com>
 * Parts of the code from libusb by Daniel Drake, Johannes Erdfelt et al.
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
 * Set the default calling convention to WINAPI (__stdcall)
 */
#if !defined(LIBWDI_API)
#define LIBWDI_API WINAPI
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Device information structure, use by libwdi functions
 */
struct wdi_device_info {
	/** (Optional) Pointer to the next element in the chained list. NULL if unused */
	struct wdi_device_info *next;
	/** USB VID */
	unsigned short vid;
	/** USB PID */
	unsigned short pid;
	/** Whether the USB device is composite */
	bool is_composite;
	/** (Optional) Composite USB interface number */
	unsigned char mi;
	/** USB Device description, usually provided by the device irself */
	char* desc;
	/** Windows' driver (service) name */
	char* driver;
	/** (Optional) Microsoft's device URI string. NULL if unused */
	char* device_id;
	/** (Optional) Microsoft's hardware ID string. NULL if unused */
	char* hardware_id;
};

/*
 * Type of driver to install
 */
enum wdi_driver_type {
	WDI_WINUSB,
	WDI_LIBUSB,
	WDI_NB_DRIVERS	// Total number of drivers in the enum
};

/*
 * Log level
 */
enum wdi_log_level {
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARNING,
	LOG_LEVEL_ERROR
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
	WDI_ERROR_USER_CANCEL = -14,

	/** Couldn't run installer with required privileges */
	WDI_ERROR_NEEDS_ADMIN = -15,

	/** Attempted to run the 32 bit installer on 64 bit */
	WDI_ERROR_WOW64 = -16,

	/** Bad inf syntax */
	WDI_ERROR_INF_SYNTAX = -17,

	/** Missing cat file */
	WDI_ERROR_CAT_MISSING = -18,

	/** System policy prevents the installation of unsigned drivers */
	WDI_ERROR_UNSIGNED = -19,

	/** Other error */
	WDI_ERROR_OTHER = -99

	/** IMPORTANT: when adding new values to this enum, remember to
	   update the wdi_strerror() function implementation! */
};

/*
 * Convert a libwdi error to a human readable error message
 */
const char* LIBWDI_API wdi_strerror(int errcode);

/*
 * Check if a specific driver is supported (embedded) in the current version of libwdi
 */
bool LIBWDI_API wdi_is_driver_supported(int driver_type);

/*
 * Return a wdi_device_info list of USB devices
 * parameter: driverless_only - boolean
 */
int LIBWDI_API wdi_create_list(struct wdi_device_info** list, bool driverless_only);

/*
 * Release a wdi_device_info list allocated by the previous call
 */
int LIBWDI_API wdi_destroy_list(struct wdi_device_info* list);

/*
 * Create an inf file for a specific device
 */
int LIBWDI_API wdi_create_inf(struct wdi_device_info* device_info, char* path,
							  char* inf_name, int driver_type);

/*
 * Install a driver for a specific device
 */
int LIBWDI_API wdi_install_driver(struct wdi_device_info* device_info,
								  char* path, char* inf_name);
/*
 * Set the log verobosity
 */
int LIBWDI_API wdi_set_log_level(int level);

/*
 * Set the Windows callback message for log notification
 */
int LIBWDI_API wdi_register_logger(HWND hWnd, UINT message);

/*
 * Unset the Windows callback message for log notification
 */
int LIBWDI_API wdi_unregister_logger(HWND hWnd);

/*
 * Read a log message after a log notification
 */
int LIBWDI_API wdi_read_logger(char* buffer, DWORD buffer_size, DWORD* message_size);

#ifdef __cplusplus
}
#endif
