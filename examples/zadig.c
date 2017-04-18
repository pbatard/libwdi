/*
 * Zadig: Automated Driver Installer for USB devices (GUI version)
 * Copyright (c) 2010-2017 Pete Batard <pete@akeo.ie>
 * For more info, please visit http://libwdi.akeo.ie
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * WARNING: if any part of the resulting executable name contains "setup" or "instal(l)"
 * it will require UAC elevation, and, when run from a MSYS shell, will produce a
 * "sh: Bad file number" message.
 * See the paragraph on Automatic Elevation at http://helpware.net/VistaCompat.htm
 */

/* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <objbase.h>
#include <process.h>
#include <shellapi.h>
#include <commctrl.h>

#include "libwdi.h"
#include "msapi_utf8.h"
#include "zadig_resource.h"
#include "zadig_registry.h"
#include "zadig.h"
#include "profile.h"
#include "stdfn.h"

#define NOT_DURING_INSTALL if (installation_running) return (INT_PTR)TRUE

void toggle_driverless(BOOL refresh);
void set_install_button(void);
BOOL parse_ini(void);

/*
 * Globals
 */
HINSTANCE main_instance;
HWND hDeviceList;
HWND hMain;
HWND hInfo;
HWND hStatus;
HWND hVIDToolTip = NULL, hArrowToolTip = NULL;
HWND hArrow;
HMENU hMenuDevice;
HMENU hMenuOptions;
HMENU hMenuLogLevel;
HMENU hMenuSplit;
HICON hIconTickOK, hIconTickNOK, hIconTickOKU, hIconFolder, hIconReport;
HICON hIconArrowGreen, hIconArrowOrange, hIconFilter;
POINT arrow_origin;
LONG arrow_width, arrow_height;
HFONT hyperlink_font, bold_font;
WNDPROC original_wndproc;
COLORREF arrow_color = ARROW_GREEN;
float fScale = 1.0f;
WORD application_version[4];
char app_dir[MAX_PATH], driver_text[64];
char extraction_path[MAX_PATH];
const char* driver_display_name[WDI_NB_DRIVERS] = { "WinUSB", "libusb-win32", "libusbK", "USB Serial (CDC)", "Custom (extract only)" };
const char* driver_name[WDI_NB_DRIVERS-1] = { "WinUSB", "libusb0", "libusbK", "usbser" };
struct wdi_options_create_list cl_options = { 0 };
struct wdi_options_prepare_driver pd_options = { 0 };
struct wdi_options_install_cert ic_options = { 0 };
struct wdi_options_install_driver id_options = { 0 };
struct wdi_device_info *device, *list = NULL;
int current_device_index = CB_ERR;
char* current_device_hardware_id = NULL;
char* editable_desc = NULL;
int default_driver_type = WDI_WINUSB;
int log_level = WDI_LOG_LEVEL_DEBUG;
int nb_devices = -1;
int dialog_showing = 0;
// Application states
BOOL advanced_mode = FALSE;
BOOL create_device = FALSE;
BOOL replace_driver = FALSE;
BOOL extract_only = FALSE;
BOOL from_install = FALSE;
BOOL installation_running = FALSE;
BOOL unknown_vid = FALSE;
BOOL has_filter_driver = FALSE;
BOOL use_arrow_icons = FALSE;
BOOL exit_on_success = FALSE;
enum wcid_state has_wcid = WCID_NONE;
int wcid_type = WDI_USER;
UINT64 target_driver_version = 0;

/*
 * On screen logging and status
 */
void w_printf_v(BOOL update_status, const char *format, va_list args)
{
	char str[STR_BUFFER_SIZE+2];
	int size;
	size_t slen;

	size = safe_vsnprintf(str, STR_BUFFER_SIZE, format, args);
	if (size < 0) {
		str[STR_BUFFER_SIZE-1] = 0;
		str[STR_BUFFER_SIZE-2] = ']';
		str[STR_BUFFER_SIZE-3] = str[STR_BUFFER_SIZE-4] = str[STR_BUFFER_SIZE-5] = '.';
		str[STR_BUFFER_SIZE-6] = '[';
	}
	slen = safe_strlen(str);
	str[slen] = '\r';
	str[slen+1] = '\n';
	str[slen+2] = 0;

	// Also send output to debug logger
	OutputDebugStringA(str);
	// Set cursor to the end of the buffer
	Edit_SetSel(hInfo, MAX_LOG_SIZE, MAX_LOG_SIZE);
	Edit_ReplaceSelU(hInfo, str);
	if (update_status) {
		SetDlgItemTextU(hMain, IDC_STATUS, str);
	}
}

void w_printf(BOOL update_status, const char *format, ...)
{
	va_list args;

	va_start (args, format);
	w_printf_v(update_status, format, args);
	va_end (args);
}

/*
 * Display a message on the status bar. If duration is non zero, ensures that message
 * is displayed for at least duration ms, regardless of any other incoming message
 */
static BOOL bStatusTimerArmed = FALSE;
char szStatusMessage[256] = { 0 };
static void CALLBACK PrintStatusTimeout(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	bStatusTimerArmed = FALSE;
	// potentially display lower priority message that was overridden
	SetDlgItemTextU(hMain, IDC_STATUS, szStatusMessage);
	KillTimer(hMain, TID_MESSAGE);
}

void print_status(unsigned int duration, BOOL debug, const char* message)
{
	if (message == NULL)
		return;
	safe_strcpy(szStatusMessage, sizeof(szStatusMessage), message);
	if (debug)
		dprintf("%s\n", szStatusMessage);

	if ((duration) || (!bStatusTimerArmed)) {
		SetDlgItemTextU(hMain, IDC_STATUS, szStatusMessage);
	}

	if (duration) {
		SetTimer(hMain, TID_MESSAGE, duration, PrintStatusTimeout);
		bStatusTimerArmed = TRUE;
	}
}

/*
 * Populate the USB device list
 */
int display_devices(void)
{
	struct wdi_device_info *dev;
	int index = -1;
	HDC hdc;
	SIZE size;
	LONG max_width = 0;

	hdc = GetDC(hDeviceList);
	_IGNORE(ComboBox_ResetContent(hDeviceList));

	for (dev = list; dev != NULL; dev = dev->next) {
		// Compute the width needed to accommodate our text
		if (dev->desc == NULL) {
			dprintf("error: device description is empty");
			break;
		}
		if (!GetTextExtentPointU(hdc, dev->desc, &size)) {
			dprintf("error: could not compute dropdown size of '%s' - %s", dev->desc, WindowsErrorString());
			continue;
		}
		max_width = max(max_width, size.cx);

		index = ComboBox_AddStringU(hDeviceList, dev->desc);
		if ((index != CB_ERR) && (index != CB_ERRSPACE)) {
			_IGNORE(ComboBox_SetItemData(hDeviceList, index, (LPARAM)dev));
		} else {
			dprintf("error: could not populate dropdown list for device '%s'", dev->desc);
		}

		// Select by Hardware ID if one's available
		if (safe_strcmp(current_device_hardware_id, dev->hardware_id) == 0) {
			current_device_index = index;
			safe_free(current_device_hardware_id);
		}
	}
	ReleaseDC(hDeviceList, hdc);

	// Select current entry
	if (current_device_index == CB_ERR) {
		current_device_index = 0;
	}
	_IGNORE(ComboBox_SetCurSel(hDeviceList, current_device_index));
	// Set the width to computed value
	SendMessage(hDeviceList, CB_SETDROPPEDWIDTH, max_width, 0);

	return index;
}

/*
 * Get the device pointer of current selection
 */
struct wdi_device_info* get_selected_device(void)
{
	struct wdi_device_info *dev = NULL;

	current_device_index = ComboBox_GetCurSel(hDeviceList);
	if (current_device_index != CB_ERR) {
		// Use the device pointers as dropdown values for easy access
		dev = (struct wdi_device_info*)ComboBox_GetItemData(hDeviceList, current_device_index);
	}
	return dev;
}

/*
 * This thread is used to send an event notification back to our app
 * param: a DWORD delay, in ms
 */
void __cdecl notification_delay_thread(void* param)
{
	DWORD delay = (DWORD)(uintptr_t)param;
	Sleep(delay);
	PostMessage(hMain, UM_DEVICE_EVENT, 0, 0);
	_endthread();
}

// Retrieve the driver type according to its service string
int get_driver_type(struct wdi_device_info* dev)
{
	int i;
	const char* libusb_name[] = { "WinUSB", "libusb0", "libusbK", "usbser" };
	const char* system_name[] = { "usbccgp", "usbstor", "uaspstor", "vusbstor", "etronstor", "hidusb",
		// NOTE: The list of hubs below should match the one from libwdi.c
		"usbhub", "usbhub3", "nusb3hub", "usbhub", "usbhub3", "usb3hub", "nusb3hub", "rusb3hub",
		"flxhcih", "tihub3", "etronhub3", "viahub3", "asmthub3", "iusb3hub", "vusb3hub", "amdhub30", "vhhub" };

	if ((dev == NULL) || (dev->driver == NULL)) {
		return DT_NONE;
	}
	for (i=0; i<ARRAYSIZE(libusb_name); i++) {
		if (safe_strcmp(dev->driver, libusb_name[i]) == 0) {
			return DT_LIBUSB;
		}
	}
	for (i=0; i<ARRAYSIZE(system_name); i++) {
		if (safe_stricmp(dev->driver, system_name[i]) == 0) {
			return DT_SYSTEM;
		}
	}
	return DT_UNKNOWN;
}

/*
 * Perform the driver installation
 */
