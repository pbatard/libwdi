/*
 * setup driver for driverless USB devices
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

#include <stdio.h>
#include "../libwdi/libwdi.h"

#define FLUSHER	while(getchar() != 0x0A)

int
// The following is necessary when compiled from a WDK/DDK environment
#ifdef _MSC_VER
__cdecl
#endif
main(void)
{
	struct wdi_device_info *device, *list;
	char c;

	list = wdi_create_list(true);
	if (list == NULL) {
		printf("No USB devices were found.\n");
		return 0;
	}

	for (device = list; device != NULL; device = device->next) {
		printf("Found driverless USB device: \"%s\" (%s:%s)\n", device->desc, device->vid, device->pid);
		printf("Do you want to install a driver for this device (y/n)?\n");
		c = (char) getchar();
		FLUSHER;
		if ((c!='y') && (c!='Y')) {
			continue;
		}
		if (wdi_create_inf(device, "C:\\test", WDI_WINUSB) == 0) {
			wdi_install_driver("C:\\test", device);
		}
	}
	wdi_destroy_list(list);
	return 0;
}
