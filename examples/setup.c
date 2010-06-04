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

#include <stdio.h>
#include "../installer/installer_library.h"

int
#ifdef _MSC_VER
__cdecl
#endif
main(void)
{
	struct driver_info *drv_info;

	drv_info = list_driverless();
	for (; drv_info != NULL; drv_info = drv_info->next) {
		if (create_inf(drv_info, "C:\\test", USE_WINUSB) == 0) {
			run_installer("C:\\test", drv_info->device_id);
		}
	}
	return 0;
}

