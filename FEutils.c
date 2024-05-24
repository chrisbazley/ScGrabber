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

/* 20.02.04 CJB Now forceably terminates strings that may have been truncated
                by strncpy().
   07.06.06 CJB Qualified arguments to string_equals() as 'const'.
                Got rid of function remove_iconised_window(), since it
                duplicates code in CBLibrary.
   22.06.06 CJB Imported functions file_exists() and read_line_comm() from
                SFeditor.
   03.09.09 CJB Moved read_line_comm() function to a new source file.
*/

/* ANSI headers */
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>

/* RISC OS headers */
#include "kernel.h"
#include "wimplib.h"
#include "wimp.h"

/* CBLibrary headers */
#include "err.h"
#include "Macros.h"
#include "msgtrans.h"
#include "WimpExtra.h"

/* Local headers */
#include "FEutils.h"
#include "SGFrontEnd.h"

/* Constant numeric values */
enum
{
  ContinueButton           = 3,
  MinExtErrorWimpVersion   = 321, /* Earliest version of window manager to
                                     support Wimp_ReportError extensions */
  OSFile_ReadCatInfoNoPath = 17   /* _kernel_osfile reason code */
};

bool dialogue_confirm(const char *mess)
{
  _kernel_oserror err_block;

  assert(mess != NULL);
  STRCPY_SAFE(err_block.errmess, mess);
  err_block.errnum = DUMMY_ERRNO;

  if (wimp_version >= MinExtErrorWimpVersion)
  {
    /* Nice error box */
    return (wimp_report_error(
                &err_block,
                Wimp_ReportError_UseCategory | Wimp_ReportError_CatWarning,
                task_name,
                NULL,
                NULL,
                msgs_lookup("ConButtons")) == ContinueButton);
  }
  else
  {
    /* Backwards compatibility */
    return (wimp_report_error(
                &err_block,
                Wimp_ReportError_OK | Wimp_ReportError_Cancel,
                task_name) == Wimp_ReportError_OK);
  }
}

/* Are two ctrl-terminated strings equal? (case sensitive) */
bool string_equals(const char *string1, const char *string2)
{
  assert(string1 != NULL);
  assert(string2 != NULL);

  while (!iscntrl(*string1) && !iscntrl(*string2))
  {
    if (*string1 != *string2)
      return false;
    string1++;
    string2++;
  }
  return true;
}

bool file_exists(const char *file_path)
{
  /* Read catalogue info for object without path */
  _kernel_osfile_block params;
  int object_type;

  assert(file_path != NULL);

  object_type = _kernel_osfile(OSFile_ReadCatInfoNoPath, file_path, &params);
  if (object_type == _kernel_ERROR)
    return false; /* if error then assume object doesn't exist */
  else
    return (object_type != 0); /* exists unless 'object not found' */
}