int install_driver(void)
{
	struct wdi_device_info* dev = device;
	char str_buf[STR_BUFFER_SIZE];
	char* inf_name = NULL;
	BOOL need_dealloc = FALSE;
	int tmp, r = WDI_ERROR_OTHER;

	if ( (dev == NULL) && (!extract_only) && (!pd_options.use_wcid_driver)
	  && (!(GetMenuState(hMenuDevice, IDM_CREATE, MF_CHECKED) & MF_CHECKED)) ) {
		return WDI_ERROR_NO_DEVICE;
	}

	installation_running = TRUE;
	if ( (GetMenuState(hMenuDevice, IDM_CREATE, MF_CHECKED) & MF_CHECKED)
	  || (pd_options.use_wcid_driver) ) {
		// If the device is created from scratch, override the existing device
		dev = (struct wdi_device_info*)calloc(1, sizeof(struct wdi_device_info));
		if (dev == NULL) {
			dprintf("could not create new device_info struct for installation");
			r = WDI_ERROR_RESOURCE; goto out;
		}
		need_dealloc = TRUE;

		if (pd_options.use_wcid_driver) {
			dev->desc = (char*)malloc(128);
			if (dev->desc == NULL) {
				r = WDI_ERROR_RESOURCE;
				goto out;
			}
			safe_sprintf(dev->desc, 128, "%s Generic Device", driver_display_name[pd_options.driver_type]);
		} else {
			// Retrieve the various device parameters
			if (ComboBox_GetTextU(GetDlgItem(hMain, IDC_DEVICEEDIT), str_buf, STR_BUFFER_SIZE) == 0) {
				notification(MSG_ERROR, NULL, "Driver Installation", "The description string cannot be empty.");
				r = WDI_ERROR_INVALID_PARAM; goto out;
			}
			dev->desc = safe_strdup(str_buf);
			GetDlgItemTextA(hMain, IDC_VID, str_buf, STR_BUFFER_SIZE);
			if (sscanf(str_buf, "%4x", &tmp) != 1) {
				dprintf("could not convert VID string - aborting");
				r = WDI_ERROR_INVALID_PARAM; goto out;
			}
			dev->vid = (unsigned short)tmp;
			GetDlgItemTextA(hMain, IDC_PID, str_buf, STR_BUFFER_SIZE);
			if (sscanf(str_buf, "%4x", &tmp) != 1) {
				dprintf("could not convert PID string - aborting");
				r = WDI_ERROR_INVALID_PARAM; goto out;
			}
			dev->pid = (unsigned short)tmp;
			GetDlgItemTextA(hMain, IDC_MI, str_buf, STR_BUFFER_SIZE);
			if ( (safe_strlen(str_buf) != 0)
			  && (sscanf(str_buf, "%2x", &tmp) == 1) ) {
				dev->is_composite = TRUE;
				dev->mi = (unsigned char)tmp;
			} else {
				dev->is_composite = FALSE;
				dev->mi = 0;
			}
		}
	}

	if (dev != NULL) {
		inf_name = to_valid_filename(dev->desc, ".inf");
		if (inf_name == NULL) {
			dsprintf("'%s' is %s for a device name",
				dev->desc, (strlen(dev->desc)>WDI_MAX_STRLEN)?"too long":"invalid");
			r = WDI_ERROR_INVALID_PARAM; goto out;
		}
		dprintf("Using inf name: %s", inf_name);
	}

	// Perform extraction/installation
	if (id_options.install_filter_driver) {
		if ((!has_filter_driver) && (MessageBoxA(hMain, "WARNING:\n"
			"Improper use of the filter driver can cause devices to malfunction\n"
			"and, in some cases, complete system failure.\n\n"
			"THE AUTHOR(S) OF THIS SOFTWARE ACCEPT NO LIABILITY FOR\n"
			"ANY DAMAGE RESULTING FROM THE USE OF THE FILTER DRIVER.\n\n"
			"Are you sure you want to install this driver?", "Warning - Filter Driver",
			MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDNO)) {
			r = WDI_ERROR_USER_CANCEL; goto out;
		}
	}
	r = wdi_prepare_driver(dev, extraction_path, inf_name, &pd_options);
	if (r == WDI_SUCCESS) {
		dsprintf("Successfully extracted driver files.");
		// Perform the install if not extracting the files only
		if ((pd_options.driver_type != WDI_USER) && (!extract_only)) {
			if ( (get_driver_type(dev) == DT_SYSTEM)
			  && (MessageBoxA(hMain, "You are about to modify a system driver.\n"
					"Are you sure this is what you want?", "Warning - System Driver",
					MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDNO) ) {
				r = WDI_ERROR_USER_CANCEL; goto out;
			}
			dsprintf("Installing driver. Please wait...");
			id_options.hWnd = hMain;
			r = wdi_install_driver(dev, extraction_path, inf_name, &id_options);
			// Switch to non driverless-only mode and set hw ID to show the newly installed device
			current_device_hardware_id = (dev != NULL)?safe_strdup(dev->hardware_id):NULL;
			if ((r == WDI_SUCCESS) && (!cl_options.list_all) && (!pd_options.use_wcid_driver)) {
				toggle_driverless(FALSE);
			}
			PostMessage(hMain, WM_DEVICECHANGE, 0, 0);	// Force a refresh
		}
	} else {
		dsprintf("Could not extract files");
	}
out:
	if ((pd_options.use_wcid_driver) && (dev != NULL)) {
		safe_free(dev->desc);
	}
	if (need_dealloc) {
		free(dev);
	}
	safe_free(inf_name);
	from_install = TRUE;
	installation_running = FALSE;
	return r;
}

/*
 * Toggle between combo and edit
 */
void combo_breaker(BOOL edit)
{
	if (edit) {
		ShowWindow(GetDlgItem(hMain, IDC_DEVICELIST), SW_HIDE);
		ShowWindow(GetDlgItem(hMain, IDC_DEVICEEDIT), SW_SHOW);
	} else {
		ShowWindow(GetDlgItem(hMain, IDC_DEVICEEDIT), SW_HIDE);
		ShowWindow(GetDlgItem(hMain, IDC_DEVICELIST), SW_SHOW);
	}
}

/*
 * Display or hide the "Install Filter Driver" menu item
 */
void set_filter_menu(BOOL display)
{
	char* itemddesc[2] = {"Install Filter Driver", "Delete Filter Driver"};
	static MENUITEMINFOA mi_filter = { sizeof(MENUITEMINFOA), MIIM_FTYPE|MIIM_ID|MIIM_STRING|MIIM_STATE, MFT_STRING, MFS_ENABLED,
		IDM_SPLIT_FILTER, NULL, NULL, NULL, 0, NULL, 0, NULL };
	static BOOL filter_is_displayed = TRUE;

	// Find if a filter driver is in use
	has_filter_driver = (device != NULL) && (safe_stricmp(driver_name[WDI_LIBUSB0], device->upper_filter) == 0);
	ShowWindow(GetDlgItem(hMain, IDC_FILTER_ICON), has_filter_driver?TRUE:FALSE);

	mi_filter.dwTypeData = itemddesc[has_filter_driver?1:0];
	mi_filter.cch = (UINT)strlen(itemddesc[has_filter_driver?1:0]);

	if (filter_is_displayed) {
		// Always perform delete + display for Delete <-> Install toggle after successfull install/delete
		DeleteMenu(hMenuSplit, IDM_SPLIT_FILTER, MF_BYCOMMAND);
		if (display) {
			InsertMenuItemA(hMenuSplit, IDM_SPLIT_EXTRACT, FALSE, &mi_filter);
			return;
		}
		filter_is_displayed = FALSE;
	} else {
		if (!display)
			return;
		InsertMenuItemA(hMenuSplit, IDM_SPLIT_EXTRACT, FALSE, &mi_filter);
		filter_is_displayed = TRUE;
	}
}

void set_default_driver(void) {
	int i;

	if (!wdi_is_driver_supported(default_driver_type, NULL)) {
		dprintf("'%s' driver is not available", driver_display_name[default_driver_type]);
		for (i=0; i<WDI_NB_DRIVERS; i++) {
			if (wdi_is_driver_supported(i, NULL)) {
				default_driver_type = i;
				break;
			}
		}
		if (i==WDI_NB_DRIVERS) {
			notification(MSG_ERROR, NULL, "No Driver Available", "No driver is available for installation with this application.\n"
				"The application will close");
			EndDialog(hMain, 0);
		}
		dprintf("falling back to '%s' for default driver", driver_display_name[default_driver_type]);
	} else {
		dprintf("default driver set to '%s'", driver_display_name[default_driver_type]);
	}
}

void set_driver(void)
{
	VS_FIXEDFILEINFO file_info;
	char target_text[64];

	if ((pd_options.driver_type == WDI_CDC) || (pd_options.driver_type >= WDI_USER)) {
		safe_sprintf(target_text, 64, "%s", driver_display_name[pd_options.driver_type]);
		EnableMenuItem(hMenuOptions, IDM_CREATECAT, MF_GRAYED);
		EnableMenuItem(hMenuOptions, IDM_SIGNCAT, MF_GRAYED);
	} else {
		EnableMenuItem(hMenuOptions, IDM_CREATECAT, MF_ENABLED);
		EnableMenuItem(hMenuOptions, IDM_SIGNCAT, pd_options.disable_cat?MF_GRAYED:MF_ENABLED);
		wdi_is_driver_supported(pd_options.driver_type, &file_info);
		target_driver_version = file_info.dwFileVersionMS;
		target_driver_version <<= 32;
		target_driver_version += file_info.dwFileVersionLS;
		safe_sprintf(target_text, 64, "%s (v%d.%d.%d.%d)", driver_display_name[pd_options.driver_type],
			(int)file_info.dwFileVersionMS>>16, (int)file_info.dwFileVersionMS&0xFFFF,
			(int)file_info.dwFileVersionLS>>16, (int)file_info.dwFileVersionLS&0xFFFF);
		pd_options.use_wcid_driver = (nb_devices < 0) ||
			((has_wcid == WCID_TRUE) && (pd_options.driver_type == wcid_type));
	}
	SetDlgItemTextA(hMain, IDC_TARGET, target_text);
}


/*
 * Select the next available target driver
 * increment: go through the list up or down
 */
BOOL select_next_driver(int increment)
{
	int i;

	for (i=WDI_WINUSB; i<WDI_NB_DRIVERS; i++) {	// don't loop forever
		pd_options.driver_type = (WDI_NB_DRIVERS + pd_options.driver_type + increment)%WDI_NB_DRIVERS;
		if (wdi_is_driver_supported(pd_options.driver_type, NULL))
			break;
	}
	if (i == WDI_NB_DRIVERS) {
		return FALSE;
	}
	set_driver();
	return TRUE;
}

// Hide/Show the MI field
void display_mi(BOOL show)
{
	int cmd = show?SW_SHOW:SW_HIDE;
	static BOOL mi_shown = TRUE;
	const int report_shift = 27;
	RECT rect;
	POINT point, origin;

	if (show == mi_shown) return;
	ShowWindow(GetDlgItem(hMain, IDC_MI), cmd);
	// Move the VID report button if MI is not shown
	GetWindowRect(GetDlgItem(hMain, IDC_VID_REPORT), &rect);
	origin.x = rect.left; origin.y = rect.top;
	ScreenToClient(hMain, &origin);
	point.x = (rect.right - rect.left);
	point.y = (rect.bottom - rect.top);
	MoveWindow(GetDlgItem(hMain, IDC_VID_REPORT), origin.x  - (show?-report_shift:+report_shift),
		origin.y, point.x, point.y, TRUE);
	mi_shown = show;
}


/*
 * Application state functions
 */

// Toggle "advanced" mode
void toggle_advanced(void)
{
	float dialog_shift = 315.0f;
	float install_widen = 6.0f;
	RECT rect;
	POINT point, origin;
	int toggle;

	advanced_mode = !(GetMenuState(hMenuOptions, IDM_ADVANCEDMODE, MF_CHECKED) & MF_CHECKED);

	// Increase or decrease the Install button size
	GetWindowRect(GetDlgItem(hMain, IDC_INSTALL), &rect);
	origin.x = rect.left; origin.y = rect.top;
	ScreenToClient(hMain, &origin);
	point.x = (rect.right - rect.left);
	point.y = (rect.bottom - rect.top);
	MoveWindow(GetDlgItem(hMain, IDC_INSTALL), origin.x, origin.y,
		point.x + (int)(fScale*(advanced_mode?-install_widen:+install_widen)),
		point.y, TRUE);

	// Increase or decrease the Window size
	GetWindowRect(hMain, &rect);
	point.x = (rect.right - rect.left);
	point.y = (rect.bottom - rect.top);
	MoveWindow(hMain, rect.left, rect.top, point.x,
		point.y + (int)(fScale*(advanced_mode?dialog_shift:-dialog_shift)), TRUE);

	// Move the status bar up or down
	GetWindowRect(hStatus, &rect);
	point.x = rect.left;
	point.y = rect.top;
	ScreenToClient(hMain, &point);
	GetClientRect(hStatus, &rect);
	MoveWindow(hStatus, point.x, point.y + (int)(fScale*(advanced_mode?dialog_shift:-dialog_shift)),
		(rect.right - rect.left), (rect.bottom - rect.top), TRUE);

	// Hide or show the various advanced options
	toggle = advanced_mode?SW_SHOW:SW_HIDE;
	ShowWindow(GetDlgItem(hMain, IDC_BROWSE), toggle);
	ShowWindow(GetDlgItem(hMain, IDC_GROUPLOG), toggle);
	ShowWindow(GetDlgItem(hMain, IDC_INFO), toggle);

	// Toggle the menu checkmark
	CheckMenuItem(hMenuOptions, IDM_ADVANCEDMODE, advanced_mode?MF_CHECKED:MF_UNCHECKED);
}

// Toggle edit description
void toggle_edit(void)
{
	if ((IsDlgButtonChecked(hMain, IDC_EDITNAME) == BST_CHECKED) && (device != NULL)) {
		combo_breaker(TRUE);
		if (editable_desc != NULL) {
			dprintf("program assertion failed - editable_desc != NULL");
			return;
		}
		editable_desc = (char*)malloc(STR_BUFFER_SIZE);
		if (editable_desc == NULL) {
			dprintf("could not allocate buffer to edit description");
			CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
			combo_breaker(FALSE);
			return;
		}
		safe_strcpy(editable_desc, STR_BUFFER_SIZE, device->desc);
		free(device->desc);	// No longer needed
		device->desc = editable_desc;
		SetDlgItemTextU(hMain, IDC_DEVICEEDIT, editable_desc);
		SetFocus(GetDlgItem(hMain, IDC_DEVICEEDIT));
	} else {
		combo_breaker(FALSE);
		display_devices();
		editable_desc = NULL;
	}
}

// Update WCID, filter and coloured arrow
void update_ui(void)
{
	BOOL same_driver;
	BOOL warn;

	switch (has_wcid) {
	case WCID_TRUE:
		ShowWindow(GetDlgItem(hMain, IDC_WCID), TRUE);
		SendMessage(GetDlgItem(hMain, IDC_WCID_ICON), STM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hIconTickOK);
		break;
	case WCID_FALSE:
		ShowWindow(GetDlgItem(hMain, IDC_WCID), FALSE);
		SendMessage(GetDlgItem(hMain, IDC_WCID_ICON), STM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hIconTickNOK);
		break;
	case WCID_NONE:
		ShowWindow(GetDlgItem(hMain, IDC_WCID), FALSE);
		SendMessage(GetDlgItem(hMain, IDC_WCID_ICON), STM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)NULL);
		break;
	}

	if (pd_options.driver_type != WDI_LIBUSB0) {
		id_options.install_filter_driver = FALSE;
	}
	set_filter_menu((pd_options.driver_type == WDI_LIBUSB0) && (nb_devices>=0));
	same_driver = device && (safe_stricmp(device->driver, driver_name[pd_options.driver_type]) == 0);
	warn = (get_driver_type(device) == DT_SYSTEM) || (same_driver && (target_driver_version < device->driver_version)) ;

	if (use_arrow_icons) {
		MoveWindow(hArrow, arrow_origin.x, arrow_origin.y, arrow_width, arrow_height+(warn?-2:0), TRUE);
		SendMessage(hArrow, STM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)(warn?hIconArrowOrange:hIconArrowGreen));
	}
	else {
		arrow_color = warn?ARROW_ORANGE:ARROW_GREEN;
		InvalidateRect(hArrow, NULL, TRUE);
		UpdateWindow(hArrow);
	}
	destroy_tooltip(hArrowToolTip);
	hArrowToolTip = create_tooltip(hArrow, warn?
		"Driver installation may produce unwanted results":
		"Driver installation is deemed safe", -1);
}

