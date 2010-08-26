/* libusb-win32, Generic Windows USB Library
* Copyright (c) 2002-2006 Stephan Meyer <ste_meyer@web.de>
* Copyright (c) 2010 Travis Robinson <libusbdotnet@gmail.com>
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifdef __GNUC__
	#if !defined(WINVER)
		#define WINVER 0x0500
	#endif
	#if !defined(_WIN32_IE)
		#define _WIN32_IE 0x0501
	#endif
#endif

#define INITGUID
#include "libusb-win32_version.h"
#include "libwdi.h"

#include <windows.h>
#include <commdlg.h>
#include <dbt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <initguid.h>
#include <commctrl.h>
#include <setupapi.h>
#include <time.h>

#define __INF_WIZARD_C__
#include "inf_wizard_rc.rc"

#define MAX_TEXT_LENGTH 256

// Copied from libwdi
#define safe_free(p) do {if (p != NULL) {free((void*)p); p = NULL;}} while(0)
#define safe_min(a, b) min((size_t)(a), (size_t)(b))
#define safe_strcp(dst, dst_max, src, count) do {memcpy(dst, src, safe_min(count, dst_max)); \
	((char*)dst)[safe_min(count, dst_max)-1] = 0;} while(0)
#define safe_strcpy(dst, dst_max, src) safe_strcp(dst, dst_max, src, safe_strlen(src)+1)
#define safe_strncat(dst, dst_max, src, count) strncat(dst, src, safe_min(count, dst_max - safe_strlen(dst) - 1))
#define safe_strcat(dst, dst_max, src) safe_strncat(dst, dst_max, src, safe_strlen(src)+1)
#define safe_strcmp(str1, str2) strcmp(((str1==NULL)?"<NULL>":str1), ((str2==NULL)?"<NULL>":str2))
#define safe_strncmp(str1, str2, count) strncmp(((str1==NULL)?"<NULL>":str1), ((str2==NULL)?"<NULL>":str2), count)
#define safe_closehandle(h) do {if (h != INVALID_HANDLE_VALUE) {CloseHandle(h); h = INVALID_HANDLE_VALUE;}} while(0)
#define safe_sprintf _snprintf
#define safe_strlen(str) ((((char*)str)==NULL)?0:strlen(str))
#define safe_swprintf _snwprintf
#define safe_strdup _strdup

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)


// Used for device notification
DEFINE_GUID(GUID_DEVINTERFACE_USB_HUB, 0xf18a0e88, 0xc30c, 0x11d0, 0x88, \
			0x15, 0x00, 0xa0, 0xc9, 0x06, 0xbe, 0xd8);

DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE, 0xA5DCBF10L, 0x6530, 0x11D2, \
			0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);

typedef struct
{
	struct wdi_device_info* wdi;

	char description[MAX_PATH];
	char manufacturer[MAX_PATH];

	char inf_name[MAX_PATH];
	char inf_dir [MAX_PATH];
	char inf_path[MAX_PATH];

	BOOL user_allocated_wdi;
	BOOL modified; // unused

	VS_FIXEDFILEINFO* driver_info;
} device_context_t;

typedef struct
{
	UINT controlID;
	const char* message;
}create_tooltip_t;

const char info_text_0[] =
"This program will create an .inf file for your device.\n\n"
"Before clicking \"Next\" make sure that your device is connected to the "
"system.\n";

const char info_text_1[] =
"A windows driver installation package has been created for the following "
"device:";

const char package_contents_fmt_0[] =
"This package contains %s v%d.%d.%d.%d drivers and support for the following platforms: %s.";

const char package_contents_fmt_1[] =
"This package contains an inf file only.";


const char list_header_text[] =
"Select your device from the list of detected devices below. "
"If your device isn't listed then either connect it or click \"Next\" "
"and enter your device description manually.";

create_tooltip_t tooltips_dlg2[]=
{
	{ID_TEXT_VID,
	"A VID is a 16-bit vendor number (Vendor ID). A vendor ID is "
	"necessary for developing a USB product. The USB-IF is responsible "
	"for issuing USB vendor ID's to product manufacturers."},

	{ID_TEXT_PID,
	"A PID is a 16-bit product number (Product ID)."},

	{ID_TEXT_MI,
	"If not blank, creates a driver package targeting a specific interface."},

	{ID_TEXT_MANUFACTURER,
	"Manufacturer or vendor string."},

	{ID_TEXT_DEV_NAME,
	"Name or description."},

	{0,NULL}
};

create_tooltip_t tooltips_dlg3[]=
{
	{ID_BUTTON_INSTALLNOW,
	"Install this driver package now."},

	{ID_BUTTON_NEXT,
	"Finish without installing."},

	{0,NULL}
};

create_tooltip_t tooltips_dlg1[]=
{
	{ID_LIST,
	LPSTR_TEXTCALLBACK},

	{0,NULL}
};

HICON mIcon;

struct wdi_device_info* wdi_dev_list = NULL;

HINSTANCE g_hInst = NULL;
WNDPROC device_list_wndproc_orig;

TOOLINFO g_toolItem;

HWND g_hwndTrackingTT = NULL;
BOOL g_TrackingMouse = FALSE;

HWND create_tooltip(HWND hMain, HINSTANCE hInstance, UINT max_tip_width, create_tooltip_t tool_tips[]);
HWND CreateTrackingToolTip(HWND hDlg, TCHAR* pText);

HWND create_label(char* text, HWND hParent, HINSTANCE hInstance, UINT x, UINT y, UINT cx, UINT cy, DWORD dwStyle, UINT uID);
HWND create_labeled_text(char* label, char* text,
						 HWND hParent, HINSTANCE hInstance,
						 UINT left, UINT top, UINT height,
						 UINT label_width, UINT text_width,
						 UINT uIDLabel, UINT uIDText);

BOOL CALLBACK dialog_proc_0(HWND dialog, UINT message,
							WPARAM wParam, LPARAM lParam);
BOOL CALLBACK dialog_proc_1(HWND dialog, UINT message,
							WPARAM wParam, LPARAM lParam);
BOOL CALLBACK dialog_proc_2(HWND dialog, UINT message,
							WPARAM wParam, LPARAM lParam);
BOOL CALLBACK dialog_proc_3(HWND dialog, UINT message,
							WPARAM wParam, LPARAM lParam);

static void device_list_init(HWND list);
static void device_list_refresh(HWND list);
static void device_list_add(HWND list, device_context_t *device);
static void device_list_clean(HWND list);

static int save_file(HWND dialog, device_context_t *device);

void close_file(FILE** file);

int infwizard_install_driver(HWND dialog, device_context_t *device);
int infwizard_prepare_driver(HWND dialog, device_context_t *device);

void output_debug(char* format,...)
{
	va_list args;
	char msg[256];

	va_start (args, format);
	vsprintf(msg, format, args);
	va_end (args);

	OutputDebugStringA(msg);
}

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE prev_instance,
					 LPSTR cmd_line, int cmd_show)
{
	device_context_t device;
	int next_dialog;

	LoadLibrary("comctl32.dll");
	InitCommonControls();

	memset(&device, 0, sizeof(device));

	next_dialog = ID_DIALOG_0;

	mIcon = LoadIcon(instance, MAKEINTRESOURCE(IDR_MAIN_ICON));

	g_hInst = instance;

	while (next_dialog)
	{
		g_TrackingMouse = FALSE;

		switch (next_dialog)
		{
		case ID_DIALOG_0:
			next_dialog = (int)DialogBoxParam(instance,
				MAKEINTRESOURCE(next_dialog),
				NULL, (DLGPROC)dialog_proc_0,
				(LPARAM)&device);

			break;
		case ID_DIALOG_1:
			next_dialog = (int)DialogBoxParam(instance,
				MAKEINTRESOURCE(next_dialog),
				NULL, (DLGPROC)dialog_proc_1,
				(LPARAM)&device);
			break;
		case ID_DIALOG_2:
			next_dialog = (int)DialogBoxParam(instance,
				MAKEINTRESOURCE(next_dialog),
				NULL, (DLGPROC)dialog_proc_2,
				(LPARAM)&device);
			break;
		case ID_DIALOG_3:
			next_dialog = (int)DialogBoxParam(instance,
				MAKEINTRESOURCE(next_dialog),
				NULL, (DLGPROC)dialog_proc_3,
				(LPARAM)&device);
			break;
		default:
			;
		}
	}

	if (device.user_allocated_wdi)
	{
		device.user_allocated_wdi = FALSE;
		if (device.wdi)
		{
			free(device.wdi);
			device.wdi = NULL;
		}
	}

	if (wdi_dev_list)
	{
		wdi_destroy_list(wdi_dev_list);
		wdi_dev_list = NULL;
	}

	if (mIcon)
	{
		DestroyIcon(mIcon);
		mIcon = NULL;
	}
	return 0;
}

BOOL CALLBACK dialog_proc_0(HWND dialog, UINT message,
							WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
		SendMessage(dialog,WM_SETICON,ICON_SMALL, (LPARAM)mIcon);
		SendMessage(dialog,WM_SETICON,ICON_BIG,   (LPARAM)mIcon);
		SetWindowText(GetDlgItem(dialog, ID_INFO_TEXT), info_text_0);
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_BUTTON_NEXT:
			EndDialog(dialog, ID_DIALOG_1);
			return TRUE ;
		case ID_BUTTON_CANCEL:
		case IDCANCEL:
			EndDialog(dialog, 0);
			return TRUE ;
		}
	}

	return FALSE;
}

INT_PTR CALLBACK device_list_wndproc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	TCHAR tipText[80];
	POINT pt;
	static int oldX, oldY;
	int newX, newY;
	LVHITTESTINFO hitTestInfo;
	LVITEM lvitem;
	device_context_t* dev_context;

	switch(message)
	{
	case WM_MOUSELEAVE:
		// The mouse pointer has left our window.
		// Deactivate the tooltip.
		SendMessage(g_hwndTrackingTT, TTM_TRACKACTIVATE, (WPARAM)FALSE, (LPARAM)&g_toolItem);
		g_TrackingMouse = FALSE;
		return FALSE;

	case WM_MOUSEMOVE:

		if (!g_TrackingMouse)
		{
			// The mouse has just entered the window.

			// Request notification when the mouse leaves.
			TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
			tme.hwndTrack = hDlg;
			tme.dwFlags = TME_LEAVE;
			TrackMouseEvent(&tme);

			// Activate the tooltip.
			SendMessage(g_hwndTrackingTT, TTM_TRACKACTIVATE,
				(WPARAM)TRUE, (LPARAM)&g_toolItem);
			g_TrackingMouse = TRUE;
		}

		newX = LOWORD(lParam);
		newY = HIWORD(lParam);

		// Make sure the mouse has actually moved. The presence of the tooltip
		// causes Windows to send the message continuously.
		if ((newX != oldX) || (newY != oldY))
		{
			oldX = newX;
			oldY = newY;

			memset(&hitTestInfo,0,sizeof(hitTestInfo));
			hitTestInfo.pt.x = newX;
			hitTestInfo.pt.y = newY;

			if ((ListView_HitTest(hDlg, &hitTestInfo) == -1) || newX > ListView_GetColumnWidth(hDlg, 0))
			{
				safe_sprintf(tipText, sizeof(tipText) - 1, TEXT("%s"), TEXT(""));
				SendMessage(g_hwndTrackingTT, TTM_TRACKACTIVATE,FALSE, (LPARAM)&g_toolItem);
			}
			else
			{
				SendMessage(g_hwndTrackingTT, TTM_SETDELAYTIME,TTDT_INITIAL, 1000);

				memset(&lvitem, 0 , sizeof(lvitem));

				lvitem.iItem = hitTestInfo.iItem;
				lvitem.mask =  LVIF_PARAM;
				ListView_GetItem(hDlg,&lvitem);

				dev_context = (device_context_t*)lvitem.lParam;
				// Update the text.
				safe_sprintf(tipText, sizeof(tipText)-1 , TEXT("%s"), wdi_get_vendor_name(dev_context->wdi->vid));
				SendMessage(g_hwndTrackingTT, TTM_TRACKACTIVATE,TRUE, (LPARAM)&g_toolItem);

			}
			g_toolItem.lpszText = tipText;
			SendMessage(g_hwndTrackingTT, TTM_SETTOOLINFO, 0, (LPARAM)&g_toolItem);

			// Position the tooltip.
			// The coordinates are adjusted so that the tooltip does not
			// overlap the mouse pointer.
			pt.x = newX;
			pt.y = newY;

			ClientToScreen(hDlg, &pt);
			SendMessage(g_hwndTrackingTT, TTM_TRACKPOSITION,
				0, (LPARAM)MAKELONG(pt.x + 10, pt.y - 20));
		}
		break;
	}
	return CallWindowProc(device_list_wndproc_orig, hDlg, message, wParam, lParam);
}

BOOL CALLBACK dialog_proc_1(HWND dialog, UINT message,
							WPARAM wParam, LPARAM lParam)
{
	static HDEVNOTIFY notification_handle_hub = NULL;
	static HDEVNOTIFY notification_handle_dev = NULL;
	DEV_BROADCAST_HDR *hdr = (DEV_BROADCAST_HDR *) lParam;
	DEV_BROADCAST_DEVICEINTERFACE dev_if;
	static device_context_t *device = NULL;
	HWND list = GetDlgItem(dialog, ID_LIST);
	LVITEM item;

	switch (message)
	{
	case WM_INITDIALOG:
		SendMessage(dialog,WM_SETICON,ICON_SMALL, (LPARAM)mIcon);
		SendMessage(dialog,WM_SETICON,ICON_BIG,   (LPARAM)mIcon);

		device = (device_context_t *)lParam;
		if (device->user_allocated_wdi)
		{
			if (device->wdi)
			{
				free(device->wdi);
				device->wdi = NULL;
			}
			device->user_allocated_wdi = FALSE;
		}
		g_hwndTrackingTT = CreateTrackingToolTip(list, TEXT(" "));

#if defined(_WIN64)
		device_list_wndproc_orig = (WNDPROC)SetWindowLongPtr(list, GWLP_WNDPROC, (UINT_PTR)device_list_wndproc);
#else
		device_list_wndproc_orig = (WNDPROC)SetWindowLongPtr(list, GWL_WNDPROC, (UINT_PTR)device_list_wndproc);
#endif

		memset(device, 0, sizeof(*device));

		SetWindowText(GetDlgItem(dialog, ID_LIST_HEADER_TEXT), list_header_text);
		device_list_init(list);
		device_list_refresh(list);

		dev_if.dbcc_size = sizeof(dev_if);
		dev_if.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

		dev_if.dbcc_classguid = GUID_DEVINTERFACE_USB_HUB;
		notification_handle_hub = RegisterDeviceNotification(dialog, &dev_if, 0);

		dev_if.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;
		notification_handle_dev = RegisterDeviceNotification(dialog, &dev_if, 0);

		return TRUE;

	case WM_DEVICECHANGE:
		switch (wParam)
		{
		case DBT_DEVICEREMOVECOMPLETE:
			if (hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
				device_list_refresh(list);
			break;
		case DBT_DEVICEARRIVAL:
			if (hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
				device_list_refresh(list);
			break;
		default:
			;
		}
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_BUTTON_NEXT:
			if (notification_handle_hub)
				UnregisterDeviceNotification(notification_handle_hub);
			if (notification_handle_dev)
				UnregisterDeviceNotification(notification_handle_dev);

			memset(&item, 0, sizeof(item));
			item.mask = LVIF_TEXT | LVIF_PARAM;
			item.iItem = ListView_GetNextItem(list, -1, LVNI_SELECTED);

			memset(device, 0, sizeof(*device));

			if (item.iItem >= 0)
			{
				if (ListView_GetItem(list, &item))
				{
					if (item.lParam)
					{
						memcpy(device, (void *)item.lParam, sizeof(*device));
					}
				}
			}

			if (!device->wdi)
			{
				device->user_allocated_wdi = TRUE;
				device->wdi = malloc(sizeof(struct wdi_device_info));
				memset(device->wdi,0,sizeof(struct wdi_device_info));

				device->wdi->vid = 0x12AB;
				device->wdi->pid = 0x12AB;
			}

			if (!device->manufacturer[0])
				strcpy(device->manufacturer, "Insert manufacturer name");
			if (!device->description[0])
				strcpy(device->description,  "Insert device description");

			if (notification_handle_hub)
				UnregisterDeviceNotification(notification_handle_hub);
			if (notification_handle_dev)
				UnregisterDeviceNotification(notification_handle_dev);

			device_list_clean(list);

			EndDialog(dialog, ID_DIALOG_2);
			return TRUE;

		case ID_BUTTON_BACK:
			device_list_clean(list);
			if (notification_handle_hub)
				UnregisterDeviceNotification(notification_handle_hub);
			if (notification_handle_dev)
				UnregisterDeviceNotification(notification_handle_dev);
			EndDialog(dialog, ID_DIALOG_0);
			return TRUE ;

		case ID_BUTTON_CANCEL:
		case IDCANCEL:
			device_list_clean(list);
			if (notification_handle_hub)
				UnregisterDeviceNotification(notification_handle_hub);
			if (notification_handle_dev)
				UnregisterDeviceNotification(notification_handle_dev);
			EndDialog(dialog, 0);
			return TRUE ;
		}
	}

	return FALSE;
}

BOOL CALLBACK dialog_proc_2(HWND dialog, UINT message,
							WPARAM wParam, LPARAM lParam)
{
	static device_context_t *device = NULL;
	static HWND hToolTip;
	char tmp[MAX_TEXT_LENGTH];
	int val;

	switch (message)
	{

	case WM_INITDIALOG:
		SendMessage(dialog,WM_SETICON,ICON_SMALL, (LPARAM)mIcon);
		SendMessage(dialog,WM_SETICON,ICON_BIG,   (LPARAM)mIcon);

		device = (device_context_t *)lParam;

		if (device)
		{
			wdi_is_driver_supported(WDI_LIBUSB, &device->driver_info);

			//g_hwndTrackingTT = CreateTrackingToolTip(dialog,TEXT(" "));
			hToolTip = create_tooltip(dialog, g_hInst, 300, tooltips_dlg2);

			memset(tmp, 0, sizeof(tmp));
			safe_sprintf(tmp,sizeof(tmp) - 1, "0x%04X", device->wdi->vid);
			SetWindowText(GetDlgItem(dialog, ID_TEXT_VID), tmp);

			memset(tmp, 0, sizeof(tmp));
			safe_sprintf(tmp,sizeof(tmp) - 1, "0x%04X", device->wdi->pid);
			SetWindowText(GetDlgItem(dialog, ID_TEXT_PID), tmp);


			memset(tmp, 0, sizeof(tmp));
			if (device->wdi->is_composite)
				safe_sprintf(tmp,sizeof(tmp) - 1, "0x%02X", device->wdi->mi);
			SetWindowText(GetDlgItem(dialog, ID_TEXT_MI), tmp);

			SetWindowText(GetDlgItem(dialog, ID_TEXT_MANUFACTURER),
				device->manufacturer);

			SetWindowText(GetDlgItem(dialog, ID_TEXT_DEV_NAME),
				device->description);
		}
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_BUTTON_NEXT:
			//memset(device, 0, sizeof(*device));
			device->wdi->is_composite=false;

			GetWindowText(GetDlgItem(dialog, ID_TEXT_MANUFACTURER),
				device->manufacturer, sizeof(tmp));

			GetWindowText(GetDlgItem(dialog, ID_TEXT_DEV_NAME),
				device->description, sizeof(tmp));

			GetWindowText(GetDlgItem(dialog, ID_TEXT_VID), tmp, sizeof(tmp));
			if(sscanf(tmp, "0x%04x", &val) == 1)
				device->wdi->vid = (WORD)val;

			GetWindowText(GetDlgItem(dialog, ID_TEXT_PID), tmp, sizeof(tmp));
			if(sscanf(tmp, "0x%04x", &val) == 1)
				device->wdi->pid = (WORD)val;

			GetWindowText(GetDlgItem(dialog, ID_TEXT_MI), tmp, sizeof(tmp));

			if (sscanf(tmp, "0x%02x", &val) == 1)
			{
				device->wdi->mi = (BYTE)val;
				device->wdi->is_composite=true;
			}
			if (save_file(dialog, device))
				EndDialog(dialog, ID_DIALOG_3);
			return TRUE ;
		case ID_BUTTON_BACK:
			EndDialog(dialog, ID_DIALOG_1);
			return TRUE ;
		case ID_BUTTON_CANCEL:
		case IDCANCEL:
			EndDialog(dialog, 0);
			return TRUE ;
		}
	}

	return FALSE;
}
HWND create_label(char* text, HWND hParent, HINSTANCE hInstance, UINT x, UINT y, UINT cx, UINT cy, DWORD dwStyle, UINT uID)
{
	return CreateWindowA("Static", text, WS_CHILD | WS_VISIBLE | dwStyle,
		x, y, cx, cy,
		hParent, (HMENU)((UINT_PTR)uID), hInstance, 0);
}

HWND create_labeled_text(char* label, char* text,
						 HWND hParent, HINSTANCE hInstance,
						 UINT left, UINT top, UINT height,
						 UINT label_width, UINT text_width,
						 UINT uIDLabel, UINT uIDText)
{
	HWND hwnd = NULL;
	HFONT dlgFont;
	LOGFONT logFont;
	HFONT labelFont;

	// Get the font from the parent dialog
	dlgFont = (HFONT)SendMessage(hParent,WM_GETFONT,0,0);

	if (label)
	{
		// convert it to a logfont
		GetObject(dlgFont,sizeof(logFont),&logFont);

		// make it bold
		logFont.lfWeight*=2;

		// convert it back to HFONT
		labelFont = CreateFontIndirectA(&logFont);
	}
	else
	{
		labelFont = dlgFont;
	}

	if (label)
	{
		// create the label text and set the label (bold) font
		hwnd = create_label(label, hParent, hInstance, left, top, label_width, height, SS_LEFT, uIDLabel);
		SendMessage(hwnd, WM_SETFONT, (WPARAM)labelFont, TRUE);
	}
	if (text)
	{
		if (label)
		{
			text_width-=5;
			left+=label_width+5;
		}
		// create the label text and set the dialog font
		hwnd = create_label(text, hParent, hInstance, left, top, text_width, height, SS_LEFT, uIDText);
		SendMessage(hwnd,WM_SETFONT, (WPARAM)dlgFont, TRUE);
	}
	return hwnd;
}

BOOL CALLBACK dialog_proc_3(HWND dialog, UINT message,
							WPARAM wParam, LPARAM lParam)
{
	static device_context_t *device = NULL;
	char* bufferLabel = NULL;
	char* bufferText = NULL;
	int ret;
	UINT x,y;
	UINT TXT_WIDTH = 200;
	UINT LBL_WIDTH = 150;
	UINT LBL_HEIGHT = 15;
	UINT LBL_SEP = 5;
	HWND hwnd;
	static HBRUSH hBrushStatic = NULL;

	RECT rect;

	switch (message)
	{
	case WM_INITDIALOG:
		SendMessage(dialog,WM_SETICON,ICON_SMALL, (LPARAM)mIcon);
		SendMessage(dialog,WM_SETICON,ICON_BIG,   (LPARAM)mIcon);

		device = (device_context_t *)lParam;

		create_tooltip(dialog, g_hInst, 300, tooltips_dlg3);

		bufferLabel  = malloc(MAX_TEXT_LENGTH*2);
		bufferText   = bufferLabel+MAX_TEXT_LENGTH;
		if (bufferLabel)
		{
			GetWindowRect(GetDlgItem(dialog,IDG_MAIN),&rect);
			TXT_WIDTH = rect.right-rect.left-30-LBL_WIDTH;

			y = 40;
			x = 30;
			safe_sprintf(bufferLabel, MAX_TEXT_LENGTH, "%s", info_text_1);
			create_labeled_text(bufferLabel,NULL, dialog, g_hInst, x, y, LBL_HEIGHT * 2,LBL_WIDTH+TXT_WIDTH,0, ID_TEXT_HIGHLIGHT_INFO, ID_TEXT_HIGHLIGHT_INFO);

			y += LBL_HEIGHT*2+LBL_SEP*2;
			safe_strcpy(bufferLabel, MAX_TEXT_LENGTH, "Vendor ID:");
			safe_sprintf(bufferText, MAX_TEXT_LENGTH, "0x%04X", device->wdi->vid);
			create_labeled_text(bufferLabel,bufferText,dialog,g_hInst,x,y,LBL_HEIGHT,LBL_WIDTH,TXT_WIDTH, ID_INFO_TEXT, ID_INFO_TEXT);

			y += LBL_HEIGHT+LBL_SEP;
			safe_strcpy(bufferLabel, MAX_TEXT_LENGTH, "Product ID:");
			safe_sprintf(bufferText, MAX_TEXT_LENGTH, "0x%04X", device->wdi->pid);
			create_labeled_text(bufferLabel,bufferText,dialog,g_hInst,x,y,LBL_HEIGHT,LBL_WIDTH,TXT_WIDTH, ID_INFO_TEXT, ID_INFO_TEXT);

			if (device->wdi->is_composite)
			{
				y += LBL_HEIGHT+LBL_SEP;
				safe_strcpy(bufferLabel, MAX_TEXT_LENGTH, "Interface # (MI):");
				safe_sprintf(bufferText, MAX_TEXT_LENGTH, "0x%02X", device->wdi->mi);
				create_labeled_text(bufferLabel,bufferText,dialog,g_hInst,x,y,LBL_HEIGHT,LBL_WIDTH,TXT_WIDTH, ID_INFO_TEXT, ID_INFO_TEXT);
			}

			y += LBL_HEIGHT+LBL_SEP;
			safe_strcpy(bufferLabel, MAX_TEXT_LENGTH, "Device description:");
			safe_sprintf(bufferText, MAX_TEXT_LENGTH, "%s", device->description);
			create_labeled_text(bufferLabel,bufferText,dialog,g_hInst,x,y,LBL_HEIGHT,LBL_WIDTH,TXT_WIDTH, ID_INFO_TEXT, ID_INFO_TEXT);

			y += LBL_HEIGHT+LBL_SEP;
			safe_strcpy(bufferLabel, MAX_TEXT_LENGTH, "Manufacturer:");
			safe_sprintf(bufferText, MAX_TEXT_LENGTH, "%s", device->manufacturer);
			create_labeled_text(bufferLabel,bufferText,dialog,g_hInst,x,y,LBL_HEIGHT,LBL_WIDTH,TXT_WIDTH, ID_INFO_TEXT, ID_INFO_TEXT);

			y += LBL_HEIGHT+LBL_SEP*2;
			if  (device->driver_info)
			{
				safe_sprintf(bufferLabel, MAX_TEXT_LENGTH, package_contents_fmt_0, "libusb-win32",
					(int)device->driver_info->dwFileVersionMS>>16, (int)device->driver_info->dwFileVersionMS&0xFFFF,
					(int)device->driver_info->dwFileVersionLS>>16, (int)device->driver_info->dwFileVersionLS&0xFFFF,
					"x86, x64, ia64");

			}
			else
			{
				safe_sprintf(bufferLabel, MAX_TEXT_LENGTH, "%s", package_contents_fmt_1);
			}
			hwnd = create_labeled_text(NULL,bufferLabel,dialog,g_hInst,x,y,LBL_HEIGHT*2, 0, LBL_WIDTH+TXT_WIDTH, ID_TEXT_HIGHLIGHT_INFO, ID_TEXT_HIGHLIGHT_INFO);

			free(bufferLabel);

		}
		if ((device->driver_info) && GetFileAttributesA(device->inf_path)!=INVALID_FILE_ATTRIBUTES)
			EnableWindow(GetDlgItem(dialog, ID_BUTTON_INSTALLNOW), TRUE);
		else
			EnableWindow(GetDlgItem(dialog, ID_BUTTON_INSTALLNOW), FALSE);

		return TRUE;
	case WM_CTLCOLORSTATIC:
		{
			///
			/// By responding to this message, the parent window can use the
			/// specified device context handle to set the text and background
			/// colors of the static control.
			///
			/// If an application processes this message, the return value is a
			/// handle to a brush that the system uses to paint the background
			/// of the static control.
			///
			if(ID_TEXT_HIGHLIGHT_INFO == GetDlgCtrlID((HWND)lParam))
			{
				SetBkColor((HDC) wParam, (COLORREF) GetSysColor(COLOR_BTNFACE));

				SetTextColor((HDC) wParam, GetSysColor(COLOR_ACTIVECAPTION));
				SetBkMode((HDC) wParam, TRANSPARENT);

				if(!hBrushStatic)
				{
					hBrushStatic = CreateSolidBrush( (COLORREF) GetSysColor(COLOR_BTNFACE));
				}

				return PtrToUlong(hBrushStatic);
			}

			// Let Windows do default handling
			return FALSE;
		}
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_BUTTON_INSTALLNOW:
			// Disable the install now button (forever)
			EnableWindow(GetDlgItem(dialog,ID_BUTTON_INSTALLNOW),FALSE);

			// Set the wait cursor and installing text
			SetCursor(LoadCursor(NULL,IDC_WAIT));
			SetWindowText(GetDlgItem(dialog, IDL_INSTALLING_TEXT), "Installing driver, please wait..");

			// Install the driver
			ret = infwizard_install_driver(dialog, device);

			// Clear installing text and restore the arrow cursor
			SetWindowText(GetDlgItem(dialog, IDL_INSTALLING_TEXT), "");
			SetCursor(LoadCursor(NULL,IDC_ARROW));

			// infwizard_install_driver() will display a message if it fails
			if (ret == ERROR_SUCCESS)
			{
				MessageBoxA(dialog,"Installation successful.",
					"Driver Install Complete", MB_OK | MB_APPLMODAL);

				// Close the wizard
				EndDialog(dialog, 0);
			}
			return TRUE;
		case ID_BUTTON_NEXT:
		case IDCANCEL:
			EndDialog(dialog, 0);
			return TRUE ;
		}
	}

	return FALSE;
}

static void device_list_init(HWND list)
{
	LVCOLUMN lvc;

	ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);

	memset(&lvc, 0, sizeof(lvc));

	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
	lvc.fmt = LVCFMT_LEFT;

	lvc.cx = 70;
	lvc.iSubItem = 0;
	lvc.pszText = "Vendor ID";
	ListView_InsertColumn(list, 1, &lvc);

	lvc.cx = 70;
	lvc.iSubItem = 1;
	lvc.pszText = "Product ID";
	ListView_InsertColumn(list, 2, &lvc);

	lvc.cx = 260;
	lvc.iSubItem = 2;
	lvc.pszText = "Description";
	ListView_InsertColumn(list, 3, &lvc);

	lvc.cx = 40;
	lvc.iSubItem = 3;
	lvc.pszText = "MI";
	ListView_InsertColumn(list, 4, &lvc);
}

static void device_list_refresh(HWND list)
{
	int ret;
	device_context_t *device;
	struct wdi_device_info* wdi_dev_info;
	struct wdi_options_create_list options;
	const char* vendor_name;

	memset(&options,0,sizeof(options));
	options.list_all=TRUE;
	options.list_hubs=FALSE;
	options.trim_whitespaces=TRUE;

	device_list_clean(list);

	if (wdi_dev_list)
		wdi_destroy_list(wdi_dev_list);

	ret = wdi_create_list(&wdi_dev_list,&options);
	if (ret == WDI_SUCCESS)
	{
		for(wdi_dev_info=wdi_dev_list; wdi_dev_info!=NULL; wdi_dev_info=wdi_dev_info->next)
		{
			device = (device_context_t *) malloc(sizeof(device_context_t));
			memset(device, 0, sizeof(*device));
			device->wdi=wdi_dev_info;
			safe_strcpy(device->description,MAX_PATH,device->wdi->desc);

			if ((vendor_name = wdi_get_vendor_name(device->wdi->vid)))
			{
				safe_strcpy(device->manufacturer,MAX_PATH, vendor_name);
			}

			device_list_add(list, device);
		}
	}
}

static void device_list_add(HWND list, device_context_t *device)
{
	LVITEM item;
	char vid[32];
	char pid[32];
	char mi[32];

	memset(&item, 0, sizeof(item));
	memset(vid, 0, sizeof(vid));
	memset(pid, 0, sizeof(pid));
	memset(mi, 0, sizeof(mi));

	sprintf(vid, "0x%04X", device->wdi->vid);
	sprintf(pid, "0x%04X", device->wdi->pid);
	if (device->wdi->is_composite)
	{
		sprintf(mi, "0x%02X", device->wdi->mi);
	}

	item.mask = LVIF_TEXT | LVIF_PARAM;
	item.lParam = (LPARAM)device;

	ListView_InsertItem(list, &item);

	ListView_SetItemText(list, 0, 0, vid);
	ListView_SetItemText(list, 0, 1, pid);
	ListView_SetItemText(list, 0, 2, device->description);
	ListView_SetItemText(list, 0, 3, mi);
}

static void device_list_clean(HWND list)
{
	LVITEM item;

	memset(&item, 0, sizeof(LVITEM));

	while (ListView_GetItem(list, &item))
	{
		if (item.lParam)
			free((void *)item.lParam);

		ListView_DeleteItem(list, 0);
		memset(&item, 0, sizeof(LVITEM));
	}
}

static int save_file(HWND dialog, device_context_t *device)
{
	OPENFILENAME open_file;
	char* c;
	int length;

	memset(&open_file, 0, sizeof(open_file));
	memset(device->inf_path, 0, sizeof(device->inf_path));
	memset(device->inf_dir,0,sizeof(device->inf_dir));
	memset(device->inf_name,0,sizeof(device->inf_name));

	if (strlen(device->description))
	{
		if (_stricmp(device->description,"Insert device description")!=0)
		{
			strcpy(device->inf_path, device->description);
			c=device->inf_path;
			while(c[0])
			{
				if (c[0]>='A' && c[0]<='Z') { c++; continue;}
				if (c[0]>='a' && c[0]<='z') { c++; continue;}
				if (c[0]>='0' && c[0]<='9') { c++; continue;}

				switch(c[0])
				{
				case '_':
				case ' ':
				case '.':
					c[0]='_';
					break;
				default: // remove
					if (!c[1])
						c[0]='\0';
					else
						memmove(c,c+1,strlen(c+1)+1);
					break;
				}

				c++;
			}
		}
	}
	if (!strlen(device->inf_path))
		strcpy(device->inf_path, "your_file.inf");

	open_file.lStructSize = sizeof(OPENFILENAME);
	open_file.hwndOwner = dialog;
	open_file.lpstrFile = device->inf_path;
	open_file.nMaxFile = sizeof(device->inf_path);
	open_file.lpstrFilter = "*.inf\0*.inf\0";
	open_file.nFilterIndex = 1;
	open_file.lpstrFileTitle = device->inf_name;
	open_file.nMaxFileTitle = sizeof(device->inf_name);
	open_file.lpstrInitialDir = NULL;
	open_file.Flags = OFN_PATHMUSTEXIST;
	open_file.lpstrDefExt = "inf";

	if (GetSaveFileName(&open_file))
	{
		safe_strcpy(device->inf_dir, MAX_PATH, device->inf_path);

		// strip the filename
		length = (int)strlen(device->inf_dir);
		while (length)
		{
			length--;
			if (device->inf_dir[length]=='\\' || device->inf_dir[length]=='/')
			{
				device->inf_dir[length]='\0';
				break;
			}

			device->inf_dir[length]='\0';
		}

		return infwizard_prepare_driver(dialog, device) == WDI_SUCCESS;
	}
	return FALSE;
}

int infwizard_prepare_driver(HWND dialog, device_context_t *device)
{
	struct wdi_options_prepare_driver options;
	int ret;

	memset(&options,0,sizeof(options));
	options.driver_type = WDI_LIBUSB;
	options.vendor_name = device->manufacturer;

	if (device->wdi->desc)
		free(device->wdi->desc);
	device->wdi->desc = safe_strdup(device->description);

	if ((ret = wdi_prepare_driver(device->wdi, device->inf_dir, device->inf_name, &options) != WDI_SUCCESS))
	{
		MessageBoxA(dialog, wdi_strerror(ret),"Error Preparing Driver", MB_OK|MB_ICONWARNING);
	}

	return ret;
}

int infwizard_install_driver(HWND dialog, device_context_t *device)
{
	struct wdi_options_install_driver options;
	int ret;

	memset(&options,0,sizeof(options));
	options.hWnd = dialog;
	if ((ret = wdi_install_driver(device->wdi, device->inf_dir, device->inf_name, &options)) != WDI_SUCCESS)
	{
		MessageBoxA(dialog, wdi_strerror(ret),"Error Installing Driver", MB_OK|MB_ICONWARNING);
	}

	return ret;
}

/*
* Create a tooltip for the controls in tool_tips
*/
HWND create_tooltip(HWND hMain, HINSTANCE hInstance, UINT max_tip_width, create_tooltip_t tool_tips[])
{
	HWND hTip;
	TOOLINFO toolInfo = {0};
	int i;

	// Create the tooltip window
	hTip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,	hMain, NULL,
		hInstance, NULL);

	if (hTip == NULL) {
		return (HWND)NULL;
	}

	// Associate the tooltip to the control
	toolInfo.cbSize = sizeof(toolInfo);
	toolInfo.hwnd = hMain;
	toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;

	for (i=0; tool_tips[i].controlID != 0 && tool_tips[i].message != NULL; i++)
	{
		toolInfo.uId =(UINT_PTR)GetDlgItem(hMain,tool_tips[i].controlID);
		toolInfo.lpszText = (LPSTR)tool_tips[i].message;
		SendMessage(hTip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
	}

	SendMessage(hTip, TTM_SETMAXTIPWIDTH, 0, max_tip_width);

	return hTip;
}
HWND CreateTrackingToolTip(HWND hDlg, TCHAR* pText)
{
	// Create a tooltip.
	HWND hwndTT = CreateWindowEx(WS_EX_TOPMOST,
		TOOLTIPS_CLASS, NULL,
		WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT,
		hDlg, NULL, g_hInst,NULL);

	if (!hwndTT)
	{
		return NULL;
	}

	// Set up tool information.
	// In this case, the "tool" is the entire parent window.
	g_toolItem.cbSize = sizeof(TOOLINFO);
	g_toolItem.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
	g_toolItem.hwnd = hDlg;
	g_toolItem.hinst = g_hInst;
	g_toolItem.lpszText = pText;
	g_toolItem.uId = (UINT_PTR)hDlg;
	GetClientRect (hDlg, &g_toolItem.rect);

	// Associate the tooltip with the tool window.
	SendMessage(hwndTT, TTM_ADDTOOL, 0, (LPARAM) (LPTOOLINFO) &g_toolItem);
	SendMessage(hwndTT, TTM_SETMAXTIPWIDTH, 0, 300);

	return hwndTT;
}

// These are used to flag end users about the driver they are going to replace
enum driver_type {
	DT_SYSTEM,
	DT_LIBUSB,
	DT_UNKNOWN,
	DT_NONE,
	NB_DRIVER_TYPES,
};

// Retrieve the driver type according to its service string
int get_driver_type(struct wdi_device_info* dev)
{
	int i;
	const char* libusb_name[] = { "WinUSB", "libusb0" };
	const char* system_name[] = { "usbhub", "usbccgp", "USBSTOR", "HidUsb"};

	if ((dev == NULL) || (dev->driver == NULL)) {
		return DT_NONE;
	}
	for (i=0; i<sizeof(libusb_name)/sizeof(libusb_name[0]); i++) {
		if (safe_strcmp(dev->driver, libusb_name[i]) == 0) {
			return DT_LIBUSB;
		}
	}
	for (i=0; i<sizeof(system_name)/sizeof(system_name[0]); i++) {
		if (safe_strcmp(dev->driver, system_name[i]) == 0) {
			return DT_SYSTEM;
		}
	}
	return DT_UNKNOWN;
}
