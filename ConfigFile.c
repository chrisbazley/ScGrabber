/*
 *  ScreenGrabber (configuration file handling)
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

/* 03.09.09 CJB Moved this code to a separate source file of its own.
*/

/* ANSI headers */
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

/* RISC OS headers */
#include "kernel.h"

/* CBLibrary headers */
#include "msgtrans.h"
#include "strextra.h"
#include "macros.h"
#include "debug.h"

/* Local headers */
#include "SGFrontEnd.h"
#include "ConfigFile.h"

#ifdef FORTIFY
#include "FORTIFY:FORTIFY.h"
#endif

static char *repeat_type_str = NULL;

static const struct
{
  enum
  {
    Type_Boolean,
    Type_Integer,
    Type_String
  }
  type;
  void *value;
  const char *name;
}
config_map[] =
{
  {
    Type_Boolean,
    &grab_enable,
    "enable_key"
  },
  {
    Type_Boolean,
    &save_palette,
    "save_palette"
  },
  {
    Type_Boolean,
    &new_sprite,
    "new_sprite"
  },
  {
    Type_Boolean,
    &force_film,
    "allow_film"
  },
  {
    Type_String,
    &save_path,
    "save_path"
  },
  {
    Type_String,
    &repeat_type_str, /* anomalous: enum-as-string */
    "delay_type"
  },
  {
    Type_Integer,
    &key_code,
    "key_code"
  },
  {
    Type_Integer,
    &interval,
    "delay_time"
  }
};

/* ----------------------------------------------------------------------- */