// Toggle device creation mode
void toggle_create(BOOL refresh)
{
	create_device = !(GetMenuState(hMenuDevice, IDM_CREATE, MF_CHECKED) & MF_CHECKED);
	if (create_device) {
		device = NULL;
		// Disable Edit Desc. if selected
		if (IsDlgButtonChecked(hMain, IDC_EDITNAME) == BST_CHECKED) {
			CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
			toggle_edit();
		}
		combo_breaker(TRUE);
		has_wcid = WCID_NONE;
		update_ui();
		EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), FALSE);
		SetDlgItemTextA(hMain, IDC_VID, "");
		SetDlgItemTextA(hMain, IDC_PID, "");
		SetDlgItemTextA(hMain, IDC_MI, "");
		SetDlgItemTextA(hMain, IDC_DRIVER, "");
		SetDlgItemTextA(hMain, IDC_DEVICEEDIT, "");
		PostMessage(GetDlgItem(hMain, IDC_VID), EM_SETREADONLY, (WPARAM)FALSE, 0);
		PostMessage(GetDlgItem(hMain, IDC_PID), EM_SETREADONLY, (WPARAM)FALSE, 0);
		PostMessage(GetDlgItem(hMain, IDC_MI), EM_SETREADONLY, (WPARAM)FALSE, 0);
		destroy_tooltip(hVIDToolTip);
		hVIDToolTip = NULL;
		unknown_vid = FALSE;
		display_mi(TRUE);
		SetFocus(GetDlgItem(hMain, IDC_DEVICEEDIT));
	} else {
		combo_breaker(FALSE);
		PostMessage(GetDlgItem(hMain, IDC_VID), EM_SETREADONLY, (WPARAM)TRUE, 0);
		PostMessage(GetDlgItem(hMain, IDC_PID), EM_SETREADONLY, (WPARAM)TRUE, 0);
		PostMessage(GetDlgItem(hMain, IDC_MI), EM_SETREADONLY, (WPARAM)TRUE, 0);
		if (refresh) {
			PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
		}
	}
	CheckMenuItem(hMenuDevice, IDM_CREATE, create_device?MF_CHECKED:MF_UNCHECKED);
	pd_options.use_wcid_driver = FALSE;
	replace_driver = FALSE;
	set_install_button();
}

