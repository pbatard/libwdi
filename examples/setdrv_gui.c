/*
 * List and install driver for USB devices (GUI version)
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

#include "../libwdi/libwdi.h"

#include "resource.h"
#include "setdrv_gui.h"

#define dclear() SendDlgItemMessage(hMain, IDC_INFO, LB_RESETCONTENT, 0, 0)
#define dprintf(...) w_printf(IDC_INFO, __VA_ARGS__)

#define EX_STYLE    (WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE | WS_EX_STATICEDGE | WS_EX_APPWINDOW)
#define COMBO_STYLE (WS_CHILD | WS_VISIBLE | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP | CBS_NOINTEGRALHEIGHT)

/*
 * Globals
 */
static HINSTANCE main_instance;
static HWND hDeviceList;
static HWND hDriver;
static HWND hMain;

/*
 * On screen logging
 */
void w_printf_v(HWND hWnd, const char *format, va_list args)
{
	char str[STR_BUFFER_SIZE];
	int size;

	size = safe_vsnprintf(str, STR_BUFFER_SIZE, format, args);
	if (size < 0) {
		str[STR_BUFFER_SIZE-1] = 0;
	}
	SendMessage(hWnd, LB_ADDSTRING, 0, (LPARAM) str);
}

void w_printf(int dialog, const char *format, ...)
{
	va_list args;
	HWND hWnd;

	hWnd = GetDlgItem(hMain, dialog);

	va_start (args, format);
	w_printf_v(hWnd, format, args);
	va_end (args);
}

/*
 * Populate the USB device list
 */
