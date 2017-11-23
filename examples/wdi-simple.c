/*
* wdi-simple.c: Console Driver Installer for a single USB device
* Copyright (c) 2010-2016 Pete Batard <pete@akeo.ie>
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 3 of the License, or (at your option) any later version.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _MSC_VER
#include "getopt/getopt.h"
#else
#include <getopt.h>
#endif
#include "libwdi.h"

#if defined(_PREFAST_)
/* Disable "Banned API Usage:" errors when using WDK's OACR/Prefast */
#pragma warning(disable:28719)
/* Disable "Consider using 'GetTickCount64' instead of 'GetTickCount'" when using WDK's OACR/Prefast */
#pragma warning(disable:28159)
#endif

#define oprintf(...) do {if (!opt_silent) printf(__VA_ARGS__);} while(0)

/*
 * Change these values according to your device if
 * you don't want to provide parameters
 */
#define DESC        "Microsoft XBox Controller Type S"
#define VID         0x045E
#define PID         0x0289
#define INF_NAME    "usb_device.inf"
#define DEFAULT_DIR "usb_driver"


void usage(void)
{
	printf("\n");
	printf("-n, --name <name>          set the device name\n");
	printf("-f, --inf <name>           set the inf name\n");
	printf("-m, --manufacturer <name>  set the manufacturer name\n");
	printf("-v, --vid <id>             set the vendor ID (VID, use 0x prefix for hex)\n");
	printf("-p, --pid <id>             set the product ID (PID, use 0x prefix for hex)\n");
	printf("-i, --iid <id>             set the interface ID (MI)\n");
	printf("-t, --type <driver_type>   set the driver to install\n");
	printf("                           (0=WinUSB, 1=libusb-win32, 2=libusbK, 3=usbser, 4=custom)\n");
	printf("-w, --wcid                 use a WCID driver instead of a device-specific\n");
	printf("                           one (WinUSB, libusb-win32 or libusbK only)\n");
	printf("    --filter               use the libusb-win32 filter driver (requires -t1)\n");
	printf("-d, --dest <dir>           set the extraction directory\n");
	printf("-x, --extract              extract files only (don't install)\n");
	printf("-c, --cert <certname>      install certificate <certname> from the\n");
	printf("                           embedded user files as a trusted publisher\n");
	printf("    --stealth-cert         installs certificate above without prompting\n");
	printf("-s, --silent               silent mode\n");
	printf("-b, --progressbar=[HWND]   display a progress bar during install\n");
	printf("                           an optional HWND can be specified\n");
	printf("-o, --timeout              timeout (in millis) to wait for any pending installations\n");
	printf("-l, --log                  set log level (0 = debug, 4 = none)\n");
	printf("-h, --help                 display usage\n");
	printf("\n");
}

// from http://support.microsoft.com/kb/124103/
HWND GetConsoleHwnd(void)
{
	HWND hwndFound;
	char pszNewWindowTitle[128];
	char pszOldWindowTitle[128];

	GetConsoleTitleA(pszOldWindowTitle, 128);
	wsprintfA(pszNewWindowTitle,"%d/%d", GetTickCount(), GetCurrentProcessId());
	SetConsoleTitleA(pszNewWindowTitle);
	Sleep(40);
	hwndFound = FindWindowA(NULL, pszNewWindowTitle);
	SetConsoleTitleA(pszOldWindowTitle);
	return hwndFound;
}