// Toggle ignore hubs & composite
void toggle_hubs(BOOL refresh)
{
	cl_options.list_hubs = GetMenuState(hMenuOptions, IDM_IGNOREHUBS, MF_CHECKED) & MF_CHECKED;

	if (create_device) {
		toggle_create(TRUE);
	}

	CheckMenuItem(hMenuOptions, IDM_IGNOREHUBS, cl_options.list_hubs?MF_UNCHECKED:MF_CHECKED);
	// Reset Edit button
	CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
	// Reset Combo
	combo_breaker(FALSE);
	if (refresh) {
		PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
	}
}

// Toggle driverless device listing
void toggle_driverless(BOOL refresh)
{
	cl_options.list_all = !(GetMenuState(hMenuOptions, IDM_LISTALL, MF_CHECKED) & MF_CHECKED);
	EnableMenuItem(hMenuOptions, IDM_IGNOREHUBS, cl_options.list_all?MF_ENABLED:MF_GRAYED);

	if (create_device) {
		toggle_create(TRUE);
	}

	CheckMenuItem(hMenuOptions, IDM_LISTALL, cl_options.list_all?MF_CHECKED:MF_UNCHECKED);
	// Reset Edit button
	CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
	// Reset Combo
	combo_breaker(FALSE);
	if (refresh) {
		PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
	}
}

// Change the Install button label
void set_install_button(void)
{
	char label[64];
	char *action, *qualifier, *object;

	EnableMenuItem(hMenuSplit, IDM_SPLIT_INSTALL, ((device==NULL)&&(!create_device))?MF_GRAYED:MF_ENABLED);
	EnableMenuItem(hMenuSplit, IDM_SPLIT_WCID, (create_device)?MF_GRAYED:MF_ENABLED);
	CheckMenuItem(hMenuSplit, IDM_SPLIT_INSTALL, MF_CHECK((!pd_options.use_wcid_driver) && (!extract_only) && (!id_options.install_filter_driver)));
	CheckMenuItem(hMenuSplit, IDM_SPLIT_WCID, MF_CHECK(pd_options.use_wcid_driver && (!extract_only) && (!id_options.install_filter_driver)));
	CheckMenuItem(hMenuSplit, IDM_SPLIT_EXTRACT, MF_CHECK(extract_only && (!id_options.install_filter_driver)));
	CheckMenuItem(hMenuSplit, IDM_SPLIT_FILTER, MF_CHECK(id_options.install_filter_driver));
	if (extract_only) {
		action = "Extract";
		object = "Files";
	} else {
		object = "Driver";
		if ((device != NULL) && (replace_driver) && (!id_options.install_filter_driver)) {
			if ((has_wcid != WCID_TRUE) && pd_options.use_wcid_driver) {
				action = "Install";
			} else if ((has_wcid == WCID_TRUE) && (!pd_options.use_wcid_driver)) {
				action = "Replace";
			} else if (safe_stricmp(device->driver, driver_name[pd_options.driver_type]) == 0) {
				if (target_driver_version == device->driver_version) {
					action = "Reinstall";
				} else if (target_driver_version > device->driver_version) {
					action = "Upgrade";
				} else {
					action = "Downgrade";
				}
			} else {
				action = pd_options.use_wcid_driver?"Install":"Replace";
			}
		} else {
			action = has_filter_driver?"Delete":"Install";
		}
	}
	qualifier = pd_options.use_wcid_driver?"WCID ":(id_options.install_filter_driver?"Filter ":"");
	safe_sprintf(label, 64, "%s %s%s", action, qualifier, object);
	SetDlgItemTextA(hMain, IDC_INSTALL, label);
}

// Change the log level
void set_loglevel(DWORD menu_cmd)
{
	CheckMenuItem(hMenuLogLevel, log_level+IDM_LOGLEVEL_DEBUG, MF_UNCHECKED);
	CheckMenuItem(hMenuLogLevel, menu_cmd, MF_CHECKED);
	log_level = menu_cmd - IDM_LOGLEVEL_DEBUG;
	wdi_set_log_level(log_level);
}

typedef HIMAGELIST (WINAPI *ImageList_Create_t)(
	int cx,
	int cy,
	UINT flags,
	int cInitial,
	int cGrow
);
typedef int (WINAPI *ImageList_ReplaceIcon_t)(
	HIMAGELIST himl,
	int i,
	HICON hicon
);

