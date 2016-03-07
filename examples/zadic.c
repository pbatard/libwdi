
/*
* Zadic: Automated Driver Installer for USB devices (Console version)
* Copyright (c) 2010-2016 Pete Batard <pete@akeo.ie>
* Copyright (c) 2015 PhracturedBlue <6xtc2h7upj@snkmail.com>
* Copyright (c) 2010 Joseph Marshall <jmarshall@gcdataconcepts.com>
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

/*
* WARNING: if any part of the resulting executable name contains "setup" or "instal(l)"
* it will require UAC elevation on Vista and later, and, when run from an MSYS shell,
* will produce a "sh: Bad file number" message.
* See the paragraph on Automatic Elevation at http://helpware.net/VistaCompat.htm
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

#define FLUSHER while(getchar() != 0x0A)
#define INF_NAME "libusb_device.inf"

void usage(void)
{
	printf("\n");
	printf("--noprompt         allows the program to end without prompting the user\n");
	printf("--usealldevices    lists all usb devices instead of only driverless ones\n");
	printf("--iface <num>      sets the interface number\n");
	printf("--vid <num>        sets the VID number. You must put 0x infront of vid number\n");
	printf("--pid <num>        sets the PID number. You must put 0x infront of pid number\n");
	printf("--useinf           use supplied .inf if it exists in the correct directory\n");
	printf("--create <desc>    create device even if USB device is not attached. Requires a description\n");
	printf("\n");
}

// The following is necessary when compiled from a WDK/DDK environment
int __cdecl main(int argc, char *argv[])
{
	int c;
	struct wdi_device_info *device, *list;
	char* path = "usb_driver";
	static struct wdi_options_create_list cl_options = { 0 };
	static struct wdi_options_prepare_driver pd_options = { 0 };

	static int prompt_flag = 1;
	static unsigned char iface = 0;
	static unsigned short vid = 0, pid = 0;
	static int verbose_flag = 3;
	static char *desc = NULL;
	static int use_supplied_inf_flag = 0;
	int r = WDI_ERROR_OTHER, option_index = 0;

	cl_options.trim_whitespaces = TRUE;

	// Parse command-line options
	while(1)
	{
		static struct option long_options[] = {
			// These options set a flag.
			{"noprompt", no_argument, &prompt_flag, 0},
			{"usealldevices", no_argument, &cl_options.list_all, 1},
			{"useinf", no_argument, &use_supplied_inf_flag, 1},
			{"iface", required_argument, 0, 'a'},
			{"vid", required_argument, 0, 'b'},
			{"pid", required_argument, 0, 'c'},
			{"help", no_argument, 0, 'd'},
			{"verbose", no_argument, &verbose_flag, 0},
			{"create", required_argument,0, 'e'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "abc:d:e:",long_options, &option_index);
		//  Detect the end of the options.
		if (c == -1)
			break;

		switch(c)
		{
		case 0:
			// If this option set a flag, do nothing else now.
			if (long_options[option_index].flag != 0) {
				break;
			}
			printf("option %s", long_options[option_index].name);
			if (optarg) {
				printf (" with arg %s", optarg);
			}
			printf("\n");
			break;
		case 'a':
			iface = (unsigned char)atoi(optarg);
			printf("OPT: interface number %d\n", iface);
			break;
		case 'b':
			vid = (unsigned short)strtol(optarg, NULL, 0);
			printf("OPT: VID number %d\n", vid);
			break;
		case 'c':
			pid = (unsigned short)strtol(optarg, NULL, 0);
			printf("OPT: PID number %d\n", pid);
			break;
		case 'd': // getopt_long already printed an error message.
			usage();
			exit(0);
			break;
		case 'e': //create requires a description argument
			desc = optarg;
			break;
		default:
			usage();
			abort();
		}
	}

	if (desc) {
		// If the device is created from scratch, override the existing device
		if (vid == 0 || pid == 0) {
			printf("Must specify both --vid and --pid with --create\n");
			return 1;
		}
		device = (struct wdi_device_info*)calloc(1, sizeof(struct wdi_device_info));
		if (device == NULL) {
			printf("could not create new device_info struct for installation");
			return 1;
		}
		device->desc = desc;
		device->vid = vid;
		device->pid = pid;
		printf("Creating USB device: device: \"%s\" (%04X:%04X)\n", device->desc, device->vid, device->pid);
		wdi_set_log_level(verbose_flag);
		if (wdi_prepare_driver(device, path, INF_NAME, &pd_options) == WDI_SUCCESS) {
			printf("installing wdi driver with <%s> at <%s>\n", INF_NAME, path);
			r = wdi_install_driver(device, path, INF_NAME, NULL);
			if (r != WDI_SUCCESS) {
				printf("Failed to install driver: %s\n", wdi_strerror(r));
			}
		}
		free(device);
		return (r == WDI_SUCCESS) ? 0 : 1;
	}

	r = wdi_create_list(&list, &cl_options);
	switch (r) {
	case WDI_SUCCESS:
		break;
	case WDI_ERROR_NO_DEVICE:
		printf("No driverless USB devices were found.\n");
		return 0;
	default:
		printf("wdi_create_list: error %s\n", wdi_strerror(r));
		return 0;
	}

	for (device = list; device != NULL; device = device->next) {
		printf("Found USB device: \"%s\" (%04X:%04X)\n", device->desc, device->vid, device->pid);
		wdi_set_log_level(verbose_flag);
		// If vid and pid have not been initialized
		// prompt user to install driver for each device
		if (vid == 0 || pid == 0) {
			printf("Do you want to install a driver for this device (y/n)?\n");
			c = (char) getchar();
			FLUSHER;
			if ((c != 'y') && (c != 'Y')) {
				continue;
			}
			// Otherwise a specific vid and pid have been given
			// so drivers will install automatically
		} else {
			// Is VID and PID a match for our device
			if ( (device->vid != vid) || (device->pid != pid)
			  || (device->mi != iface) ) {
				continue;
			}
		}
		// Does the user want to use a supplied .inf
		if (use_supplied_inf_flag == 0) {
			if (wdi_prepare_driver(device, path, INF_NAME, &pd_options) == WDI_SUCCESS) {
				printf("installing wdi driver with <%s> at <%s>\n",INF_NAME, path);
				wdi_install_driver(device, path, INF_NAME, NULL);
			}
		} else {
			printf("installing wdi driver with <%s> at <%s>\n",INF_NAME, path);
			wdi_install_driver(device, path, INF_NAME, NULL);
		}
	}
	wdi_destroy_list(list);

	// This is needed when ran in UAC mode
	if (prompt_flag) {
		printf("\nPress Enter to exit this program\n");
		FLUSHER;
	}
	return 0;
}