int display_devices(struct wdi_device_info* list)
{
	struct wdi_device_info *device;
	int index = -1;

	ComboBox_ResetContent(hDeviceList);

	for (device = list; device != NULL; device = device->next) {
		index = ComboBox_AddString(hDeviceList, device->desc);
		if ((index != CB_ERR) && (index != CB_ERRSPACE)) {
			ComboBox_SetItemData(hDeviceList, index, (LPARAM) device);
		} else {
			dprintf("could not populate dropdown list past device #%d", index);
		}
	}

	SendMessage(hDeviceList, CB_SETCURSEL, 0, 0);

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
 * The lengths you need to go through just to change a combobox style...
 */
void combo_breaker(DWORD type)
{
	RECT rect, rect2;
	POINT point;

	// TODO: use GetComboBoxInfo()
	GetClientRect(hDeviceList, &rect);
	GetWindowRect(hDeviceList, &rect2);
	point.x = rect2.left;
	point.y = rect2.top;
	ScreenToClient(hMain, &point);
	ComboBox_ResetContent(hDeviceList);
	DestroyWindow(hDeviceList);

	hDeviceList = CreateWindowEx(0, "COMBOBOX", "", COMBO_STYLE | type,
		point.x, point.y, rect.right, rect.bottom*((type==CBS_SIMPLE)?1:8),
		hMain, (HMENU)IDC_DEVICELIST, main_instance, NULL);
}

// TODO: use DlgDirListComboBox for directory control

/*
 * Main dialog callback
 */
INT_PTR CALLBACK main_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
static struct wdi_device_info *device, *list = NULL;
static bool list_driverless_only = true;
static char folder[STR_BUFFER_SIZE];
static char* editable_desc = NULL;
char vidpid[5];

int nb_devices;

	// Quite a burden to carry around as parameters
	hMain = hDlg;
	hDeviceList = GetDlgItem(hDlg, IDC_DEVICELIST);
	hDriver = GetDlgItem(hDlg, IDC_DRIVER);

	switch (message) {

	case WM_INITDIALOG:
		SetDlgItemText(hMain, IDC_FOLDER, "C:\\test");
		CheckDlgButton(hMain, IDC_DRIVERLESSONLY, list_driverless_only?BST_CHECKED:BST_UNCHECKED);
		// Try without... and lament for the lack of consistancy of MS controls.
		combo_breaker(CBS_DROPDOWNLIST);

	case WM_APP:	// WM_APP is not sent on focus, unlike WM_USER
		dclear();
		if (list != NULL) wdi_destroy_list(list);
		list = wdi_create_list(list_driverless_only);
		if (list != NULL) {
			nb_devices = display_devices(list);
			dprintf("%d device%s found.", nb_devices+1, (nb_devices>0)?"s":"");
			// Send a dropdown selection message to update fields
			PostMessage(hMain, WM_COMMAND, MAKELONG(IDC_DEVICELIST, CBN_SELCHANGE),
				(LPARAM) hDeviceList);
		} else {
			ComboBox_ResetContent(hDeviceList);
			EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), false);
			dprintf("No devices found.");
		}
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam)) {
		case IDC_DRIVERLESSONLY:
			list_driverless_only = (IsDlgButtonChecked(hMain, IDC_DRIVERLESSONLY) == BST_CHECKED);
			// Reset Edit button
			CheckDlgButton(hMain, IDC_EDITNAME, 0);
			// Reset Combo
			combo_breaker(CBS_DROPDOWNLIST);
			PostMessage(hMain, WM_APP, 0, 0);
			break;
		case IDC_EDITNAME:
			if (IsDlgButtonChecked(hMain, IDC_EDITNAME) == BST_CHECKED) {
				combo_breaker(CBS_SIMPLE);
				if (device->desc != editable_desc) {
					editable_desc = malloc(STR_BUFFER_SIZE);
					if (editable_desc == NULL) {
						// TODO
					} else {
						safe_strcpy(editable_desc, STR_BUFFER_SIZE, device->desc);
						free(device->desc);	// No longer needed
						device->desc = editable_desc;
					}
				}
				ComboBox_AddString(hDeviceList, editable_desc);
				SendMessage(hDeviceList, CB_SETCURSEL, 0, 0);
			} else {
				combo_breaker(CBS_DROPDOWNLIST);
				display_devices(list);
			}
			break;
		case IDC_DEVICELIST:
			switch (HIWORD(wParam)) {
			case CBN_SELCHANGE:
				device = get_selected_device();
				if (device != NULL) {
					// Change the description string if needed
					if (device->desc == NULL) {
						editable_desc = malloc(STR_BUFFER_SIZE);
						if (editable_desc == NULL) {
							// TODO
						} else {
							safe_sprintf(editable_desc, STR_BUFFER_SIZE, "(Unknown Device)");
							device->desc = editable_desc;
						}
					}
					if (device->driver != NULL) {
						SendMessage(hDriver, WM_SETTEXT, 0, (LPARAM)device->driver);
					} else {
						SendMessage(hDriver, WM_SETTEXT, 0, (LPARAM)"(NONE)");
					}
					safe_sprintf(vidpid, 5, "%04X", device->vid);
					SetDlgItemText(hMain, IDC_VID, vidpid);
					safe_sprintf(vidpid, 5, "%04X", device->pid);
					SetDlgItemText(hMain, IDC_PID, vidpid);
					EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), true);
				}
				break;
			case CBN_EDITCHANGE:
				ComboBox_GetText(hDeviceList, editable_desc, STR_BUFFER_SIZE);
				break;
			default:
				break;
			}
			break;
		case IDC_INSTALL:
			GetDlgItemText(hMain, IDC_FOLDER, folder, STR_BUFFER_SIZE);
			if (wdi_create_inf(device, folder, WDI_WINUSB) == 0) {
				dprintf("Extracted driver files to %s", folder);
				if (wdi_install_driver(folder, device) == 0) {
					dprintf("SUCCESS");
				} else {
					dprintf("DRIVER INSTALLATION FAILED");
				}
			} else {
				dprintf("Could not create/extract files in %s", folder);
			}
			break;
		case IDOK:
		case IDCANCEL:
			wdi_destroy_list(list);
			EndDialog(hDlg, 0);
			break;
		default:
			break;
		}
		break;

	default:
		break;

	}
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


