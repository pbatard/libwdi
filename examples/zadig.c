/*
 * Zadig: Automated Driver Installer for USB devices (GUI version)
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

/*
 * WARNING: if any part of the resulting executable name contains "setup" or "instal(l)"
 * it will require UAC elevation on Vista and later, and, when run from a cygwin/MSYS
 * shell, will produce a "sh: Bad file number" message.
 * See the paragraph on Automatic Elevation at http://helpware.net/VistaCompat.htm
 */

#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <objbase.h>
#include <process.h>

#include "../libwdi/libwdi.h"
#include "resource.h"
#include "zadig.h"

#define INF_NAME "libusb_device.inf"
#define EX_STYLE    (WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE | WS_EX_STATICEDGE | WS_EX_APPWINDOW)
#define COMBO_STYLE (WS_CHILD | WS_VISIBLE | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP | CBS_NOINTEGRALHEIGHT)
#define NOT_DURING_INSTALL if (install_thread != NULL) return FALSE

/*
 * Globals
 */
HINSTANCE main_instance;
HWND hDeviceList;
HWND hDriver;
HWND hMain;
HWND hInfo;
HMENU hMenu;
char path[MAX_PATH];
char* driver_display_name[WDI_NB_DRIVERS] = { "WinUSB", "libusb0" };
int driver_type = WDI_NB_DRIVERS-1;
HANDLE install_thread = NULL;

/*
 * On screen logging
 */
void w_printf_v(const char *format, va_list args)
{
	char str[STR_BUFFER_SIZE];
	int size;

	size = safe_vsnprintf(str, STR_BUFFER_SIZE, format, args);
	if (size < 0) {
		str[STR_BUFFER_SIZE-1] = 0;
	}
	Edit_SetSel(hInfo, -1, -1);
	Edit_ReplaceSel(hInfo, str);
}

void w_printf(const char *format, ...)
{
	va_list args;

	va_start (args, format);
	w_printf_v(format, args);
	va_end (args);
}

/*
 * Populate the USB device list
 */
int display_devices(struct wdi_device_info* list)
{
	struct wdi_device_info *device;
	int index = -1;
	int junk;
	HDC hdc;
	SIZE size;
	LONG max_width = 0;

	hdc = GetDC(hDeviceList);
	junk = ComboBox_ResetContent(hDeviceList);

	for (device = list; device != NULL; device = device->next) {
		// The dropdown width needs to accomodate our text
		GetTextExtentPoint(hdc, device->desc, (int)strlen(device->desc)+1, &size);
		max_width = max(max_width, size.cx);

		index = ComboBox_AddString(hDeviceList, device->desc);
		if ((index != CB_ERR) && (index != CB_ERRSPACE)) {
			junk = ComboBox_SetItemData(hDeviceList, index, (LPARAM) device);
		} else {
			dprintf("could not populate dropdown list past device #%d\n", index);
		}
	}

	SendMessage(hDeviceList, CB_SETCURSEL, 0, 0);
	SendMessage(hDeviceList, CB_SETDROPPEDWIDTH, max_width, 0);

	return index;
}

/*
 * Get the device pointer of current selection
 */
struct wdi_device_info* get_selected_device(void)
{
	struct wdi_device_info *device = NULL;
	int index;
	index = (int) SendDlgItemMessage(hMain, IDC_DEVICELIST, CB_GETCURSEL, 0, 0);
	if (index != CB_ERR) {
		// Use the device pointers as dropdown values for easy access
		device = (struct wdi_device_info*) SendDlgItemMessage(hMain, IDC_DEVICELIST,
			CB_GETITEMDATA, index, 0);
	}
	return device;
}

/*
 * Thread that sends a device event notification back to our dialog after a delay
 */
void __cdecl notification_delay_thread(void* param)
{
	DWORD delay = (DWORD)(uintptr_t)param;
	Sleep(delay);
	PostMessage(hMain, UM_DEVICE_EVENT, 0, 0);
}