void init_dialog(HWND hDlg)
{
	int i, err;
	HINSTANCE hDllInst;
	HFONT hf;
	HDC hdc;
	long lfHeight;
	RECT rect;
	int i16, i24;
	char *token, version[] = APP_VERSION;

	struct {
		HIMAGELIST himl;
		RECT margin;
		UINT uAlign;
	} bi = {0};	// BUTTON_IMAGELIST
	// MinGW fails to link those
	ImageList_Create_t pImageList_Create = NULL;
	ImageList_ReplaceIcon_t pImageList_ReplaceIcon = NULL;

	// Quite a burden to carry around as parameters
	hMain = hDlg;
	hDeviceList = GetDlgItem(hDlg, IDC_DEVICELIST);
	hInfo = GetDlgItem(hDlg, IDC_INFO);
	hArrow = GetDlgItem(hMain, IDC_RARR);
	hMenuDevice = GetSubMenu(GetMenu(hDlg), 0);
	hMenuOptions = GetSubMenu(GetMenu(hDlg), 1);
	hMenuLogLevel = GetSubMenu(hMenuOptions, 7);
	hMenuSplit = GetSubMenu(LoadMenuA(main_instance, "IDR_INSTALLSPLIT"), 0);

	// High DPI scaling
	i16 = GetSystemMetrics(SM_CXSMICON);
	hdc = GetDC(hDlg);
	fScale = GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f;
	ReleaseDC(hDlg, hdc);
	i24 = (int)(24.0f*fScale);

	// Set the title bar icon
	set_title_bar_icon(hDlg);

	// Count of Microsoft for making it more attractive to read a
	// version using strtok() than using GetFileVersionInfo()
	IGNORE_RETVAL(strtok(version, " "));
	for (i=0; (i<4) && ((token = strtok(NULL, ".")) != NULL); i++)
		application_version[i] = (uint16_t)atoi(token);

	// Create the status line
	create_status_bar();
	// Display the version in the right area of the status bar
	SendMessageA(GetDlgItem(hDlg, IDC_STATUS), SB_SETTEXTA, SBT_OWNERDRAW | 1, (LPARAM)APP_VERSION);

	// Create various tooltips
	create_tooltip(GetDlgItem(hMain, IDC_EDITNAME),
		"Change the device name", -1);
	create_tooltip(GetDlgItem(hMain, IDC_VIDPID),
		"VID:PID[:MI]", -1);
	create_tooltip(GetDlgItem(hMain, IDC_DRIVER),
		"Current Driver", -1);
	create_tooltip(GetDlgItem(hMain, IDC_TARGET),
		"Target Driver", -1);
	create_tooltip(GetDlgItem(hMain, IDC_STATIC_WCID),
		"Windows Compatible ID\nClick '?' for more info.", -1);
	create_tooltip(GetDlgItem(hMain, IDC_WCID_BOX),
		"Windows Compatible ID\nClick '?' for more info.", -1);
	create_tooltip(GetDlgItem(hMain, IDC_WCID_ICON),
		"Windows Compatible ID\nClick '?' for more info.", -1);
	create_tooltip(GetDlgItem(hMain, IDC_BROWSE),
		"Directory to extract/install files to", -1);
	create_tooltip(GetDlgItem(hMain, IDC_WCID_URL),
		"Online information about WCID", -1);
	create_tooltip(GetDlgItem(hMain, IDC_VID_REPORT),
		"Submit Vendor to the USB ID Repository", -1);
	create_tooltip(GetDlgItem(hMain, IDC_FILTER_ICON),
		"This device also has the\nlibusb-win32 filter driver", -1);
	create_tooltip(GetDlgItem(hMain, IDC_LIBUSB_URL),
		"Find out more about libusb online", -1);
	create_tooltip(GetDlgItem(hMain, IDC_LIBUSB0_URL),
		"Find out more about libusb-win32 online", -1);
	create_tooltip(GetDlgItem(hMain, IDC_LIBUSBK_URL),
		"Find out more about libusbK online", -1);
	create_tooltip(GetDlgItem(hMain, IDC_WINUSB_URL),
		"Find out more about WinUSB online", -1);

	// Load system icons for various items (NB: Use the excellent http://www.nirsoft.net/utils/iconsext.html to find icon IDs)
	hDllInst = GetDLLHandle("shell32.dll");
	// These shell32 icons should be available on any Windows system
	hIconFolder = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(4), IMAGE_ICON, i16, i16, LR_DEFAULTCOLOR|LR_SHARED);
	hIconTickNOK = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(240), IMAGE_ICON, i16, i16, LR_DEFAULTCOLOR|LR_SHARED);
	hIconReport = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(244), IMAGE_ICON, i16, i16, LR_DEFAULTCOLOR|LR_SHARED);
	// Try to locate a green checkmark icon
	hDllInst = GetDLLHandle("urlmon.dll");
	hIconTickOK = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(100), IMAGE_ICON, i16, i16, LR_DEFAULTCOLOR|LR_SHARED);
	if ((hDllInst == NULL) || (hIconTickOK == NULL)) {
		// No luck, fallback to next best thing in shell32
		hDllInst = GetDLLHandle("shell32.dll");
		hIconTickOK = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(246), IMAGE_ICON, i16, i16, LR_DEFAULTCOLOR|LR_SHARED);
	}
	// Try to locate a funnel icon
	hDllInst = GetDLLHandle("admtmpl.dll");
	hIconFilter = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(6), IMAGE_ICON, i16, i16, LR_DEFAULTCOLOR|LR_SHARED);
	if ((hDllInst == NULL) || (hIconFilter == NULL)) {
		hDllInst = GetDLLHandle("wmploc.dll");
		hIconFilter = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(475), IMAGE_ICON, i16, i16, LR_DEFAULTCOLOR|LR_SHARED);
	}
	if ((hDllInst == NULL) || (hIconFilter == NULL)) {
		// No luck, fallback to next best thing in shell32
		hDllInst = GetDLLHandle("shell32.dll");
		hIconFilter = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(278), IMAGE_ICON, i16, i16, LR_DEFAULTCOLOR|LR_SHARED);
	}
	SendMessage(GetDlgItem(hDlg, IDC_FILTER_ICON), STM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hIconFilter);
	do {
		if ((hDllInst = GetDLLHandle("ieframe.dll")) == NULL) break;	// Green right arrow
		hIconArrowGreen = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(42025), IMAGE_ICON, i24, i24, LR_DEFAULTCOLOR|LR_SHARED);
		if ((hDllInst = GetDLLHandle("netshell.dll")) == NULL) break;	// Orange right arrow
		hIconArrowOrange = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(1607), IMAGE_ICON, i24, i24, LR_DEFAULTCOLOR|LR_SHARED);
		if ( (hIconArrowGreen == NULL) || (hIconArrowOrange == NULL) ) break;
		use_arrow_icons = TRUE;
		// On newer OSes, recreate the control so that it uses icons
		GetWindowRect(hArrow, &rect);
		arrow_origin.x = rect.left; arrow_origin.y = rect.top;
		arrow_width = rect.right - rect.left; arrow_height = i24;
		ScreenToClient(hMain, &arrow_origin);
		arrow_origin.x += 1;	// Some fixup is needed
		DestroyWindow(hArrow);
		// We need SS_CENTERIMAGE to be able to increase the control height by two and achieve pixel positioning
		hArrow = CreateWindowExA(0, "STATIC", NULL,
			SS_ICON | SS_NOTIFY | SS_CENTERIMAGE | SS_REALSIZEIMAGE | WS_GROUP | WS_CHILD | WS_VISIBLE,
			arrow_origin.x, arrow_origin.y, arrow_width, arrow_height, hMain, NULL, main_instance, NULL);
		SendMessage(hArrow, STM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hIconArrowGreen);
	} while (0);
	if (!use_arrow_icons) {
		// Fallback to text arrow if icons can't be used, but first change the font
		hdc = GetDC(NULL);
		lfHeight = -MulDiv(20, GetDeviceCaps(hdc, LOGPIXELSY), 72);
		ReleaseDC(NULL, hdc);
		hf = CreateFontA(lfHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			SYMBOL_CHARSET, 0, 0, PROOF_QUALITY, 0, "Wingdings");
		SendDlgItemMessageA(hDlg, IDC_RARR, WM_SETFONT, (WPARAM)hf, TRUE);
		ShowWindow(hArrow, TRUE);
	}

	// Set a folder icon on the select folder button
	pImageList_Create = (ImageList_Create_t) GetProcAddress(GetDLLHandle("comctl32.dll"), "ImageList_Create");
	pImageList_ReplaceIcon = (ImageList_ReplaceIcon_t) GetProcAddress(GetDLLHandle("comctl32.dll"), "ImageList_ReplaceIcon");

	bi.himl = pImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
	pImageList_ReplaceIcon(bi.himl, -1, hIconFolder);
	SetRect(&bi.margin, 0, 0, 0, 0);
	bi.uAlign = 4;	// BUTTON_IMAGELIST_ALIGN_CENTER
	SendMessage(GetDlgItem(hDlg, IDC_BROWSE), BCM_SETIMAGELIST, 0, (LPARAM)&bi);

	bi.himl = pImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
	pImageList_ReplaceIcon(bi.himl, -1, hIconReport);
	SetRect(&bi.margin, 0, 1, 0, 0);
	bi.uAlign = 4;	// BUTTON_IMAGELIST_ALIGN_CENTER
	SendMessage(GetDlgItem(hDlg, IDC_VID_REPORT), BCM_SETIMAGELIST, 0, (LPARAM)&bi);

	// The application always starts in advanced mode
	CheckMenuItem(hMenuOptions, IDM_ADVANCEDMODE, MF_CHECKED);

	// Setup logging
	err = wdi_register_logger(hMain, UM_LOGGER_EVENT, 0);
	if (err != WDI_SUCCESS) {
		dprintf("Unable to access log output - logging will be disabled (%s)", wdi_strerror(err));
	}
	// Increase the size of our log textbox to MAX_LOG_SIZE (unsigned word)
	PostMessage(hInfo, EM_LIMITTEXT, MAX_LOG_SIZE , 0);

	dprintf(APP_VERSION);
	dprintf(WindowsVersionStr);

	// Limit the input size of VID, PID, MI
	PostMessage(GetDlgItem(hMain, IDC_VID), EM_SETLIMITTEXT, 4, 0);
	PostMessage(GetDlgItem(hMain, IDC_PID), EM_SETLIMITTEXT, 4, 0);
	PostMessage(GetDlgItem(hMain, IDC_MI), EM_SETLIMITTEXT, 2, 0);

	// Parse the ini file and set the startup options accordingly
	parse_ini();
	set_loglevel(log_level+IDM_LOGLEVEL_DEBUG);
	set_default_driver();

	if (!advanced_mode) {
		toggle_advanced();	// We start in advanced mode
	}
	if (cl_options.list_all) {
		toggle_driverless(FALSE);
	}
	if (cl_options.list_hubs) {
		toggle_hubs(FALSE);
	}
	pd_options.driver_type = default_driver_type;
	select_next_driver(0);
}

/*
 * Parse the default ini file
 */
BOOL parse_ini(void) {
	profile_t profile;
	char* tmp = NULL;
	long r;

	// Check if the ini file exists
	if (GetFileAttributesU(INI_NAME) == INVALID_FILE_ATTRIBUTES) {
		dprintf("ini file '%s' not found - default parameters will be used", INI_NAME);
		return FALSE;
	}

	// Parse the file
	r = profile_open(INI_NAME, &profile);
	if (r) {
		dprintf("error while processing '%s': %s", INI_NAME, profile_errtostr(r));
		return FALSE;
	}

	dprintf("reading ini file '%s'", INI_NAME);

	// Set the various boolean options
	profile_get_boolean(profile, "general", "advanced_mode", NULL, FALSE, &advanced_mode);
	profile_get_boolean(profile, "general", "exit_on_success", NULL, FALSE, &exit_on_success);
	profile_get_boolean(profile, "device", "list_all", NULL, FALSE, &cl_options.list_all);
	profile_get_boolean(profile, "device", "include_hubs", NULL, FALSE, &cl_options.list_hubs);
	profile_get_boolean(profile, "driver", "extract_only", NULL, FALSE, &extract_only);
	profile_get_boolean(profile, "device", "trim_whitespaces", NULL, FALSE, &cl_options.trim_whitespaces);
	profile_get_boolean(profile, "security", "disable_cert_install_warning", NULL, FALSE, &ic_options.disable_warning);

	// Set the log level
	profile_get_integer(profile, "general", "log_level", NULL, WDI_LOG_LEVEL_INFO, &log_level);
	if ((log_level < WDI_LOG_LEVEL_DEBUG) && (log_level > WDI_LOG_LEVEL_NONE)) {
		log_level = WDI_LOG_LEVEL_INFO;
	}

	// Set the default extraction dir
	if ((profile_get_string(profile, "driver", "default_dir", NULL, NULL, &tmp) == 0) && (tmp != NULL)) {
		safe_strcpy(extraction_path, sizeof(extraction_path), tmp);
	}

	// Set the certificate name to install, if any
	if ( (profile_get_string(profile, "security", "install_cert", NULL, NULL, &tmp) == 0)
	  && (tmp != NULL) ) {
		if (wdi_is_file_embedded(NULL, (char*)tmp)) {
			ic_options.hWnd = hMain;
			wdi_install_trusted_certificate((char*)tmp, &ic_options);
		} else {
			dprintf("certificate '%s' not found in this application", tmp);
		}
	}

	// Set the default driver
	profile_get_integer(profile, "driver", "default_driver", NULL, WDI_WINUSB, &default_driver_type);
	if ((default_driver_type < WDI_WINUSB) || (default_driver_type >= WDI_NB_DRIVERS)) {
		dprintf("invalid value '%d' for ini option 'default_driver'", default_driver_type);
		default_driver_type = WDI_WINUSB;
	}

	profile_close(profile);

	return TRUE;
}

/*
 * Parse a preset device configuration file
 */
BOOL parse_preset(char* filename)
{
	profile_t profile;
	unsigned int tmp = 0x10000;
	long r;
	char str_tmp[5];
	char* desc = NULL;

	if (filename == NULL) {
		return FALSE;
	}

	r = profile_open(filename, &profile);
	if (r) {
		dprintf("error while processing '%s': %s", filename, profile_errtostr(r));
		return FALSE;
	}

	profile_get_uint(profile, "device", "VID", NULL, 0x10000, &tmp);
	if (tmp > 0xFFFF) {
		dprintf("no VID found in preset file - aborting readout");
		profile_close(profile);
		return FALSE;
	}

	if (!create_device) {
		toggle_create(FALSE);
	}

	safe_sprintf(str_tmp, 5, "%04X", tmp);
	SetDlgItemTextA(hMain, IDC_VID, str_tmp);

	profile_get_string(profile, "device", "Description", NULL, NULL, &desc);
	if (desc != NULL) {
		SetDlgItemTextU(hMain, IDC_DEVICEEDIT, (char*)desc);
	}

	profile_get_uint(profile, "device", "PID", NULL, 0x10000, &tmp);
	if (tmp <= 0xFFFF) {
		safe_sprintf(str_tmp, 5, "%04X", tmp);
		SetDlgItemTextA(hMain, IDC_PID, str_tmp);
	}

	profile_get_uint(profile, "device", "MI", NULL, 0x100, &tmp);
	if (tmp <= 0xFF) {
		safe_sprintf(str_tmp, 5, "%02X", tmp);
		SetDlgItemTextA(hMain, IDC_MI, str_tmp);
	}

	profile_get_string(profile, "device", "GUID", NULL, NULL, &pd_options.device_guid);

	profile_close(profile);

	return TRUE;
}