int __cdecl main(int argc, char** argv)
{
	static struct wdi_device_info *ldev, dev = {NULL, VID, PID, FALSE, 0, DESC, NULL, NULL, NULL};
	static struct wdi_options_create_list ocl = { 0 };
	static struct wdi_options_prepare_driver opd = { 0 };
	static struct wdi_options_install_driver oid = { 0 };
	static struct wdi_options_install_cert oic = { 0 };
	static int opt_silent = 0, opt_extract = 0, log_level = WDI_LOG_LEVEL_WARNING;
	static BOOL matching_device_found;
	int c, r;
	char *inf_name = INF_NAME;
	char *ext_dir = DEFAULT_DIR;
	char *cert_name = NULL;

	static struct option long_options[] = {
		{"name", required_argument, 0, 'n'},
		{"inf", required_argument, 0, 'f'},
		{"manufacturer", required_argument, 0, 'm'},
		{"vid", required_argument, 0, 'v'},
		{"pid", required_argument, 0, 'p'},
		{"iid", required_argument, 0, 'i'},
		{"type", required_argument, 0, 't'},
		{"filter", no_argument, 0, 2},
		{"wcid", no_argument, 0, 'w'},
		{"dest", required_argument, 0, 'd'},
		{"cert", required_argument, 0, 'c'},
		{"extract", no_argument, 0, 'x'},
		{"silent", no_argument, 0, 's'},
		{"stealth-cert", no_argument, 0, 1},
		{"progressbar", optional_argument, 0, 'b'},
		{"log", required_argument, 0, 'l'},
		{"timeout", required_argument, 0, 'o'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	ocl.list_all = TRUE;
	ocl.list_hubs = TRUE;
	ocl.trim_whitespaces = TRUE;
	opd.driver_type = WDI_WINUSB;

	while(1)
	{
		c = getopt_long(argc, argv, "n:f:m:d:c:v:p:i:l:t:o:hxsb", long_options, NULL);
		if (c == -1)
			break;
		switch(c) {
		case 1: // --stealth-cert
			oic.disable_warning = TRUE;
			break;
		case 2: // --filter
			oid.install_filter_driver = TRUE;
			break;
		case 'n':
			dev.desc = optarg;
			break;
		case 'm':
			opd.vendor_name = optarg;
			break;
		case 'f':
			inf_name = optarg;
			break;
		case 'd':
			ext_dir = optarg;
			break;
		case 'c':
			cert_name = optarg;
			break;
		case 'v':
			dev.vid = (unsigned short)strtol(optarg, NULL, 0);
			break;
		case 'o':
			oid.pending_install_timeout = (DWORD)strtoul(optarg, NULL, 0);
			break;
		case 'p':
			dev.pid = (unsigned short)strtol(optarg, NULL, 0);
			break;
		case 'i':
			dev.is_composite = TRUE;
			dev.mi = (unsigned char)strtol(optarg, NULL, 0);
			break;
		case 't':
			opd.driver_type = (int)strtol(optarg, NULL, 0);
			break;
		case 'w':
			opd.use_wcid_driver = TRUE;
			break;
		case 'h':
			usage();
			exit(0);
			break;
		case 'x':
			opt_extract = 1;
			break;
		case 's':
			opt_silent = 1;
			log_level = WDI_LOG_LEVEL_NONE;
			break;
		case 'b':
			oid.hWnd = (optarg)?(HWND)(uintptr_t)strtol(optarg, NULL, 0):GetConsoleHwnd();
			oic.hWnd = oid.hWnd;
			break;
		case 'l':
			log_level = (int)strtol(optarg, NULL, 0);
			break;
		default:
			usage();
			exit(0);
		}
	}

	wdi_set_log_level(log_level);

	oprintf("Extracting driver files...\n");
	r = wdi_prepare_driver(&dev, ext_dir, inf_name, &opd);
	oprintf("  %s\n", wdi_strerror(r));
	if ((r != WDI_SUCCESS) || (opt_extract))
		return r;

	if (cert_name != NULL) {
		oprintf("Installing certificate '%s' as a Trusted Publisher...\n", cert_name);
		r = wdi_install_trusted_certificate(cert_name, &oic);
		oprintf("  %s\n", wdi_strerror(r));
	}

	oprintf("Installing driver(s)...\n");

	// Try to match against a plugged device to avoid device manager prompts
	matching_device_found = FALSE;
	if (wdi_create_list(&ldev, &ocl) == WDI_SUCCESS) {
		r = WDI_SUCCESS;
		for (; (ldev != NULL) && (r == WDI_SUCCESS); ldev = ldev->next) {
			if ( (ldev->vid == dev.vid) && (ldev->pid == dev.pid) && (ldev->mi == dev.mi) &&(ldev->is_composite == dev.is_composite) ) {
				
				dev.hardware_id = ldev->hardware_id;
				dev.device_id = ldev->device_id;
				matching_device_found = TRUE;
				oprintf("  %s: ", dev.hardware_id);
				fflush(stdout);
				r = wdi_install_driver(&dev, ext_dir, inf_name, &oid);
				oprintf("%s\n", wdi_strerror(r));
			}
		}
	}

	// No plugged USB device matches this one -> install driver
	if (!matching_device_found) {
		r = wdi_install_driver(&dev, ext_dir, inf_name, &oid);
		oprintf("  %s\n", wdi_strerror(r));
	}

	return r;
}
