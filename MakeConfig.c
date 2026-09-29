/*
 *  ScreenGrabber (make configuration command)
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

/* 04.09.09 CJB Created this source file.
   27.09.26 CJB Require a non-null output buffer, even for size queries.
   27.09.26 CJB Use the correct format specifiers for pointers and sizes.
   27.09.26 CJB Defer the output-length declaration until construction.
   29.09.26 CJB Build configuration commands in a CBUtilLib string buffer.
*/

/* ANSI headers */
#include <stdio.h>
#include <assert.h>
#include <limits.h>

/* CBLibrary headers */
#include "Debug.h"

/* CBUtilLib headers */
#include "StringBuff.h"

/* Local headers */
#include "SGFrontEnd.h"
#include "MakeConfig.h"

bool make_config_cmd(StringBuffer *buffer)
{
  char interval_text[sizeof("Interval ") + sizeof(unsigned int) * CHAR_BIT];
  const char *sync;

  assert(buffer != NULL);
  assert(save_path != NULL);
  if (save_path == NULL)
    return false;
  const char *const path = &*save_path;

  switch (repeat_type)
  {
    case RepeatType_AutoSync:
      sync = "AutoSync";
      break;

    case RepeatType_HalfSync:
      sync = "HalfSync";
      break;

    default:
      assert(repeat_type == RepeatType_Interval);
      (void)sprintf(interval_text, "Interval %u", interval);
      sync = interval_text;
      break;
  }

  bool const success = stringbuffer_printf(buffer,
      "SGrabConfigure -%s -%s -KeyCode %d -%s -%s -%s -Filename %s",
      grab_enable ? "On" : "Off",
      force_film ? "Film" : "Single",
      key_code,
      sync,
      save_palette ? "Palette" : "NoPalette",
      new_sprite ? "NewSprite" : "OldSprite",
      path);

  if (success)
    DEBUGF("Command is '%s'\n", stringbuffer_get_pointer(buffer));

  return success;
}