/*
 * Work around the limitations of edit control, for UI aesthetics
 */
static INT_PTR CALLBACK subclass_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_SETCURSOR:
		if ((HWND)wParam == GetDlgItem(hDlg, IDC_WCID_BOX)) {
			SetCursor(LoadCursor(NULL, IDC_ARROW));
			return (INT_PTR)TRUE;
		}
		if ( ((HWND)wParam == GetDlgItem(hDlg, IDC_LIBUSB0_URL))
		  || ((HWND)wParam == GetDlgItem(hDlg, IDC_LIBUSB_URL))
		  || ((HWND)wParam == GetDlgItem(hDlg, IDC_LIBUSBK_URL))
		  || ((HWND)wParam == GetDlgItem(hDlg, IDC_WINUSB_URL))
		  || ((HWND)wParam == GetDlgItem(hDlg, IDC_WCID_URL)) ) {
			SetCursor(LoadCursor(NULL, IDC_HAND));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return CallWindowProc(original_wndproc, hDlg, message, wParam, lParam);
}

void create_static_fonts(HDC dc) {
	TEXTMETRIC tm;
	LOGFONT lf;

	if (hyperlink_font != NULL)
		return;
	GetTextMetrics(dc, &tm);
	lf.lfHeight = tm.tmHeight;
	lf.lfWidth = 0;
	lf.lfEscapement = 0;
	lf.lfOrientation = 0;
	lf.lfWeight = tm.tmWeight;
	lf.lfItalic = tm.tmItalic;
	lf.lfUnderline = TRUE;
	lf.lfStrikeOut = tm.tmStruckOut;
	lf.lfCharSet = tm.tmCharSet;
	lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
	lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	lf.lfQuality = DEFAULT_QUALITY;
	lf.lfPitchAndFamily = tm.tmPitchAndFamily;
	GetTextFace(dc, LF_FACESIZE, lf.lfFaceName);
	hyperlink_font = CreateFontIndirect(&lf);
	lf.lfWeight = FW_BOLD;
	lf.lfUnderline = FALSE;
	bold_font = CreateFontIndirect(&lf);
}

/*
 * Main dialog callback
 */
INT_PTR CALLBACK main_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	static uintptr_t notification_delay_thid = -1L;
	static DWORD last_scroll = 0;
	char str_tmp[5];
	char log_buf[2*STR_BUFFER_SIZE];
	char *log_buffer, *filepath;
	const char *vid_string, *ms_comp_hdr = "USB\\MS_COMP_";
	int i, r;
	HWND hCtrl;
	DWORD delay, read_size, log_size;
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	NMBCDROPDOWN* pDropDown;
	POINT pt;
	DRAWITEMSTRUCT* di;

	// The following local variables are used to change the visual aspect of the fields
	static HWND hDeviceEdit;
	static HWND hVid, hPid, hMi, hWcid;
	static HWND hDriver, hTarget;
	static HBRUSH white_brush = (HBRUSH)FALSE;
	static HBRUSH grey_brush = (HBRUSH)FALSE;
#if defined(COLOURED_FIELDS)
	static HBRUSH green_brush = (HBRUSH)FALSE;
	static HBRUSH orange_brush = (HBRUSH)FALSE;
	static HBRUSH driver_background[NB_DRIVER_TYPES];
