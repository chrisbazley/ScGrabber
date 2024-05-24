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

/* 03.09.09 CJB Adapted from existing code for Star Fighter 3000.
   21.10.09 CJB Updated to use additional MessageTrans SWI veneers.
*/

/* ANSI headers */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

/* RISC OS headers */
#include "kernel.h"
#include "toolbox.h"

/* CBLibrary headers */
#include "macros.h"
#include "msgtrans.h"
#include "messtrans.h"
#include "debug.h"

/* Local headers */
#include "KeyNames.h"

#ifdef FORTIFY
#include "FORTIFY:FORTIFY.h"
#endif

#define KB_PATH "<ScGrabber$Dir>.Keyboards."
#define TOKEN_PREFIX "K"

/* Miscellaneous numeric constants */
enum
{
  MaxKeyNameLen              = 31,
  MaxKeyTokenLen             = sizeof(TOKEN_PREFIX) + 12,
  MaxKeyFileNameLen          = sizeof(KB_PATH) + 12,
  OSByte_International       = 71,  /* _kernel_osbyte reason code */
  OSByte_InternationalReadKB = 255,
  OSByteR1ResultMask         = 0xff /* least significant 8 bits hold the return
                                       value of R1 */
};

static MessagesFD key_msgs_desc;
static void *key_msgs_file;

/* ----------------------------------------------------------------------- */

const _kernel_oserror *close_key_msgs(void)
{
  const _kernel_oserror *e = NULL;

  if (key_msgs_file != NULL)
  {
    /* Close the messages file that defines names for keys */
    DEBUGF("Closing key names file %p\n", key_msgs_file);
    e = messagetrans_close_file(&key_msgs_desc);
    free(key_msgs_file);
    key_msgs_file = NULL;
  }
  return e;
}

/* ----------------------------------------------------------------------- */

const _kernel_oserror *open_key_msgs(void)
{
  const _kernel_oserror *e = NULL;

  /* If the key names file is already open then there is nothing to do */
  if (key_msgs_file == NULL)
  {
    /* Read keyboard number */
    int kb_num =_kernel_osbyte(OSByte_International,
                               OSByte_InternationalReadKB,
                               0);
    if (kb_num == _kernel_ERROR)
    {
      DEBUGF("Failed to read keyboard number\n");
      e = _kernel_last_oserror();
    }
    else
    {
      char kb_file_path[MaxKeyFileNameLen + 1];
      size_t size;

      kb_num &= OSByteR1ResultMask; /* keyboard number is returned in R1 */
      DEBUGF("Keyboard number is %d\n", kb_num);

#ifdef OLD_SCL_STUBS
      sprintf(kb_file_path,
#else
      snprintf(kb_file_path,
               sizeof(kb_file_path),
#endif
               KB_PATH"%d",
               kb_num);

      DEBUGF("Key names file path is %s\n", kb_file_path);

      /* Find buffer size required for the messages file */
      e = messagetrans_file_info(kb_file_path, NULL, &size);
      if (e == NULL)
      {
        /* Allocate buffer for the message file */
        DEBUGF("Allocating %u bytes for key names file\n", size);
        key_msgs_file = malloc(size);
        if (key_msgs_file == NULL)
        {
          e = msgs_error(DUMMY_ERRNO, "NoMem");
        }
        else
        {
          /* Open the messages file */
          DEBUGF("About to open the key names file\n");
          e = messagetrans_open_file(&key_msgs_desc,
                                     kb_file_path,
                                     key_msgs_file);
          if (e != NULL)
          {
            DEBUGF("Failed to open key names file\n");
            free(key_msgs_file);
            key_msgs_file = NULL;
          }
        }
      }
      else
      {
        DEBUGF("Failed to read info on key names file\n");
      }
    }
  }
  else
  {
    DEBUGF("Key names file is already open\n");
  }

  return e;
}

/* ----------------------------------------------------------------------- */

const _kernel_oserror *lookup_key_name(unsigned int key_code,
                                       const char **key_name)
{
  static char key_name_buf[MaxKeyNameLen + 1];
  char token[MaxKeyTokenLen + 1];
  const _kernel_oserror *e = NULL;

  assert(key_name != NULL);

  if (key_msgs_file == NULL)
  {
    /* Messages file isn't open (perhaps because it doesn't exist) */
    DEBUGF("Key names file is not open\n");
    *key_name = NULL;
  }
  else
  {
#ifdef OLD_SCL_STUBS
    sprintf(token,
#else
    snprintf(token,
             sizeof(token),
#endif
             TOKEN_PREFIX"%u",
             key_code);

    DEBUGF("Looking up token '%s'\n", token);

    /* Look up a key name in the messages file */
    e = messagetrans_lookup(&key_msgs_desc,
                            token,
                            key_name_buf,
                            sizeof(key_name_buf),
                            NULL,
                            0);
    if (e == NULL)
    {
      *key_name = key_name_buf;
      DEBUGF("Found key name '%s'\n", *key_name);
    }
    else
    {
      *key_name = NULL;
      DEBUGF("Failed to look up key name\n");
    }
  }
  return e;
}