/*
 * Perform the driver installation
 * param: currently selected device (will be ignored if create new is selected)
 *
 * This is created as a thread as the call is blocking and we need to process
 * Windows messages to prevent application freezout
 */
void __cdecl install_driver_thread(void* param)
{
	struct wdi_device_info* device = (struct wdi_device_info*)(uintptr_t)param;
	static char str_buf[STR_BUFFER_SIZE];
	int tmp;

	if (IsDlgButtonChecked(hMain, IDC_CREATE) == BST_CHECKED) {
		device = calloc(1, sizeof(struct wdi_device_info));
		if (device != NULL) {
			GetDlgItemText(hMain, IDC_DEVICELIST, str_buf, STR_BUFFER_SIZE);
			device->desc = safe_strdup(str_buf);
			GetDlgItemText(hMain, IDC_VID, str_buf, STR_BUFFER_SIZE);
			// TODO: use custom scanf for hex
			if (sscanf(str_buf, "%4x", &tmp) != 1) {
				dprintf("could not convert VID string - aborting\n");
				return;
			}
			device->vid = (unsigned short)tmp;
			GetDlgItemText(hMain, IDC_PID, str_buf, STR_BUFFER_SIZE);
			if (sscanf(str_buf, "%4x", &tmp) != 1) {
				dprintf("could not convert PID string - aborting\n");
				return;
			}
			device->pid = (unsigned short)tmp;
			GetDlgItemText(hMain, IDC_MI, str_buf, STR_BUFFER_SIZE);
			if ( (strlen(str_buf) != 0)
			  && (sscanf(str_buf, "%2x", &tmp) == 1) ) {
				device->is_composite = true;
				device->mi = (unsigned char)tmp;
			} else {
				device->is_composite = false;
				device->mi = 0;
			}
		}
	}
	GetDlgItemText(hMain, IDC_FOLDER, path, MAX_PATH);
	if (wdi_create_inf(device, path, INF_NAME, driver_type) == WDI_SUCCESS) {
		dprintf("Extracted driver files to %s\n", path);
		toggle_busy();
		if (wdi_install_driver(device, path, INF_NAME) == WDI_SUCCESS) {
			dprintf("SUCCESS\n");
		} else {
			dprintf("DRIVER INSTALLATION FAILED\n");
		}
		toggle_busy();
	} else {
		dprintf("Could not create/extract files in %s\n", str_buf);
	}
	PostMessage(hMain, WM_DEVICECHANGE, 0, 0);	// Force a refresh
	PostMessage(hMain, WM_SETCURSOR, 0, 0);		// Needed to restore the cursor
	install_thread = NULL;
}

/*
 * The lengths you need to go through just to change a combobox style...
 */
void combo_breaker(DWORD type)
{
	RECT rect, rect2;
	POINT point;
	int junk;

	GetClientRect(hDeviceList, &rect);
	GetWindowRect(hDeviceList, &rect2);
	point.x = rect2.left;
	point.y = rect2.top;
	ScreenToClient(hMain, &point);
	junk = ComboBox_ResetContent(hDeviceList);
	DestroyWindow(hDeviceList);

	hDeviceList = CreateWindowEx(0, "COMBOBOX", "", COMBO_STYLE | type,
		point.x, point.y, rect.right, rect.bottom*((type==CBS_SIMPLE)?1:8),
		hMain, (HMENU)IDC_DEVICELIST, main_instance, NULL);
}

/*
 * Select the next available target driver
 */
bool select_next_driver(bool increment)
{
	int i;
	bool found = false;

	for (i=0; i<WDI_NB_DRIVERS; i++) {	// don't loop forever
		driver_type = (WDI_NB_DRIVERS + driver_type +
			(increment?1:-1))%WDI_NB_DRIVERS;
		if (!wdi_is_driver_supported(driver_type)) {
			continue;
		}
		found = true;
		break;
	}
	SetDlgItemText(hMain, IDC_TARGET_DRIVER,
		found?driver_display_name[driver_type]:"(NONE)");
	return found;
}

