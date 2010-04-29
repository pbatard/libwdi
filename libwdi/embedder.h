/*
 * embedder : converts binary resources into a .h include
 * "If you can think of a better way to get ice, I'd like to hear it."
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
#pragma once

#if !defined(OPT_M32) && !defined(OPT_M64)
#error both 32 and 64 bit support have been disabled - check your config.h
#endif

// Some adjustment is needed for MSVC
#if defined(_MSC_VER)
#define __STR2__(x) #x
#define __STR1__(x) __STR2__(x)
// TODO: feed preprocessor arg to custom step?
#if defined(_WIN64) && defined(OPT_M32)
// a 64 bit application/library CANNOT be used on 32 bit platforms
#pragma message(__FILE__ "(" __STR1__(__LINE__) ") : warning : library is compiled as 64 bit - disabling 32 bit support")
#undef OPT_M32
#endif
#endif

/*
 * Defines where we should we pick the 32 and 64 bit installer exes to embed
 */
#if defined(_MSC_VER) && !defined(DDKBUILD)
#if defined(_DEBUG)
#define INSTALLER_PATH_32 "..\\Win32\\Debug\\lib"
#define INSTALLER_PATH_64 "..\\x64\\Debug\\lib"
#else
#define INSTALLER_PATH_32 "..\\Win32\\Release\\lib"
#define INSTALLER_PATH_64 "..\\x64\\Release\\lib"
#endif
#else
// If you compile with shared libs, DON'T PICK THE EXE IN "installer",
// as it won't run from ANYWHERE ELSE! Use the one from .libs instead.
#define INSTALLER_PATH_32 "."
#define INSTALLER_PATH_64 "."
#endif
