/*
 * 7z.h: interface for libwdi 7z archive extraction
 *
 * Copyright (c) 2011 Pete Batard <pbatard@gmail.com>
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
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int  _7z_open(const uint8_t* arch, const size_t arch_size);
void _7z_close(void);
int  _7z_extract(const char* id, uint8_t** buf, size_t* buf_size);
void _7z_free(uint8_t* buf);

#ifdef __cplusplus
}
#endif
