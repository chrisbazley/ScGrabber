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

#include <stddef.h>

/* Makes a *SGrabConfigure command from the settings held by the front-end. If
   n is 0, nothing is written and s may be NULL. If is less than the required
   buffer size then the output will be truncated. Returns the number of
   characters that would have been written had n been sufficiently large, not
   counting the nul terminator. */
extern int make_config_cmd(char *s, size_t n);

#endif