#endif

	switch (message) {

	case WM_DEVICECHANGE:
		NOT_DURING_INSTALL;
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
		if (notification_delay_thid == -1L) {
			delay = NOTIFICATION_DELAY;
			notification_delay_thid = _beginthread(notification_delay_thread, 0, (void*)(uintptr_t)delay);
			if (notification_delay_thid == -1L) {
				dprintf("Unable to create notification delay thread - notification events will be disabled");
			}
		}
		return (INT_PTR)TRUE;

	case UM_DEVICE_EVENT:
		NOT_DURING_INSTALL;
		notification_delay_thid = -1L;
		if (create_device) {
			if (MessageBoxA(hMain, "The USB device list has been modified.\n"
				"Do you want to refresh the application?\n(you will lose all your modifications)",
				"USB Event Notification", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
				if (create_device) {
					toggle_create(FALSE);
				}
				CheckMenuItem(hMenuDevice, IDM_CREATE, MF_UNCHECKED);
				combo_breaker(FALSE);
				PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
			}
		} else {
			PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
		}
		return (INT_PTR)TRUE;

	case UM_LOGGER_EVENT:
		r = wdi_read_logger(log_buf, sizeof(log_buf), &read_size);
		if (r == WDI_SUCCESS) {
			if (read_size > 0) dprintf("%s", log_buf);
		} else {
			dprintf("wdi_read_logger: error %s", wdi_strerror(r));
		}
		return (INT_PTR)TRUE;

	case WM_INITDIALOG:
		SetUpdateCheck();
		// Setup options
		cl_options.trim_whitespaces = TRUE;

		// Setup local visual variables
		white_brush = CreateSolidBrush(WHITE);
#if defined(COLOURED_FIELDS)
		green_brush = CreateSolidBrush(FIELD_GREEN);
		orange_brush = CreateSolidBrush(FIELD_ORANGE);
		driver_background[DT_NONE] = grey_brush;
		driver_background[DT_LIBUSB] = green_brush;
		driver_background[DT_SYSTEM] = orange_brush;
		driver_background[DT_UNKNOWN] = (HBRUSH)FALSE;
#endif

		// Speedup checks for WM_CTLCOLOR
		hDeviceEdit = GetDlgItem(hDlg, IDC_DEVICEEDIT);
		hVid = GetDlgItem(hDlg, IDC_VID);
		hPid = GetDlgItem(hDlg, IDC_PID);
		hMi = GetDlgItem(hDlg, IDC_MI);
		hWcid =  GetDlgItem(hDlg, IDC_WCID);
		hDriver = GetDlgItem(hDlg, IDC_DRIVER);
		hTarget = GetDlgItem(hDlg, IDC_TARGET);

		// Subclass the callback so that we can change the cursor
		original_wndproc = (WNDPROC)SetWindowLongPtr(hDlg, GWLP_WNDPROC, (LONG_PTR)subclass_callback);

		// Main init
		init_dialog(hDlg);
		CheckForUpdates(FALSE);

		// Fall through
	case UM_REFRESH_LIST:
		NOT_DURING_INSTALL;
		// Reset edit mode if selected
		if (IsDlgButtonChecked(hMain, IDC_EDITNAME) == BST_CHECKED) {
			combo_breaker(FALSE);
			CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
		}
		id_options.install_filter_driver = FALSE;
		if (list != NULL) wdi_destroy_list(list);
		if (!from_install) {
			current_device_index = 0;
		}
		editable_desc = NULL;
		device = NULL;
		r = wdi_create_list(&list, &cl_options);
		if (r == WDI_SUCCESS) {
			nb_devices = display_devices();
			// Send a dropdown selection message to update fields
			PostMessage(hMain, WM_COMMAND, MAKELONG(IDC_DEVICELIST, CBN_SELCHANGE),
				(LPARAM)hDeviceList);
		} else {
			nb_devices = -1;
			_IGNORE(ComboBox_ResetContent(hDeviceList));
			SetDlgItemTextA(hMain, IDC_VID, "");
			SetDlgItemTextA(hMain, IDC_PID, "");
			SetDlgItemTextA(hMain, IDC_DRIVER, "");
			display_mi(FALSE);
			EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), FALSE);
			has_wcid = WCID_NONE;
			pd_options.use_wcid_driver = TRUE;
			update_ui();
			set_install_button();
		}
		// Make sure we don't override the install status on refresh from install
		if (!from_install) {
			dsprintf("%d device%s found.", nb_devices+1, (nb_devices!=0)?"s":"");
		} else {
			dprintf("%d device%s found.", nb_devices+1, (nb_devices!=0)?"s":"");
			from_install = FALSE;
		}
		return (INT_PTR)TRUE;

	case WM_VSCROLL:
		if (LOWORD(wParam) == 4) {
			select_next_driver( ((HIWORD(wParam) <= last_scroll))?-1:+1);
			update_ui();
			set_install_button();
			last_scroll = HIWORD(wParam);
			return (INT_PTR)TRUE;
		}
		return (INT_PTR)FALSE;

	// Change the font colour of editable fields to dark blue
	case WM_CTLCOLOREDIT:
		hCtrl = (HWND)lParam;
		if ( (hCtrl == hDeviceEdit) || ((HWND)lParam == hVid)
		  || (hCtrl == hPid) || (hCtrl == hMi) ) {
			SetTextColor((HDC)wParam, DARK_BLUE);
			return (INT_PTR)white_brush;
		}
		return (INT_PTR)FALSE;

	// Set background colour of read only fields as well as the colour of the text arrow
	case WM_CTLCOLORSTATIC:
		hCtrl = (HWND)lParam;
		// Must be transparent for non Aero Windows 7
		SetBkMode((HDC)wParam, TRANSPARENT);
		if ( (hCtrl == hVid) || (hCtrl == hPid) || (hCtrl == hMi) || (hCtrl == hWcid) ) {
			return (INT_PTR)grey_brush;
		}
		if (hCtrl == hDriver) {
#if defined(COLOURED_FIELDS)
			return (INT_PTR)driver_background[get_driver_type(device)];
#endif
			return (INT_PTR)grey_brush;
		}
		if (hCtrl == hTarget) {
			return (INT_PTR)white_brush;
		}
		if (hCtrl == hArrow) {
			SetTextColor((HDC)wParam, arrow_color);
			return (INT_PTR)CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
		}
		if ( (hCtrl == GetDlgItem(hMain, IDC_LIBUSB0_URL))
		  || (hCtrl == GetDlgItem(hMain, IDC_LIBUSB_URL))
		  || (hCtrl == GetDlgItem(hMain, IDC_LIBUSBK_URL))
		  || (hCtrl == GetDlgItem(hMain, IDC_WINUSB_URL))
		  || (hCtrl == GetDlgItem(hMain, IDC_WCID_URL)) ) {
			create_static_fonts((HDC)wParam);
			SelectObject((HDC)wParam, hyperlink_font);
			SetTextColor((HDC)wParam, DARK_BLUE);
			return (INT_PTR)GetStockObject(NULL_BRUSH);
		}
		if (hCtrl == GetDlgItem(hMain, IDC_LINKS)) {
			create_static_fonts((HDC)wParam);
			SelectObject((HDC)wParam, bold_font);
			SetTextColor((HDC)wParam, GetSysColor(COLOR_3DDKSHADOW));
			return (INT_PTR)GetStockObject(NULL_BRUSH);
		}
		if (hCtrl == GetDlgItem(hMain, IDC_WCID_ICON)) {
			// Ensures that the WCID background field is not drawn on top of the WCID icon
			SendMessage(GetDlgItem(hMain, IDC_WCID_BOX), (WPARAM)WM_SETREDRAW, TRUE, 0);
			InvalidateRect(GetDlgItem(hMain, IDC_WCID_BOX), NULL, TRUE);
			UpdateWindow(GetDlgItem(hMain, IDC_WCID_BOX));
			SendMessage(GetDlgItem(hMain, IDC_WCID_BOX), (WPARAM)WM_SETREDRAW, FALSE, 0);
		}
		// Restore transparency if we don't change the background
		SetBkMode((HDC)wParam, OPAQUE);
		return (INT_PTR)FALSE;

	// Change the colour of the version text in the status bar
	case WM_DRAWITEM:
		if (wParam == IDC_STATUS) {
			di = (DRAWITEMSTRUCT*)lParam;
			SetBkMode(di->hDC, TRANSPARENT);
			SetTextColor(di->hDC, GetSysColor(COLOR_3DSHADOW));
			di->rcItem.top += (int)(2.0f * fScale);
			di->rcItem.left += (int)(4.0f * fScale);
			DrawTextExA(di->hDC, APP_VERSION, -1, &di->rcItem, DT_LEFT, NULL);
			return (INT_PTR)TRUE;
		}
		break;

	// Display the Install button split menu
	case WM_NOTIFY:
		switch (LOWORD(wParam)) {
		case IDC_INSTALL:
			switch (((LPNMHDR)lParam)->code) {
			case BCN_DROPDOWN:
				pDropDown = (LPNMBCDROPDOWN)lParam;
				pt.x = pDropDown->rcButton.left;
				pt.y = pDropDown->rcButton.bottom;
				ClientToScreen(pDropDown->hdr.hwndFrom, &pt);
				TrackPopupMenuEx(hMenuSplit, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, hMain, NULL);
				break;
			}
			break;
		}
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam)) {
		case IDC_LIBUSB0_URL:
			ShellExecuteA(hDlg, "open", LIBUSB0_URL, NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDC_LIBUSB_URL:
			ShellExecuteA(hDlg, "open", LIBUSB_URL, NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDC_LIBUSBK_URL:
			ShellExecuteA(hDlg, "open", LIBUSBK_URL, NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDC_WINUSB_URL:
			ShellExecuteA(hDlg, "open", WINUSB_URL, NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDC_WCID_URL:
			ShellExecuteA(hDlg, "open", WCID_URL, NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDC_VID_REPORT:
			ShellExecuteA(hDlg, "open", USB_IDS_URL, NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDC_EDITNAME:			// checkbox: "Edit Desc."
			toggle_edit();
			break;
		case IDC_DEVICEEDIT:		// edit: device description
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				GetDlgItemTextU(hMain, IDC_DEVICEEDIT, editable_desc, STR_BUFFER_SIZE);
				break;
			default:
				return (INT_PTR)FALSE;
			}
			break;
		case IDC_DEVICELIST:		// dropdown: device description
			switch (HIWORD(wParam)) {
			case CBN_SELCHANGE:
				device = get_selected_device();
				if (device != NULL) {
					// Change the description string if needed
					if (device->desc == NULL) {
						editable_desc = (char*)malloc(STR_BUFFER_SIZE);
						if (editable_desc == NULL) {
							dprintf("could not use modified device description");
							editable_desc = device->desc;
						} else {
							safe_sprintf(editable_desc, STR_BUFFER_SIZE, "(Unknown Device)");
							device->desc = editable_desc;
						}
					}
					// Display the current driver info
					replace_driver = (device->driver != NULL);
					if (replace_driver) {
						if (device->driver_version == 0) {
							safe_strcpy(driver_text, sizeof(driver_text), device->driver);
						} else {
							safe_sprintf(driver_text, sizeof(driver_text), "%s (v%d.%d.%d.%d)", device->driver,
								(int)((device->driver_version>>48)&0xffff), (int)((device->driver_version>>32)&0xffff),
								(int)((device->driver_version>>16)&0xffff), (int)(device->driver_version & 0xffff));
						}
					} else {
						safe_strcpy(driver_text, sizeof(driver_text), "(NONE)");
					}
					SetDlgItemTextU(hMain, IDC_DRIVER, driver_text);
					// Display the VID,PID,MI
					safe_sprintf(str_tmp, 5, "%04X", device->vid);
					SetDlgItemTextA(hMain, IDC_VID, str_tmp);
					// Display the vendor string as a tooltip
					destroy_tooltip(hVIDToolTip);
					vid_string = wdi_get_vendor_name(device->vid);
					unknown_vid = (vid_string == NULL);
					if (unknown_vid) {
						vid_string = "Vendor name could not be resolved.\nIf you know "
							"the name of this vendor, please consider submitting it to "
							"the USB ID Repository, by clicking the button on the right.";
					};
					hVIDToolTip = create_tooltip(GetDlgItem(hMain, IDC_VID), (char*)vid_string, unknown_vid?20000:-1);
					ShowWindow(GetDlgItem(hMain, IDC_VID_REPORT), unknown_vid?SW_SHOW:SW_HIDE);
					safe_sprintf(str_tmp, 5, "%04X", device->pid);
					SetDlgItemTextA(hMain, IDC_PID, str_tmp);
					if (device->is_composite) {
						safe_sprintf(str_tmp, 5, "%02X", device->mi);
						SetDlgItemTextA(hMain, IDC_MI, str_tmp);
						display_mi(TRUE);
					} else {
						display_mi(FALSE);
					}
					// Display the WCID status
					pd_options.use_wcid_driver = (safe_strncmp(device->compatible_id, ms_comp_hdr, safe_strlen(ms_comp_hdr)) == 0);
					has_wcid = (pd_options.use_wcid_driver)?WCID_TRUE:WCID_FALSE;
					if (has_wcid == WCID_TRUE) {
						SetDlgItemTextA(hMain, IDC_WCID, device->compatible_id + safe_strlen(ms_comp_hdr));
						// Select the driver according to the WCID (will be set to WDI_USER = unsupported if no match)
						for (wcid_type=WDI_WINUSB; wcid_type<WDI_LIBUSBK; wcid_type++) {
							if (safe_stricmp(device->compatible_id + safe_strlen(ms_comp_hdr), driver_name[wcid_type]) == 0) {
								break;
							}
						}
						if (wcid_type < WDI_USER) {
							for (i=WDI_WINUSB; i<WDI_USER; i++) {
								if ((i == wcid_type) && (wdi_is_driver_supported(i, NULL)))
									break;
							}
							if (i < WDI_USER) {
								pd_options.driver_type = i;
								set_driver();
							} else {
								pd_options.use_wcid_driver = FALSE;
							}
						}
					}
					EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), TRUE);
				} else {
					has_wcid = WCID_NONE;
				}
				update_ui();
				set_install_button();
				break;
			default:
				return (INT_PTR)FALSE;
			}
			break;
		// Install button
		case IDC_INSTALL:
			r = install_driver();
			if (id_options.install_filter_driver) {
				dsprintf("Driver Installation: %s", (r==WDI_SUCCESS)?"SUCCESS":"FAILED");
				break;
			}
			if (r == WDI_SUCCESS) {
				if (!extract_only) {
					dsprintf("Driver Installation: SUCCESS");
					notification(MSG_INFO, NULL, "Driver Installation", "The driver was installed successfully.");
				}
				if (exit_on_success)
					PostMessage(hMain, WM_CLOSE, 0, 0);
			} else if (r == WDI_ERROR_USER_CANCEL) {
				dsprintf("Driver Installation: Cancelled by User");
				notification(MSG_WARNING, NULL, "Driver Installation", "Driver installation cancelled by user.");
			} else {
				dsprintf("Driver Installation: FAILED (%s)", wdi_strerror(r));
				notification(MSG_ERROR, NULL, "Driver Installation", "The driver installation failed.");
			}
			break;
		case IDC_BROWSE:	// button: "Browse..."
			browse_for_folder();
			dprintf("Using '%s' as extraction directory.", extraction_path);
			break;
		case IDC_CLEAR:		// button: "Clear Log"
			SetWindowTextA(hInfo, "");
			break;
		case IDC_SAVE:		// button: "Save Log"
			log_size = GetWindowTextLengthU(hInfo);
			if (log_size == 0)
				break;
			log_buffer = (char*)malloc(log_size);
			if (log_buffer != NULL) {
				log_size = GetDlgItemTextU(hMain, IDC_INFO, log_buffer, log_size);
				if (log_size == 0) {
					dprintf("unable to read log text");
				} else {
					log_size--;	// remove NULL terminator
					filepath = file_dialog(TRUE, app_dir, "zadig.log", "log", "Zadig log");
					if (filepath != NULL) {
						file_io(TRUE, filepath, &log_buffer, &log_size);
					}
					safe_free(filepath);
				}
				safe_free(log_buffer);
			} else {
				dprintf("could not allocate buffer to save log");
			}
			break;
		case IDC_WCID_BOX:	// prevent focus
			if (HIWORD(wParam) == EN_SETFOCUS) {
				SetFocus(hMain);
				return (INT_PTR)TRUE;
			}
			break;
		case IDOK:			// close application
		case IDCANCEL:
			wdi_destroy_list(list);
			EndDialog(hDlg, 0);
			break;
		// Main Menus
		case IDM_CREATE:
			toggle_create(TRUE);
			break;
		case IDM_OPEN:
			filepath = file_dialog(FALSE, app_dir, "sample.cfg", "cfg", "Zadig device config");
			parse_preset(filepath);
			break;
		case IDM_ABOUT:
			DialogBoxW(main_instance, MAKEINTRESOURCEW(IDD_ABOUTBOX), hMain, about_callback);
			break;
		case IDM_UPDATES:
			DialogBoxW(main_instance, MAKEINTRESOURCEW(IDD_UPDATE_POLICY), hMain, UpdateCallback);
			break;
		case IDM_CERTMGR:
			memset(&si, 0, sizeof(si));
			si.cb = sizeof(si);
			memset(&pi, 0, sizeof(pi));
			if (!CreateProcessU(NULL, "mmc certmgr.msc", NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
				dsprintf("Unable to launch the Certificate Manager");
			}
			break;
		case IDM_ONLINEHELP:
			ShellExecuteA(hDlg, "open", HELP_URL, NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDM_ADVANCEDMODE:
			toggle_advanced();
			break;
		case IDM_LISTALL:
			toggle_driverless(TRUE);
			break;
		case IDM_IGNOREHUBS:
			toggle_hubs(TRUE);
			break;
		case IDM_CREATECAT:
			pd_options.disable_cat = GetMenuState(hMenuOptions, IDM_CREATECAT, MF_CHECKED) & MF_CHECKED;
			EnableMenuItem(hMenuOptions, IDM_SIGNCAT, pd_options.disable_cat?MF_GRAYED:MF_ENABLED);
			CheckMenuItem(hMenuOptions, IDM_CREATECAT, pd_options.disable_cat?MF_UNCHECKED:MF_CHECKED);
			break;
		case IDM_SIGNCAT:
			pd_options.disable_signing = GetMenuState(hMenuOptions, IDM_SIGNCAT, MF_CHECKED) & MF_CHECKED;
			CheckMenuItem(hMenuOptions, IDM_SIGNCAT, pd_options.disable_signing?MF_UNCHECKED:MF_CHECKED);
			break;
		case IDM_LOGLEVEL_ERROR:
		case IDM_LOGLEVEL_WARNING:
		case IDM_LOGLEVEL_INFO:
		case IDM_LOGLEVEL_DEBUG:
			set_loglevel(LOWORD(wParam));
			break;
		// Split button menu
		case IDM_SPLIT_INSTALL:
		case IDM_SPLIT_WCID:
		case IDM_SPLIT_FILTER:
		case IDM_SPLIT_EXTRACT:
			id_options.install_filter_driver = (LOWORD(wParam)==IDM_SPLIT_FILTER);
			pd_options.use_wcid_driver = ( (LOWORD(wParam) == IDM_SPLIT_WCID)
				|| ((device == NULL) && (!create_device)) );
			extract_only = (LOWORD(wParam) == IDM_SPLIT_EXTRACT);
			set_install_button();
			break;
		default:
			return (INT_PTR)FALSE;
		}
		return (INT_PTR)TRUE;

	case WM_CLOSE:
		PostQuitMessage(0);
		destroy_all_tooltips();
		break;

	default:
		return (INT_PTR)FALSE;

	}
	return (INT_PTR)FALSE;
}

/*
 * Application Entrypoint
 */
#if defined(_MSC_VER) && (_MSC_VER >= 1600)
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
	HANDLE mutex = NULL;
	HWND hDlg = NULL;
	MSG msg;
	/* Deletion of libusb-1.0 DLLs magic command
	 * Remember that a 32 bit app running on a 64 bit system has to use "Sysnative"
	 * to access the actual "System32" as "SysWOW64" gets remapped to "System32"
	 */
	const char* system_dir[] = { "System32", "SysWOW64", "Sysnative" };
	char path[MAX_PATH], *tmp;
	int i, wait_for_mutex = 0;
	BOOL r;

	// Disable loading system DLLs from the current directory (DLL sideloading mitigation)
#ifndef DDKBUILD	// WDK doesn't know about that one
	SetDllDirectoryA("");
#endif

	// Retrieve the current application directory
	GetCurrentDirectoryU(MAX_PATH, app_dir);

	// Prevent 2 applications from running at the same time, unless "/W" is passed as an option
	// in which case we wait for the mutex to be relinquished
	if ((safe_strlen(lpCmdLine)==2) && (lpCmdLine[0] == '/') && (lpCmdLine[1] == 'W'))
		wait_for_mutex = 150;		// Try to acquire the mutex for 15 seconds
	mutex = CreateMutexA(NULL, TRUE, "Global/" APPLICATION_NAME);
	for (;(wait_for_mutex>0) && (mutex != NULL) && (GetLastError() == ERROR_ALREADY_EXISTS); wait_for_mutex--) {
		CloseHandle(mutex);
		Sleep(100);
		mutex = CreateMutexA(NULL, TRUE, "Global/" APPLICATION_NAME);
	}
	if ((mutex == NULL) || (GetLastError() == ERROR_ALREADY_EXISTS)) {
		MessageBoxA(NULL, "Another Zadig application is running.\n"
			"Please close the first application before running another one.",
			"Other instance detected", MB_ICONSTOP);
		safe_closehandle(mutex);
		return 0;
	}

	// Set the Windows version
	GetWindowsVersion();

	// Alert users if they are running versions older than Windows 7
	if (nWindowsVersion < WINDOWS_7) {
		MessageBoxA(NULL, "This version of Zadig can only be run on Windows 7 or later",
			"Incompatible version", MB_ICONSTOP);
		CloseHandle(mutex);
		return 0;
	}

	// Save instance of the application for further reference
	main_instance = hInstance;

	// Initialize COM for folder selection
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));

	// Some dialogs have Rich Edit controls and won't display without this
	GetDLLHandle("Riched20.dll");

	// Retrieve the current application directory and set the extraction directory from the user's
	GetCurrentDirectoryU(MAX_PATH, app_dir);
	tmp = getenvU("USERPROFILE");
	safe_sprintf(extraction_path, sizeof(extraction_path), "%s\\usb_driver", tmp);
	safe_free(tmp);

	// Create the main Window
	if ( (hDlg = CreateDialogA(hInstance, "MAIN_DIALOG", NULL, main_callback)) == NULL ) {
		MessageBoxA(NULL, "Could not create Window", "DialogBox failure", MB_ICONSTOP);
	}
	ShowWindow(hDlg, SW_SHOWNORMAL);
	UpdateWindow(hDlg);

	// Do our own event processing, in order to process "magic" commands
	while(GetMessage(&msg, NULL, 0, 0)) {
		// Alt-Z => Delete libusb-1.0 DLLs
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'Z')) {
			for (r = TRUE, i = 0; i<ARRAYSIZE(system_dir); i++) {
				path[0] = 0;
				safe_strcpy(path, MAX_PATH, getenvU("WINDIR"));
				safe_strcat(path, MAX_PATH, "\\");
				safe_strcat(path, MAX_PATH, system_dir[i]);
				safe_strcat(path, MAX_PATH, "\\libusb-1.0.dll");
				// coverity[tainted_string]
				DeleteFileA(path);
				if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
					r = FALSE;
				}
			}
			if (r) {
				dsprintf("Successfully deleted the libusb-1.0 system DLLs");
			} else {
				dsprintf("Could not remove the libusb-1.0 system32 DLLs");
			}
			continue;
		}
		// Alt-R => Remove all the registry keys created by Zadig
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'R')) {
			dsprintf(DeleteRegistryKey(REGKEY_HKCU, COMPANY_NAME "\\" APPLICATION_NAME)?
				"Application registry keys successfully deleted":"Failed to delete application registry keys");
			// Also try to delete the upper key (company name) if it's empty (don't care about the result)
			DeleteRegistryKey(REGKEY_HKCU, COMPANY_NAME);
			continue;
		}

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	CloseHandle(mutex);
#ifdef _CRTDBG_MAP_ALLOC
	_CrtDumpMemoryLeaks();
#endif

	return 0;
}