/*
 * Main dialog callback
 */
INT_PTR CALLBACK main_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	static struct wdi_device_info *device, *list = NULL;
	static bool list_driverless_only = true;
	static char* editable_desc = NULL;
	static HANDLE delay_thread = NULL;
	static DWORD last_scroll = 0;
	char str_tmp[5];
	char log_buf[STR_BUFFER_SIZE];
	int nb_devices, junk, r;
	DWORD delay, read_size;

	switch (message) {

	case WM_DEVICECHANGE:
		/*
		 * Why the convoluted process on device notification?
		 * 1. When not using RegisterDeviceNotification(), Windows sends an undefined number
		 * of WM_DEVICECHANGE events in rapid sequence, all with the exact SAME wParam/lParam
		 * so that we cannot differentiate between them. Notifying on each of those would
		 * bother the user too much.
		 * 2. When using RegisterDeviceNotification(), it is possible to get unique
		 * WM_DEVICECHANGE events but only for devices that already have a driver, because
		 * there is no device interface class for unknown/driverless devices and Microsoft
		 * has not publicized any way of doing so, it is NOT possible to get a single notifi-
		 * cation event for insertion/removal of devices that don't have a driver.
		 * Our solution then is to initiate delayed notification thread on the first
		 * WM_DEVICECHANGE message we receive, and wait for this thread to send a user defined
		 * event back to our main callback.
		 */
		if (delay_thread == NULL) {
			delay = NOTIFICATION_DELAY;
			delay_thread = (HANDLE)_beginthread(notification_delay_thread, 0, (void*)(uintptr_t)delay);
			if (delay_thread == NULL) {
				dprintf("Unable to create notification delay thread - notification events will be disabled\n");
			}
		}
		return TRUE;

	case UM_DEVICE_EVENT:
		delay_thread = NULL;
		// Don't handle these events when installation has started
		NOT_DURING_INSTALL;
		if (IsDlgButtonChecked(hMain, IDC_CREATE) == BST_CHECKED) {
			if (MessageBox(hMain, "The device list has changed.\n"
				"Do you want to refresh the list\n"
				"and lose all your modifications?", "Device Event Notification",
				MB_YESNO | MB_ICONINFORMATION) == IDYES) {
				CheckDlgButton(hMain, IDC_CREATE, BST_UNCHECKED);
				EnableWindow(GetDlgItem(hMain, IDC_DRIVERLESSONLY), true);
				combo_breaker(CBS_DROPDOWNLIST);
				PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
			}
		} else {
			PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
		}
		return TRUE;

	case UM_LOGGER_EVENT:
		// TODO: use different colours according to the log level?
//		dprintf("log level: %d\n", wParam);
		r = wdi_read_logger(log_buf, STR_BUFFER_SIZE, &read_size);
		if (r == WDI_SUCCESS) {
			dprintf("%s\n", log_buf);
		} else {
			dprintf("wdi_read_logger: error %s\n", wdi_strerror(r));
		}
		return TRUE;

	case WM_INITDIALOG:
		// Quite a burden to carry around as parameters
		hMain = hDlg;
		hDeviceList = GetDlgItem(hDlg, IDC_DEVICELIST);
		hDriver = GetDlgItem(hDlg, IDC_DRIVER);
		hInfo = GetDlgItem(hDlg, IDC_INFO);
		hMenu = GetSubMenu(GetMenu(hDlg), 0);

		// Initialize COM for folder selection
		CoInitialize(NULL);

		// Increase the size of our log textbox
		PostMessage(hInfo, EM_LIMITTEXT, 0xFFFF, 0);

		SetDlgItemText(hMain, IDC_FOLDER, DEFAULT_DIR);
		CheckDlgButton(hMain, IDC_DRIVERLESSONLY, list_driverless_only?BST_CHECKED:BST_UNCHECKED);
		// Try without... and lament for the lack of consistancy of MS controls.
		combo_breaker(CBS_DROPDOWNLIST);

		wdi_register_logger(hMain, UM_LOGGER_EVENT);
		wdi_set_log_level(LOG_LEVEL_DEBUG);

		// Fall through
	case UM_REFRESH_LIST:
		NOT_DURING_INSTALL;
		if (list != NULL) wdi_destroy_list(list);
		r = wdi_create_list(&list, list_driverless_only);
		if (r == WDI_SUCCESS) {
			nb_devices = display_devices(list);
			dprintf("%d device%s found.\n", nb_devices+1, (nb_devices>0)?"s":"");
			// Send a dropdown selection message to update fields
			PostMessage(hMain, WM_COMMAND, MAKELONG(IDC_DEVICELIST, CBN_SELCHANGE),
				(LPARAM) hDeviceList);
		} else {
			junk = ComboBox_ResetContent(hDeviceList);
			SetDlgItemText(hMain, IDC_VID, "");
			SetDlgItemText(hMain, IDC_PID, "");
			SetDlgItemText(hMain, IDC_MI, "");
			SetDlgItemText(hMain, IDC_DRIVER, "");
			EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), false);
			dprintf("No device found.\n");
		}
		return TRUE;

	case WM_VSCROLL:
		// TODO: ability to scroll existing driver text
		NOT_DURING_INSTALL;
		if (LOWORD(wParam) == 4) {
			if (!select_next_driver(HIWORD(wParam) <= last_scroll)) {
				dprintf("no driver is selectable in libwdi!");
			}
			last_scroll = HIWORD(wParam);
			return TRUE;
		}
		return FALSE;

	case WM_COMMAND:
		NOT_DURING_INSTALL;
		switch(LOWORD(wParam)) {
		case IDC_DRIVERLESSONLY:	// checkbox: "List Only Driverless Devices"
			list_driverless_only = (IsDlgButtonChecked(hMain, IDC_DRIVERLESSONLY) == BST_CHECKED);
			// Reset Edit button
			CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
			// Reset Combo
			combo_breaker(CBS_DROPDOWNLIST);
			PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
			break;
		case IDC_EDITNAME:			// checkbox: "Edit Device Name"
			if (IsDlgButtonChecked(hMain, IDC_EDITNAME) == BST_CHECKED) {
				combo_breaker(CBS_SIMPLE);
				if (device->desc != editable_desc) {
					editable_desc = malloc(STR_BUFFER_SIZE);
					if (editable_desc == NULL) {
						dprintf("could not use modified device description\n");
						editable_desc = device->desc;
					} else {
						safe_strcpy(editable_desc, STR_BUFFER_SIZE, device->desc);
						free(device->desc);	// No longer needed
						device->desc = editable_desc;
					}
				}
				junk = ComboBox_AddString(hDeviceList, editable_desc);
				SendMessage(hDeviceList, CB_SETCURSEL, 0, 0);
				PostMessage(hDeviceList, WM_SETFOCUS, 0, 0);
			} else {
				combo_breaker(CBS_DROPDOWNLIST);
				display_devices(list);
			}
			break;
		case IDC_CREATE:			// checkbox: "Non Listed Device (Create)"
			if (IsDlgButtonChecked(hMain, IDC_CREATE) == BST_CHECKED) {
				combo_breaker(CBS_SIMPLE);
				EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), false);
				EnableWindow(GetDlgItem(hMain, IDC_DRIVERLESSONLY), false);
				SetDlgItemText(hMain, IDC_VID, "");
				SetDlgItemText(hMain, IDC_PID, "");
				SetDlgItemText(hMain, IDC_MI, "");
				SetDlgItemText(hMain, IDC_DRIVER, "");
				EnableWindow(GetDlgItem(hMain, IDC_PID), true);
				EnableWindow(GetDlgItem(hMain, IDC_VID), true);
				EnableWindow(GetDlgItem(hMain, IDC_MI), true);
				PostMessage(hDeviceList, WM_SETFOCUS, 0, 0);
			} else {
				EnableWindow(GetDlgItem(hMain, IDC_PID), false);
				EnableWindow(GetDlgItem(hMain, IDC_VID), false);
				EnableWindow(GetDlgItem(hMain, IDC_MI), false);
				EnableWindow(GetDlgItem(hMain, IDC_DRIVERLESSONLY), true);
				PostMessage(hMain, WM_COMMAND, MAKELONG(IDC_DRIVERLESSONLY, CBN_SELCHANGE), 0);
			}
			break;
		case IDC_DEVICELIST:		// dropdown/field: device description
			switch (HIWORD(wParam)) {
			case CBN_SELCHANGE:
				device = get_selected_device();
				if (device != NULL) {
					// Change the description string if needed
					if (device->desc == NULL) {
						editable_desc = malloc(STR_BUFFER_SIZE);
						if (editable_desc == NULL) {
							dprintf("could not use modified device description\n");
							editable_desc = device->desc;
						} else {
							safe_sprintf(editable_desc, STR_BUFFER_SIZE, "(Unknown Device)");
							device->desc = editable_desc;
						}
					}
					if (device->driver != NULL) {
						SetDlgItemText(hMain, IDC_DRIVER, device->driver);
					} else {
						SetDlgItemText(hMain, IDC_DRIVER, "(NONE)");
					}
					driver_type = WDI_NB_DRIVERS-1;
					if (!select_next_driver(true)) {
						dprintf("no driver is selectable in libwdi!");
					}
					safe_sprintf(str_tmp, 5, "%04X", device->vid);
					SetDlgItemText(hMain, IDC_VID, str_tmp);
					safe_sprintf(str_tmp, 5, "%04X", device->pid);
					SetDlgItemText(hMain, IDC_PID, str_tmp);
					if (device->is_composite) {
						safe_sprintf(str_tmp, 5, "%02X", device->mi);
						SetDlgItemText(hMain, IDC_MI, str_tmp);
					}
					EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), true);
				}
				break;
			case CBN_EDITCHANGE:
				ComboBox_GetText(hDeviceList, editable_desc, STR_BUFFER_SIZE);
				break;
			default:
				return FALSE;
			}
			break;
		case IDC_INSTALL:	// button: Install
			if (install_thread != NULL) {
				dprintf("program assertion failed - another install thread is running\n");
			} else {
				install_thread = (HANDLE)_beginthread(install_driver_thread, 0, (void*)(uintptr_t)device);
				if (install_thread == NULL) {
					dprintf("unable to create install_thread\n");
				}
			}
			break;
		case IDC_BROWSE:	// button: Browse
			browse_for_folder();
			break;
		case IDC_CLEAR:
			Edit_SetText(hInfo, "");
			break;
		case IDOK:
		case IDCANCEL:
			wdi_destroy_list(list);
			EndDialog(hDlg, 0);
			break;
		// Menus
		case IDM_ABOUT:
			DialogBox(main_instance, MAKEINTRESOURCE(IDD_ABOUTBOX), hMain, About);
			break;
		case IDM_BASICMODE:
			CheckMenuItem(hMenu, IDM_BASICMODE, MF_CHECKED);
			CheckMenuItem(hMenu, IDM_ADVANCEDMODE, MF_UNCHECKED);
			// TODO: switch dialog
			break;
		case IDM_ADVANCEDMODE:
			CheckMenuItem(hMenu, IDM_ADVANCEDMODE, MF_CHECKED);
			CheckMenuItem(hMenu, IDM_BASICMODE, MF_UNCHECKED);
			// TODO: switch dialog
			break;
		default:
			return FALSE;
		}
		return TRUE;

	default:
		return FALSE;

	}
	// TODO: return TRUE on handled messages
	return FALSE;
}

/*
 * Application Entrypoint
 */
int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{

	// Save instance of the application for further reference
	main_instance = hInstance;

	// Create the main Window
	if (DialogBox(hInstance, "MAIN_DIALOG", NULL, main_callback) == -1) {
		MessageBox(NULL, "Could not create Window", "DialogBox failure", MB_ICONSTOP);
	}

	return (0);
}