const _kernel_oserror *save_config(const char *dest_file)
{
  const _kernel_oserror *e = NULL;
  FILE *f; /* output file handle */

  assert(dest_file != NULL);

  _kernel_last_oserror(); /* clear any previous OS error */

  f = fopen(dest_file, "w"); /* open text file for writing */
  if (f == NULL)
  {
    e = _kernel_last_oserror();
    if (e == NULL)
      e = msgs_error_subn(DUMMY_ERRNO, "OpenOutFail", 1, dest_file);
  }
  else
  {
    int chars_out; /* no. of characters transmitted by fprintf() */
    unsigned int i;

    /* We save the value of repeat_type as a string rather than an integer,
       so set up a temporary string value for that purpose. */
    switch (repeat_type)
    {
      case RepeatType_HalfSync:
        repeat_type_str = "half";
        break;

      case RepeatType_AutoSync:
        repeat_type_str = "auto";
        break;

      default:
        assert(repeat_type == RepeatType_Interval);
        repeat_type_str = "timed";
        break;
    }

    for (i = 0; i < ARRAY_SIZE(config_map) && e == NULL; i++)
    {
      switch (config_map[i].type)
      {
        case Type_Boolean:
          chars_out = fprintf(f,
                              "%s:%d\n",
                              config_map[i].name,
                              *(bool *)config_map[i].value ? 1 : 0);
          break;

        case Type_Integer:
          chars_out = fprintf(f,
                              "%s:%u\n",
                              config_map[i].name,
                              *(int *)config_map[i].value);
          break;

        default:
          assert(config_map[i].type == Type_String);
          chars_out = fprintf(f,
                              "%s:%s\n",
                              config_map[i].name,
                              *(char **)config_map[i].value);

          break;
      }
      if (chars_out <= 0)
      {
        e = _kernel_last_oserror();
        if (e == NULL)
          e = msgs_error_subn(DUMMY_ERRNO, "WriteFail", 1, dest_file);
      }
    }
    fclose(f);
  }

  return e;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *interpret_line(const char *line, const char *source_file, unsigned int line_num)
{
  bool mistake = false;
  const _kernel_oserror *e = NULL;
  unsigned int i;
  char *colon;

  assert(line != NULL);
  assert(source_file != NULL);

  /* This string will be allocated if we find a delay type specifier */
  repeat_type_str = NULL;

  /* Find the end of the variable name */
  colon = strchr(line, ':');
  if (colon == NULL)
  {
    DEBUGF("No colon in input line '%s'\n", line);
    mistake = true;
  }
  else
  {
    /* Calculate the length of the prefix in the input line */
    unsigned int name_len;
    const char *value, *end;

    assert(colon > line);
    name_len = colon - line;
    DEBUGF("Length of input line prefix is %d\n", name_len);

    value = colon + 1;
    DEBUGF("Value to assign is '%s'\n", value);

    /* Compare the prefix with each known variable name in turn */
    for (i = 0; i < ARRAY_SIZE(config_map); i++)
    {
      /* The names must match up to the end of the prefix, which
         must also be the end of the variable name */
      if (strncmp(line, config_map[i].name, name_len) == 0 &&
          config_map[i].name[name_len] == '\0')
      {
        DEBUGF("Input line prefix matches variable %u:%s\n",
               i, config_map[i].name);
        break;
      }
    }
    if (i < ARRAY_SIZE(config_map))
    {
      /* Having identified which variable to set, decode the rest of the
         input line */
      unsigned long input;

      switch (config_map[i].type)
      {
        case Type_Boolean:
          input = strtoul(value, (char **)&end, 10);
          if (*end == '\n')
          {
            *(bool *)config_map[i].value = (input != 0);
            DEBUGF("Got boolean value %s\n", (input != 0) ? "true" : "false");
          }
          else
          {
            DEBUGF("Boolean value has trailing junk: '%s'\n", end);
            mistake = true;
          }
          break;

        case Type_Integer:
          input = strtoul(value, (char **)&end, 10);
          if (*end == '\n')
          {
            *(int *)config_map[i].value = (int)input;
            DEBUGF("Got integer value %u\n", input);
          }
          else
          {
            DEBUGF("Integer value has trailing junk: '%s'\n", end);
            mistake = true;
          }
          break;

        default:
          assert(config_map[i].type == Type_String);

          /* Find end of string value (first whitespace character)
             Could use strpbrk here, but this is probably faster. */
          for (end = value; *end != '\0'; end++)
          {
            if (isspace(*end))
              break;
          }
          if (*end == '\n')
          {
            /* Allocate a buffer large enough for the string value and nul
               terminator */
            char *new_string = malloc(end - value + 1);
            if (new_string == NULL)
            {
              /* Insufficient free memory for string buffer */
              e = msgs_error(DUMMY_ERRNO, "NoMem");
            }
            else
            {
              /* Copy new string value into the buffer */
              strncpy(new_string, value, end - value);
              new_string[end - value] = '\0';
              DEBUGF("Got string value '%s'\n", new_string);

              /* Replace existing string value */
              free(*(char **)config_map[i].value);
              *(char **)config_map[i].value = new_string;
            }
          }
          else
          {
            DEBUGF("String value has trailing junk: '%s'\n", end);
            mistake = true;
          }
          break;
      }
    }
    else
    {
      DEBUGF("Unrecognised name in input line '%s'\n", line);
      mistake = true;
    }
  }

  /* We load the value of repeat_type as a string rather than an integer, so
     check whether a temporary string was allocated for that purpose. */
  if (repeat_type_str != NULL)
  {
    if (strcmp(repeat_type_str, "half") == 0)
    {
      repeat_type = RepeatType_HalfSync;
    }
    else if (strcmp(repeat_type_str, "auto") == 0)
    {
      repeat_type = RepeatType_AutoSync;
    }
    else if (strcmp(repeat_type_str, "timed") == 0)
    {
      repeat_type = RepeatType_Interval;
    }
    else
    {
      mistake = true; /* unrecognised parameter */
    }
    free(repeat_type_str);
  }

  if (mistake)
  {
    /* Report syntax error and line number */
    char string[12];
    (void)sprintf(string, "%u", line_num);
    e = msgs_error_subn(DUMMY_ERRNO, "Mistake", 2, string, source_file);
  }

  return e;
}

/* ----------------------------------------------------------------------- */

const _kernel_oserror *load_config(const char *source_file)
{
  const _kernel_oserror *e = NULL;
  FILE *f; /* input file handle */

  assert(source_file != NULL);

  _kernel_last_oserror(); /* clear any previous OS error */

  f = fopen(source_file, "r"); /* open text file for reading */
  if (f == NULL)
  {
    e = _kernel_last_oserror();
    if (e == NULL)
      e = msgs_error_subn(DUMMY_ERRNO, "OpenInFail", 1, source_file);
  }
  else
  {
    unsigned int line;
    char read_line[256];

    for (line = 1; e == NULL; line++)
    {
      char *got;
      int c;

      /* Read as much of the next line as will fit in our string buffer */
      got = fgets(read_line, sizeof(read_line), f);
      if (got == NULL)
      {
        /* Read error or end-of-file */
        break;
      }

      /* Skip comments and blank lines */
      if (*got == '#' || *got == '\n')
        continue;

      /* Check for buffer overflow */
      if (strlen(got) == sizeof(read_line) - 1)
      {
        (void)sprintf(read_line, "%u", line);
        e = msgs_error_subn(DUMMY_ERRNO, "TooLong", 2, read_line, source_file);
        break;
      }

      /* Find the index of the last non-whitespace character */
      for (c = strlen(got) - 1; c >= 0; c--)
      {
        if (!isspace(got[c]))
          break;
      }
      if (c < 0)
        continue; /* skip lines which consist only of whitespace */

      /* Strip any trailing spaces by overwriting them with a linefeed
         followed by a nul terminator, if there is room to do so. */
      if (c + 2 < sizeof(read_line))
      {
        got[c + 1] = '\n';
        got[c + 2] = '\0';
      }
      e = interpret_line(got, source_file, line);
    }

    fclose(f);
  }

  return e;
}
