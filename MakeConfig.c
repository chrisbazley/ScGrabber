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
*/

/* ANSI headers */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* CBLibrary headers */
#include "Debug.h"

/* Local headers */
#include "SGFrontEnd.h"
#include "MakeConfig.h"

int make_config_cmd(char *s, size_t n)
{
  char sync[32];
  int nchars;

  assert(s != NULL || n == 0);
  DEBUGF("Making config command with buffer %p of size %u\n", s, n);

  switch (repeat_type)
  {
    case RepeatType_AutoSync:
      strcpy(sync, "AutoSync");
      break;

    case RepeatType_HalfSync:
      strcpy(sync, "HalfSync");
      break;

    default:
      assert(repeat_type == RepeatType_Interval);
      sprintf(sync, "Interval %u", interval);
      break;
  }

  assert(save_path != NULL);

#ifdef OLD_SCL_STUBS
  {
    /* Temporary buffer in which to construct the first part of the command,
       which has a predictable maximum length */
    char temp[256];

    /* Construct the first part of the configuration command */
    nchars = sprintf(temp,
#else /* OLD_SCL_STUBS */
    nchars = snprintf(s, n,
#endif /* OLD_SCL_STUBS */
                     "SGrabConfigure -%s -%s -KeyCode %d -%s -%s -%s -Filename "
#ifndef OLD_SCL_STUBS
                     "%s"
#endif /* OLD_SCL_STUBS*/
                    ,grab_enable ? "On" : "Off",
                     force_film ? "Film" : "Single",
                     key_code,
                     sync,
                     save_palette ? "Palette" : "NoPalette",
                     new_sprite ? "NewSprite" : "OldSprite"
#ifndef OLD_SCL_STUBS
                    ,save_path);
#else /* OLD_SCL_STUBS*/
                    );
    DEBUGF("%d characters written by sprintf\n", nchars);
    assert(nchars < sizeof(temp)); /* guard against buffer overrun */
    assert(nchars == strlen(temp));

    /* If a buffer was specified then copy as much of the partial command
       string into it as will fit */
    if (n > 0)
    {
      DEBUGF("Copying up to %d chars into caller's buffer\n", n - 1);
      strncpy(s, temp, n - 1);

      /* If there is any space remaining in the caller's buffer then append
         as much of the file path as will fit */
      if (nchars < n - 1)
      {
        DEBUGF("Copying up to %d chars to offset %d in caller's buffer\n",
               n - 1 - nchars, nchars);
        strncpy(s + nchars, save_path, n - 1 - nchars);
      }

      DEBUGF("Adding terminator at offset %d\n", n - 1);
      s[n - 1] = '\0'; /* strncpy pads with zeros only if the source string is
                          shorter than the maximum no. of characters to copy. */
    }

    /* Number of characters that would have been output had a large enough
       buffer been supplied must include the length of the file path */
    nchars += strlen(save_path);
  }
#endif /* OLD_SCL_STUBS */

  DEBUGF("Length of command is %d characters\n", nchars);
  if (s != NULL)
    DEBUGF("Command is '%s'\n", s);

  return nchars;
}
