/*
 *  ScreenGrabber (make configuration command)
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

#ifndef MakeConfig_h
#define MakeConfig_h

#include <stdbool.h>
#include "StringBuff.h"

/* Appends a *SGrabConfigure command to an initialized string buffer from
   the settings held by the front-end. Returns false if allocation fails.
   The caller must destroy the buffer even on failure. */
extern bool make_config_cmd(StringBuffer *buffer);

#endif


