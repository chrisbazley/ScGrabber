/*
 *  ScreenGrabber (key names for desktop front-end)
 *  Copyright (C) 2009  Chris Bazley
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

#ifndef KeyNames_h
#define KeyNames_h

#include "kernel.h"

extern const _kernel_oserror *close_key_msgs(void);
extern const _kernel_oserror *open_key_msgs(void);
extern const _kernel_oserror *lookup_key_name(unsigned int key_code, const char **key_name);

#endif
