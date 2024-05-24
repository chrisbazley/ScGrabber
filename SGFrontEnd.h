/*
 *  ScreenGrabber (desktop front-end)
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

#ifndef SGFrontEnd_h
#define SGFrontEnd_h

#include <stdbool.h>

typedef enum
{
  RepeatType_AutoSync,
  RepeatType_HalfSync,
  RepeatType_Interval
}
RepeatType;

extern int wimp_version;
extern char task_name[];

extern char *save_path; /* Base filename for screenshots */
extern bool grab_enable; /* Is hotkey enabled? */
extern bool force_film; /* If true then a single press starts filming */
extern bool save_palette; /* Should the palette be saved with screenshots? */
extern bool new_sprite; /* Generate new (RISC OS 3.5) sprite type */
extern RepeatType repeat_type;
extern unsigned int interval; /* Delay time in cs between screenshot */
extern unsigned int key_code; /* Internal key number */

extern int main(int argc, const char *argv[]);

#endif


