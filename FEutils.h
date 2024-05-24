/*
 *  ScreenGrabber (misc front-end routines)
 *  Copyright (C) 2000  Chris Bazley
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public Licence as published by
 *  the Free Software Foundation; either version 2 of the Licence, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public Licence for more details.
 *
 *  You should have received a copy of the GNU General Public Licence
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef FEutils_h
#define FEutils_h

#include <stdbool.h>

/* Are the two ctrl-terminated strings are equal? (case sensitive) */
extern bool string_equals(const char *string1, const char *string2);

extern bool dialogue_confirm(const char *mess);

extern bool file_exists(const char *file_path);

#endif


