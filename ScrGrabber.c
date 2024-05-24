/*
 *  ScreenGrabber (back-end module code)
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

/* 17.04.06 CJB Rewritten to avoid use of OS_SpriteOp 2 and hence the necessity
                of changing the graphics window from a callback (where the state
                of the VDU command queue is unknown).
   29.05.06 CJB Fixed bug in show_state_savepalette, which was actually
                displaying the hot key enable state. Widespread changes to use
                puts() or fputs() instead of printf() where appropriate.
   07.06.06 CJB Made it configurable which 'hot' key is used. Now uses CBlibrary
                function strdup() where appropriate.
                Got rid of function lowercase(); cmd_handler() now uses
                stricmp() instead of transforming the command argument string.
*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>

#include "kernel.h"
#include "swis.h"

#include "ScrGrabberHdr.h"

#include "SprFormats.h"
#include "StrExtra.h"
#include "PathTail.h"
#include "Macros.h"
#include "Debug.h"
#include "FileUtils.h"
#include "PalEntry.h"
#include "OSVDU.h"
#include "MessTrans.h"

#ifdef FORTIFY
#include "Fortify:Fortify.h"
#endif

#define MOD_NAME "ScreenGrabber"

/* Switches to control compilation */
#define SUPPORT_OS_310

/* My tests indicate that saving screenshots via the standard I/O functions
   is about 160% slower than using the Acorn library kernel directly! */
#undef USE_STDIO

#ifdef USE_STDIO
#define FWRITE(ptr, size, stream) \
    (fwrite((ptr), (size), 1, (stream)) == 1)
#else
#define FWRITE(ptr, size, stream) \
    (inout.dataptr = (ptr), inout.nbytes = (size), \
     _kernel_osgbpb(2, (stream), &inout) != _kernel_ERROR)
#endif

/* _kernel_osbyte reason codes */
enum
{
  OSByte_DisableEvent    = 13,
  OSByte_EnableEvent     = 14,
  OSByte_ReadVduStatus   = 117,
  OSByte_ScanKeyboard    = 129,
  OSByte_ReadCharAndMode = 135,
  OSByte_ReadDisplayBank = 251
};

/* VDU status flags */
#define VduStatus_PrintEnabled   (1u << 0)
#define VduStatus_PagedScroll    (1u << 2)
#define VduStatus_TextWindow     (1u << 3)
#define VduStatus_ShadowMode     (1u << 4)
#define VduStatus_JoinedCursors  (1u << 5)
#define VduStatus_CursorEditing  (1u << 6)
#define VduStatus_ScreenDisabled (1u << 7)

/* Special interval values */
enum
{
  Interval_HalfSync = -2,
  Interval_AutoSync = -1,
  Interval_Minimum = 2,  /* in centiseconds (50 Hz) */
  Interval_Default = 10  /* in centiseconds (10 Hz) */
};

/* Key transition states */
enum
{
  KeyTrans_Release,
  KeyTrans_Press
};

/* Internal key transition codes */
enum
{
  KeyCode_Print     = 13,
  KeyCode_LeftCtrl  = 59,
  KeyCode_RightCtrl = 97
};

/* Event handler return values */
enum
{
  EventHandler_Claim,
  EventHandler_PassOn
};

/* Miscellaneous other 'magic' numbers */
enum
{
  OSScreenMode_ReadMode = 1, /* OS_ScreenMode reason code */
  OSByteScanKeysReadVsn = 0, /* Read OS version identifier */
  OSByteScanKeysNoLimit = 0xff,
  OSByteR1ResultMask    = 0xff,
  OSByteR2ResultMask    = 0xff00,
  OSByteR2ResultShift   = 8,
  ErrorNum_NoMem        = 0x81a720, /* Allocated from C.Bazley's chunk */
  Service_ModeChange    = 0x46,
  AutoSyncInterval      = 2,  /* Check for display bank swap 50 times per
                                 second */
  NumSuffixLen          = 5,  /* Number of decimal digits in file name */
  KeyTrans              = 11, /* Event number */
  EventV                = 16, /* Vector number */
  LastNumberedMode      = 255, /* Highest valid screen mode number */
  MaxPaletteSize        = 256, /* Maximum number of palette entries */
  MinOSVersion          = 0xa5 /* Identifier of earliest OS version to
                                  provide SWI OS_ScreenMode */
};

/* Must keep the following enumeration synchronised with the array of
   mode variable numbers */
enum
{
  VarIndex_ModeFlags,
  VarIndex_NColour,
  VarIndex_XEigFactor,
  VarIndex_YEigFactor,
  VarIndex_LineLength,
  VarIndex_ScreenSize,
  VarIndex_Log2BPP,
  VarIndex_XWindLimit,
  VarIndex_YWindLimit,
  VarIndex_LAST
};

/* Keep the following enumeration synchronised with the syntax string
   for command *SGrabConfigure. */
enum
{
  Arg_On,
  Arg_Off,
  Arg_Single,
  Arg_Film,
  Arg_KeyName,
  Arg_KeyCode,
  Arg_Interval,
  Arg_AutoSync,
  Arg_HalfSync,
  Arg_NoPalette,
  Arg_Palette,
  Arg_NewSprite,
  Arg_OldSprite,
  Arg_Filename,
  Arg_END
};

/* Type encapsulating one or more mutually-exclusive command line arguments and
   a handler function to be called when one is found by OS_ReadArgs. */
typedef struct
{
  void (*show)( void );
  const _kernel_oserror * (*handler)( void *value, int index );
  int cmd_no;
  struct
  {
    int index;
    const char *string;
  }
  states[4];
}
ArgSwitch;

extern const _kernel_oserror error_hotkey_syntax, error_palette_syntax, error_film_syntax, error_filmdelay_syntax, error_bad_interval, error_unknown_command, error_configure_syntax, error_uk_key_name;

/* Module state: */
static unsigned int internal_key_no = KeyCode_Print; /* New 07.06.2006 */
static unsigned int max_num_shots; /* Number of screen shots that can be
                                      saved before the suffix returns to 0 */
static unsigned int shot_num = 0; /* Numeric suffix for screenshot files */
static char *file_path = NULL; /* Base file path for screenshots */
static bool grab_enable = false; /* Is hotkey enabled? */
static bool save_palette = true; /* Should the palette be saved with each
                                    screenshot? */
static bool default_film = false; /* Lock filming when the hot key is pressed
                                     in isolation without control held? */
static bool new_sprite = false; /* Generate new (RISC OS 3.5) sprite type */
static bool ticker_running = false; /* Is a ticker event pending? */
static bool callback_pending = false; /* Is a transient callback pending? */
static bool filming = false; /* Keep adding transient callbacks to take
                                screenshots? */
static bool left_ctrl = false; /* Is the lefthand control key pressed? */
static bool right_ctrl = false; /* Is the righthand control key pressed? */
static bool film_lock = false; /* Should we stop filming next time that the
                                  hot key is released? */
static bool error_recorded = false; /* Has an error occurred within the
                                       transient callback or ticker event
                                       routine since the last *SGrabStatus? */
static bool save_next = true; /* Should we do a screenshot next time that we
                                 detect a different display bank? */
static bool mode_vars_valid = false; /* Are the contents of the mode_vars array
                                        valid for the current screen mode? */
static int interval = Interval_Default; /* Delay time between screenshots */
static int old_display_bank;
#ifdef SUPPORT_OS_310
static int os_version;
#endif
static _kernel_oserror last_error; /* Most recent OS error to occur within the
                                      transient callback or ticker event
                                      routine. */
static int mode_vars[VarIndex_LAST];

static const char *key_names[] = {
  "Escape",
  "F1",
  "F2",
  "F3",
  "F4",
  "F5",
  "F6",
  "F7",
  "F8",
  "F9",
  "F10",
  "F11",
  "F12",
  "Print",
  "ScrollLock",
  "Break",
  "`", /* was "~" before v2.15 */
  "1",
  "2",
  "3",
  "4",
  "5",
  "6",
  "7",
  "8",
  "9",
  "0",
  "-",
  "=",
  "£",
  "Backspace",
  "Insert",
  "Home",
  "PageUp",
  "NumLock",
  "Keypad/",
  "Keypad*",
  "Keypad#",
  "Tab",
  "Q",
  "W",
  "E",
  "R",
  "T",
  "Y",
  "U",
  "I",
  "O",
  "P",
  "[",
  "]",
  "#",
  "Delete",
  "End",
  "PageDown",
  "Keypad7",
  "Keypad8",
  "Keypad9",
  "Keypad-",
  "LCtrl",
  "A",
  "S",
  "D",
  "F",
  "G",
  "H",
  "J",
  "K",
  "L",
  ";",
  "'",
  "Return",
  "Keypad4",
  "Keypad5",
  "Keypad6",
  "Keypad+",
  "LShift",
  "\\",
  "Z",
  "X",
  "C",
  "V",
  "B",
  "N",
  "M",
  ",",
  ".",
  "/",
  "RShift",
  "Up",
  "Keypad1",
  "Keypad2",
  "Keypad3",
  "CapsLock",
  "LAlt",
  "Space",
  "RAlt",
  "RCtrl",
  "Left",
  "Down",
  "Right",
  "Keypad0",
  "Keypad.",
  "Enter"
};

static const struct
{
  char ModeNumber;
  char XEigFactor; // Horizontal dots-per-inch = 180 >> XEigFactor
  char YEigFactor; // Vertical dots-per-inch = 180 >> YEigFactor
  char Log2BPP;    // Bits-per-pixel = 1 << Log2BPP
}
known_modes[] =
{
//  {4,  2, 2, 0}, /* 45x45 dpi, 1 bpp (2 colours) */
// There appear to be bugs in Paint and/or the OS that prevent mode 4 sprites
// being handled properly. (Paint reports them to be half their actual width
// in pixels and displays them horizontally compressed!)
  {1,  2, 2, 1}, /* 45x45 dpi, 2 bpp (4 colours) */
  {9,  2, 2, 2}, /* 45x45 dpi, 4 bpp (16 colours) */
  {13, 2, 2, 3}, /* 45x45 dpi, 8 bpp (256 colours) */
  {0,  1, 2, 0}, /* 90x45 dpi, 1 bpp (2 colours) */
  {8,  1, 2, 1}, /* 90x45 dpi, 2 bpp (4 colours) */
  {12, 1, 2, 2}, /* 90x45 dpi, 4 bpp (16 colours) */
  {15, 1, 2, 3}, /* 90x45 dpi, 8 bpp (256 colours) */
  {25, 1, 1, 0}, /* 90x90 dpi, 1 bpp (2 colours) */
  {26, 1, 1, 1}, /* 90x90 dpi, 2 bpp (4 colours) */
  {27, 1, 1, 2}, /* 90x90 dpi, 4 bpp (16 colours) */
  {28, 1, 1, 3} /* 90x90 dpi, 8 bpp (256 colours) */
};

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *no_mem_error(void)
{
  return messagetrans_error_lookup(NULL, ErrorNum_NoMem, "NoMem", 0);
}

/* ----------------------------------------------------------------------- */

static void record_error(const _kernel_oserror *e)
{
  if (e != NULL)
  {
    memcpy(&last_error, e, sizeof(last_error));
    error_recorded = true;
  }
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *check_disp_bank(void *pw)
{
  const _kernel_oserror *e = NULL;
  bool do_screenshot = false;

  assert(pw != NULL);

  /* Are we waiting on screen bank swap? (e.g. for double buffering) */
  if (interval == Interval_AutoSync || interval == Interval_HalfSync)
  {
    /* Get the number of the currently displayed screen bank */
    int display_bank = _kernel_osbyte(OSByte_ReadDisplayBank, 0, UCHAR_MAX);
    if (display_bank == _kernel_ERROR)
    {
      e = _kernel_last_oserror();
    }
    else
    {
      /* Return value of R1 is in the least significant byte */
      display_bank = display_bank & OSByteR1ResultMask;

      /* Bank number 0 isn't real - it means 'default for this screen mode' */
      if (display_bank == 0)
      {
        int vdu_status = _kernel_osbyte(OSByte_ReadVduStatus, 0, 0);
        if (vdu_status == _kernel_ERROR)
        {
          e = _kernel_last_oserror();
        }
        else
        {
           /* The second screen bank is selected by default in a shadow mode */
          if ((vdu_status & VduStatus_ShadowMode) != 0)
            display_bank = 2;
          else
            display_bank = 1;
        }
      }

      /* Has the displayed screen bank changed since the last ticker event? */
      if (e == NULL && display_bank != old_display_bank)
      {
        old_display_bank = display_bank;

        /* Are we configured to ignore every other display bank change? */
        if (interval == Interval_HalfSync)
        {
          /* Was the previous display bank change ignored? */
          if (save_next)
            do_screenshot = true;

          save_next = !save_next;
        }
        else
        {
          do_screenshot = true;
        }
      }
    }
  }
  else
  {
    do_screenshot = true;
  }

  if (do_screenshot)
  {
    if (callback_pending)
    {
      DEBUGF("Previous callback hasn't finished yet!\n");
    }
    else
    {
      DEBUGF("Adding callback to routine %p\n", callback_veneer);
      callback_pending = true;
      e = _swix(OS_AddCallBack, _INR(0,1), callback_veneer, pw);
      if (e != NULL)
        callback_pending = false;
    }
  }

  return e;
}

/* ----------------------------------------------------------------------- */

_kernel_oserror *ticker_handler(_kernel_swi_regs *r, void *pw)
{
  NOT_USED(r);
  assert(pw != NULL);

  /* Ticker event routines can't return errors */
  record_error(check_disp_bank(pw));

  return NULL;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *read_mode_vars(void)
{
  /* Must keep the following array synchronised with the enumeration of mode
     variable value indicies */
  static const VDUVar variable_nos[VarIndex_LAST + 1] =
  {
    (VDUVar)ModeVar_ModeFlags,
    (VDUVar)ModeVar_NColour,
    (VDUVar)ModeVar_XEigFactor,
    (VDUVar)ModeVar_YEigFactor,
    (VDUVar)ModeVar_LineLength,
    (VDUVar)ModeVar_ScreenSize,
    (VDUVar)ModeVar_Log2BPP,
    (VDUVar)ModeVar_XWindLimit,
    (VDUVar)ModeVar_YWindLimit,
    VDUVar_EndOfList
  };
  const _kernel_oserror *e;

  /* Read information about the current screen mode */
  e = os_read_vdu_variables(variable_nos, mode_vars);
  mode_vars_valid = (e == NULL);

  return e;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *save_screen(const char *save_file_path)
{
  unsigned int screen_mode; /* this MUST be unsigned because it might be a
                               top-bit-set address */
  PaletteEntry *palette = NULL;
  unsigned int i, palette_size;
#ifdef USE_STDIO
  FILE *out = NULL;
#else
  _kernel_osgbpb_block inout;
  int out = 0;
#endif
  const _kernel_oserror *e = NULL;
  SpriteAreaHeader area_header;
  SpriteHeader sprite_header;

  assert(save_file_path != NULL);

  /* We require valid information about the current screen mode. We can't read
     mode variable values here because VDU driver output may have been
     redirected to a sprite. */
  if (!mode_vars_valid)
    goto error;

  /* Check that the current screen mode is suitable for saving the
     display as a sprite. */
  if (mode_vars[VarIndex_ModeFlags] & (ModeFlag_NonGraphics |
                                       ModeFlag_Teletext |
                                       ModeFlag_LineGap |
                                       ModeFlag_BBCLineGap) != 0)
  {
    DEBUGF("Screen mode is unsuitable (flags = 0x%x)\n",
           mode_vars[VarIndex_ModeFlags]);
  }
  else if (mode_vars[VarIndex_Log2BPP] + 1 < SPRITE_TYPE_1BPP &&
           mode_vars[VarIndex_Log2BPP] + 1 > SPRITE_TYPE_32BPP)
  {
    DEBUGF("Screen mode is unsuitable (bpp = %u)\n",
           1 << mode_vars[VarIndex_Log2BPP]);
  }
  else
  {
    static const VDUVar disp_var_num[] =
    {
      VDUVar_DisplayStart, /* Not affected by sprite output redirection */
      VDUVar_EndOfList
    };
    int disp_var_val[ARRAY_SIZE(disp_var_num) - 1];
    void *display_start;

    /* Get the address of the frame buffer currently being displayed */
    e = os_read_vdu_variables(disp_var_num, disp_var_val);
    if (e != NULL)
      goto error;

    display_start = (void *)disp_var_val[0];

    /* Read palette for current screen mode, if any */
    if (save_palette &&
        mode_vars[VarIndex_NColour] > 0 &&
        mode_vars[VarIndex_NColour] < MaxPaletteSize)
    {
      /* Calculate required buffer size. We need room for a pair of palette
         entries (first flash, second flash) for each logical colour. */
      palette_size = sizeof(palette[0]) * (mode_vars[VarIndex_NColour] + 1) * 2;
    }
    else
    {
      palette_size = 0;
    }

    if (palette_size > 0)
    {
      unsigned int col;

      /* Allocate memory for palette */
      DEBUGF("Allocating %u bytes for palette\n", palette_size);
      palette = malloc(palette_size);
      if (palette == NULL)
      {
        e = no_mem_error();
        goto error;
      }

      /* Read a pair of palette entries for each logical colour into our buffer,
         one at a time. This is likely to be relatively slow. We can't read the
         whole palette at once using ColourTrans_ReadPalette because it outputs
         a simpler format that doesn't cater for flashing colours. :o( */
      DEBUGF("Reading palette entries for %u colours\n",
             mode_vars[VarIndex_NColour] + 1);

      for (col = 0; col <= mode_vars[VarIndex_NColour]; col++)
      {
        /* You might expect this SWI to read a sprite's palette when output
           has been redirected to a sprite - luckily it doesn't! */
        e = _swix(OS_ReadPalette,
                  _INR(0,1)|_OUTR(2,3),
                  col,
                  16 /* means read 'normal' colour (not border) */,
                  palette + col * 2,
                  palette + col * 2 + 1);
        if (e != NULL)
          goto error;
      }
    }

    _kernel_last_oserror(); /* clear any OS error previously recorded */

    /* Open binary file for writing */
    DEBUGF("Opening output file '%s'\n", save_file_path);
#ifdef USE_STDIO
    out = fopen(save_file_path, "wb");
    if (out == NULL)
#else
    out = _kernel_osfind(0x80, (char *)save_file_path);
    if (out == _kernel_ERROR || out == 0)
#endif
    {
      e = _kernel_last_oserror();
      goto error;
    }

    /* Write sprite file header */
    area_header.sprite_count = 1;
    area_header.first = sizeof(area_header);
    area_header.used = sizeof(area_header) + sizeof(sprite_header) +
                       palette_size + mode_vars[VarIndex_ScreenSize];

    /* Write the sprite area header to the output file
       (not including the area size, which isn't required) */
    DEBUGF("Writing sprite file header (%u bytes)\n",
           sizeof(area_header) - sizeof(area_header.size));

    if (!FWRITE(&area_header.sprite_count,
                sizeof(area_header) - sizeof(area_header.size),
                out))
    {
      e = _kernel_last_oserror();
      goto error;
    }

    /* Write sprite header */
    sprite_header.size = sizeof(sprite_header) + palette_size +
                         mode_vars[VarIndex_ScreenSize];

    /* Sprite name only requires a NUL terminator if less than maximum length */
    strncpy(sprite_header.name,
            pathtail(save_file_path, 1),
            sizeof(sprite_header.name));

    /* Bizarrely, Paint rejects sprites named with upper-case characters! */
    for (i = 0;
         sprite_header.name[i] != '\0' && i < sizeof(sprite_header.name);
         i ++)
    {
      sprite_header.name[i] = tolower(sprite_header.name[i]);
    }

    sprite_header.height = mode_vars[VarIndex_YWindLimit];
    sprite_header.width = ((unsigned)mode_vars[VarIndex_LineLength] + 3) / 4 -
                          1;
    sprite_header.left_bit = 0; /* Left-hand wastage is deprecated */

    /* Last bit used (0-31) is the remainder from dividing one less than
      bits-per-line by bits-per-word (latter is fixed as 32). This determines
      the amount of right hand wastage for each row. */
    sprite_header.right_bit = SPRITE_RIGHT_BIT_LOG2(
                                (unsigned)mode_vars[VarIndex_XWindLimit] + 1,
                                mode_vars[VarIndex_Log2BPP]);
    sprite_header.image = sizeof(sprite_header) + palette_size;
    sprite_header.mask = sprite_header.image; /* Sprite will never have mask */

#ifdef SUPPORT_OS_310
    if (os_version < MinOSVersion)
    {
      /* Fall-back code for RISC OS 3.1 (doesn't support SWI OS_ScreenMode) */
      screen_mode = _kernel_osbyte(OSByte_ReadCharAndMode, 0, 0);
      if (screen_mode == _kernel_ERROR)
      {
        e = _kernel_last_oserror();
        goto error;
      }
      /* The mode number is returned in R2 */
      screen_mode = (screen_mode & OSByteR2ResultMask) >> OSByteR2ResultShift;
    }
    else
#endif
    {
      /* Read current screen mode on RISC OS 3.5 and above */
      e = _swix(OS_ScreenMode,
                _IN(0)|_OUT(1),
                OSScreenMode_ReadMode,
                &screen_mode);

      if (e != NULL)
        goto error;
    }
    DEBUGF("Screen mode is 0x%x\n", screen_mode);

    /* If the configured preference is for old-format sprites and OS_ScreenMode
       returned a pointer to a mode specifier block then try to find an
       equivalent mode number. */
    if (!new_sprite && screen_mode > LastNumberedMode)
    {
      /* Search a short list of known old-style screen modes for one that
         matches the important features of the current mode */
      for (i = 0; i < ARRAY_SIZE(known_modes); i++)
      {
        if (known_modes[i].XEigFactor == mode_vars[VarIndex_XEigFactor] &&
            known_modes[i].YEigFactor == mode_vars[VarIndex_YEigFactor] &&
            known_modes[i].Log2BPP == mode_vars[VarIndex_Log2BPP])
        {
          /* This old-style mode number is a good enough match to use */
          screen_mode = known_modes[i].ModeNumber;
          DEBUGF("Substituting mode number %d\n", screen_mode);
          break;
        }
      }
    }

    /* If we could not find a suitable old-style screen mode number then we
       must create a new-format sprite, whatever the configured preference. */
    if (screen_mode > LastNumberedMode)
    {
      unsigned int type;

      /* Synthesise a sprite type specifier (supported from RISC OS 3.5). */
      sprite_header.type =
        SPRITE_INFO_NOT_MODE_SEL |
        ((180 >> mode_vars[VarIndex_XEigFactor]) << SPRITE_INFO_HOZ_DPI_SHIFT) |
        ((180 >> mode_vars[VarIndex_YEigFactor]) << SPRITE_INFO_VER_DPI_SHIFT);

      /* Treat 16 bpp modes with 64 thousand colours as a special case */
      if (mode_vars[VarIndex_Log2BPP] == 4 &&
          (mode_vars[VarIndex_ModeFlags] & ModeFlag_ExtraColours) != 0)
      {
        type = 10; /* 16bpp RGB565 format */
      }
      else
      {
        /* sprite types: 1=1bpp, 2=2bpp, 3=4bpp, 4=8bpp, 5=16bpp, 6=24bpp */
        type = mode_vars[VarIndex_Log2BPP] + 1;
      }
      sprite_header.type |= type << SPRITE_INFO_TYPE_SHIFT;
    }
    else
    {
      /* Use mode number as sprite type */
      sprite_header.type = screen_mode;
    }
    DEBUGF("Sprite type is 0x%x\n", sprite_header.type);

    /* Write the sprite header to the output file */
    DEBUGF("Writing sprite header (%u bytes)\n", sizeof(sprite_header));
    if (!FWRITE(&sprite_header, sizeof(sprite_header), out))
    {
      e = _kernel_last_oserror();
      goto error;
    }

    /* Dump current screen palette to output file, if buffered */
    if (palette != NULL)
    {
      DEBUGF("Writing sprite palette (%u bytes)\n", palette_size);
      if (!FWRITE(palette, palette_size, out))
      {
        e =  _kernel_last_oserror();
        goto error;
      }
    }

    /* Dump contents of frame buffer to output file (same format as a sprite) */
    DEBUGF("Copying sprite bitmap from frame buffer %p (%u bytes)\n",
           display_start, mode_vars[VarIndex_ScreenSize]);

    if (!FWRITE(display_start, mode_vars[VarIndex_ScreenSize], out))
    {
      e = _kernel_last_oserror();
      goto error;
    }

    /* Close the output file */
    DEBUGF("Closing output file\n");
#ifdef USE_STDIO
    fclose(out);
    out = NULL;
#else
    _kernel_osfind(0, (char *)out);
    out = 0;
#endif

    /* Set type of output file */
    e = set_file_type(save_file_path, FileType_Sprite);
  }

error:
  free(palette);

#ifdef USE_STDIO
  if (out != NULL)
    fclose(out);
#else
  if (out != 0)
    _kernel_osfind(0, (char *)out);
#endif

  return e;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *take_numbered_shot(void)
{
  /* construct file path */
  const _kernel_oserror *e = NULL;
  char *full_path;

  full_path = malloc(strlen(file_path) + NumSuffixLen + 1);
  if (full_path == NULL)
  {
    e = no_mem_error();
  }
  else
  {
    sprintf(full_path, "%s%.*u", file_path, NumSuffixLen, shot_num);

    e = save_screen(full_path);
    if (e == NULL)
    {
      /* Advance screenshot counter */
      if (++shot_num >= max_num_shots)
        shot_num = 0; /* wrap */
    }
    free(full_path);
  }
  return e;
}

/* ----------------------------------------------------------------------- */

_kernel_oserror *callback_handler(_kernel_swi_regs *r, void *pw)
{
  NOT_USED(r);
  NOT_USED(pw);

  /* Transient callback routines can't return errors */
  record_error(take_numbered_shot());

  callback_pending = false;

  return NULL;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *remove_ticker(void *pw)
{
  const _kernel_oserror *e = NULL;

  assert(pw != NULL);

  /* Remove the ticker event routine that is used to schedule transient
     callbacks to save screen shots */
  if (ticker_running)
  {
    DEBUGF("Removing ticker event routine\n");
    ticker_running = false;
    e = _swix(OS_RemoveTickerEvent, _INR(0,1), &ticker_veneer, pw);
  }

  return e;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *enable_hotkey(void)
{
  const _kernel_oserror *e = NULL;

  if (!grab_enable)
  {
    DEBUGF("Enabling key transition events\n");
    if (_kernel_osbyte(OSByte_EnableEvent, KeyTrans, 0) == _kernel_ERROR)
    {
      e = _kernel_last_oserror();
    }
    else
    {
      grab_enable = true;
      left_ctrl = false;
      right_ctrl = false;
    }
  }
  return e;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *disable_hotkey(void)
{
  const _kernel_oserror *e = NULL;

  if (grab_enable)
  {
    DEBUGF("Disabling key transition events\n");
    if (_kernel_osbyte(OSByte_DisableEvent, KeyTrans, 0) == _kernel_ERROR)
      e = _kernel_last_oserror();
    else
      grab_enable = false;
  }
  return e;
}

/* ----------------------------------------------------------------------- */

#ifdef FORTIFY
static void fortify_output(const char *text)
{
  DEBUGF(text);
}
#endif

/* ----------------------------------------------------------------------- */

int event_handler(_kernel_swi_regs *r, void *pw)
{
  /* (No need to check event number, as CMHG veneer filters events for us) */
  const _kernel_oserror *e = NULL;
  int claim = EventHandler_PassOn;

  assert(r != NULL);
  assert(pw != NULL);

  switch (r->r[2])
  {
    case KeyCode_LeftCtrl:
      /* Change in state of left control key */
      left_ctrl = (r->r[1] != KeyTrans_Release);
      DEBUGF("Left Ctrl was %s\n",
             r->r[1] == KeyTrans_Release ? "released" : "pressed");
      break;

    case KeyCode_RightCtrl:
      /* Change in state of right control key */
      right_ctrl = (r->r[1] != KeyTrans_Release);
      DEBUGF("Right Ctrl was %s\n",
             r->r[1] == KeyTrans_Release ? "released" : "pressed");
      break;

    default:
      /* Change in state of some other key */
      if (r->r[2] == internal_key_no)
      {
        /* Change in state of the configured hot key */
        DEBUGF("Hot key was %s\n",
             r->r[1] == KeyTrans_Release ? "released" : "pressed");

        claim = EventHandler_Claim;

        if (r->r[1] == KeyTrans_Release)
        {
          /* Hot key released - stop filming unless locked */
          if (film_lock)
          {
            DEBUGF("Can't stop filming (locked)\n");
          }
          else
          {
            DEBUGF("Stop filming (not locked)\n");
            if (filming)
            {
              filming = false;

              /* Remove the ticker event routine that adds transient callbacks
                 to take additional screen shots */
              e = remove_ticker(pw);
            }
          }
        }
        else
        {
          /* Hot key pressed - start filming and lock or unlock */

          /* Start filming, if not already doing so */
          if (filming)
          {
            DEBUGF("Already filming\n");
          }
          else
          {
            DEBUGF("Starting filming\n");
            filming = true;

            /* Save a screenshot and record the initial display bank number */
            save_next = true;
            old_display_bank = -1;
            e = check_disp_bank(pw);

            if (e == NULL && !ticker_running)
            {
              /* Register a ticker event routine to save subsequent screenshots
                 if the hot key is held or we are locked into filming. */
              int freq;

              if (interval == Interval_AutoSync ||
                  interval == Interval_HalfSync)
                freq = AutoSyncInterval;
              else
                freq = interval;

              DEBUGF("Registering ticker event routine %p with frequency %d\n",
                     ticker_veneer, freq);

              ticker_running = true;
              e = _swix(OS_CallEvery, _INR(0,2), freq - 1, &ticker_veneer, pw);
              if (e != NULL)
                ticker_running = false;
            }
          }

          /* If the default action is hands-off filming then set a flag to
             allow it to continue when the hot key is released; otherwise,
             clear the flag. We must update this flag even if we are already
             filming to allow filming to be stopped by releasing the hot key a
             second time. */
          film_lock = default_film;

          /* If pressed, the Control key reverses the default action. This
             allows discrete screenshots in hands-off filming mode or hands-off
             filming in screenshot mode. */
          if (left_ctrl || right_ctrl)
            film_lock = !film_lock;

          DEBUGF("Releasing the hot key %s stop filming\n",
                 film_lock ? "won't" : "will");
        }
      }
      break;
  }

  record_error(e);  /* EventV claimants can't return errors */
  return claim;
}

/* ----------------------------------------------------------------------- */

_kernel_oserror *screengrabber_initialise(const char *cmd_tail, int podule_base, void *pw)
{
  const _kernel_oserror *e = NULL;
  unsigned int pow = 0, temp;

  NOT_USED(cmd_tail);
  NOT_USED(podule_base);
  assert(pw != NULL);

  DEBUG_SET_OUTPUT(DebugOutput_Reporter, MOD_NAME);

#ifdef FORTIFY
  Fortify_SetOutputFunc(fortify_output);
  Fortify_SetAllocateFailRate(5);
  Fortify_EnterScope();
#endif

  /* Raise 10 to the power of the number of decimal digits allocated for the
     screen shot file name's numeric suffix, to calculate the number of
     unique file names that can be generated. */
  temp = 1;
  for (pow = 1; pow <= NumSuffixLen; pow++)
    temp *= 10;

  DEBUGF("No. of unique file names is %u\n", temp);
  max_num_shots = temp;

#ifdef SUPPORT_OS_310
  /* Read OS version identifier */
  os_version = _kernel_osbyte(OSByte_ScanKeyboard,
                              OSByteScanKeysReadVsn,
                              OSByteScanKeysNoLimit);
  if (os_version == _kernel_ERROR)
  {
    e = _kernel_last_oserror();
    goto error;
  }
  os_version &= OSByteR1ResultMask;
#endif

  /* Set up the default base file name to use when saving screenshots */
  file_path = strdup("ScrGrab");
  if (file_path == NULL)
  {
    e = no_mem_error();
    goto error;
  }

  /* Read information about the current screen mode. Hopefully output isn't
     directed to a sprite whilst this module is (re)initialised! */
  e = read_mode_vars();
  if (e != NULL)
    goto error;

  /* Enable key transition events */
  e = enable_hotkey();
  if (e == NULL)
  {
    /* Claim the event vector to monitor key transitions */
    e = _swix(OS_Claim, _INR(0,2), EventV, &event_veneer, pw);
    if (e != NULL)
      (void)disable_hotkey();
  }

error:
  if (e != NULL)
    FREE_SAFE(file_path);

  return (_kernel_oserror *)e;
}

/* ----------------------------------------------------------------------- */

_kernel_oserror *screengrabber_finalise(int fatal, int podule, void *pw)
{
  const _kernel_oserror *e = NULL;
  static bool failed = false;

  NOT_USED(fatal);
  NOT_USED(podule);
  assert(pw != NULL);

  /* Allow a second attempt at finalisation to succeed if this one fails */
  if (!failed)
  {
    failed = true;

    /* Release the event vector (no longer interested in key transitions) */
    MERGE_ERR(e, _swix(OS_Release, _INR(0,2), EventV, &event_veneer, pw));

    /* Disable key transition events */
    MERGE_ERR(e, disable_hotkey());

    /* Remove the ticker event routine that adds transient callbacks to take
       additional screen shots */
    MERGE_ERR(e, remove_ticker(pw));

    /* Remove any pending transient callback */
    if (callback_pending)
    {
      DEBUGF("Removing transient callback\n");
      callback_pending = false;
      MERGE_ERR(e, _swix(OS_RemoveCallBack, _INR(0,1), callback_veneer, pw));
    }

    FREE_SAFE(file_path);
#ifdef FORTIFY
    Fortify_LeaveScope();
#endif
  }

  return (_kernel_oserror *)e;
}

/* ----------------------------------------------------------------------- */

static int key_name_to_num(const char *key_name)
{
  /* Returns the internal key number matching the specified key name */
  int i, key_code = -1;

  assert(key_name != NULL);
  if (stricmp("~", key_name) == 0)
  {
    key_code = 16; /* This key was named ~ rather than ' until v2.15 */
  }
  else
  {
    for (i = 0; i < ARRAY_SIZE(key_names); i++)
    {
      if (stricmp(key_names[i], key_name) == 0)
      {
        key_code = i;
        break;
      }
    }
  }

  DEBUGF("Key named %s is internal code %d\n", key_name, key_code);
  return key_code;
}

/* ----------------------------------------------------------------------- */

static int read_evaluated(const uint8_t *eval)
{
  /* OS_ReadArgs stores the results of evaluated expressions in a
     non-aligned format, which this function decodes. */
  int value;

  assert(eval != NULL);
  if (*eval == 0) /* type should always be integer */
  {
    /* Little-endian format */
    value = eval[1] | eval[2] << 8 | eval[3] << 16 | eval[4] << 24;
  }
  else
  {
    value = 0;
  }
  DEBUGF("Decoded evaluated argument at %p as %d\n", eval, value);
  return value;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *evaluate_int( const char *string, int *type, int *value )
{
  return _swix( OS_EvaluateExpression,
                _INR(0,2)|_OUTR(1,2),
                string,
                NULL, /* No output buffer */
                0, /* output buffer length */
                type,
                value );
}

/* ----------------------------------------------------------------------- */

static void show_state_savepalette(void)
{
  fputs("Save palette:        ", stdout);
  puts(save_palette ? "Yes" : "No");
}

/* ----------------------------------------------------------------------- */

static void show_state_sprite_type(void)
{
  fputs("Sprite type:         ", stdout);
  puts(new_sprite ? "New" : "Old");
}

/* ----------------------------------------------------------------------- */

static void show_background(void)
{
  fputs("Background activity: ", stdout);
  puts(filming ? "Recording" : "Idle");

  fputs("Background error:    ", stdout);
  if (error_recorded)
  {
    error_recorded = false;
    printf("&%X ('%s')\n", last_error.errnum, last_error.errmess);
  }
  else
  {
    puts("None");
  }
}

/* ----------------------------------------------------------------------- */

static void show_state_enabled(void)
{
  printf("Key shortcut:        Code %u", internal_key_no);
  if (internal_key_no < ARRAY_SIZE(key_names))
    printf(" ('%s')", key_names[internal_key_no]);

  puts(grab_enable ? ", enabled" : ", disabled");
}

/* ----------------------------------------------------------------------- */

static void show_state_counter(void)
{
  printf("Next shot number:    %u\n", shot_num);
}

/* ----------------------------------------------------------------------- */

static void show_state_file_path(void)
{
  printf("Base file name:      '%s'\n", file_path);
}

/* ----------------------------------------------------------------------- */

static void show_state_film(void)
{
  fputs("Default key action:  ", stdout);
  puts(default_film ? "Record until cancelled" : "Record until key release");
}

/* ----------------------------------------------------------------------- */

static void show_state_filmdelay(void)
{
  fputs("Frame interval:      ", stdout);
  switch (interval)
  {
    case Interval_AutoSync:
      puts("Automatic (every screen bank swap)");
      break;
    case Interval_HalfSync:
      puts("Automatic (alternate bank swaps)");
      break;
    default:
      printf("%d centiseconds\n", interval);
      break;
  }
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *switch_on_or_off( void *value, int index )
{
  const _kernel_oserror *e = NULL; /* success */
  const char * const str = value;

  NOT_USED( value );

  switch ( index )
  {
    case Arg_On:
      e = enable_hotkey();
      break;

    case Arg_Off:
      e = disable_hotkey();
      break;

    default:
      /* If called with Arg_END then this is *SGrabHotKey not *SGrabConfigure */
      assert( index == Arg_END );
      if ( *str == '!' )
      {
        /* Attempt to extract raw key number from command argument */
        int type, key_num;

        e = evaluate_int( str + 1, &type, &key_num );
        if ( e == NULL )
        {
          if ( type == 0 )
            internal_key_no = key_num;
          else
            e = &error_hotkey_syntax;
        }
      }
      else
      {
        const int key_num = key_name_to_num( str );
        if ( key_num < 0 )
          e = &error_uk_key_name;
        else
          internal_key_no = key_num;
      }
      break;
  }

  return e;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *switch_single_or_film( void *value, int index )
{
  const _kernel_oserror *e = NULL; /* success */

  NOT_USED( value );

  switch ( index )
  {
    case Arg_Single:
      default_film = false;
      break;

    case Arg_Film:
      default_film = true;
      break;

    default:
      /* If called with Arg_END then this is *SGrabFilm not *SGrabConfigure */
      assert( index == Arg_END );
      e = &error_film_syntax;
      break;
  }

  return e;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *switch_key_name_or_code( void *value, int index )
{
  const _kernel_oserror *e = NULL; /* success */

  assert( value != NULL );
  if ( index == Arg_KeyName )
  {
    const int key_num = key_name_to_num( value );
    if ( key_num < 0 )
    {
      e = &error_uk_key_name;
    }
    else
    {
      internal_key_no = key_num;
    }
  }
  else
  {
    /* Only called for *SGrabConfigure, so the expression has already been evaluated */
    assert( index == Arg_KeyCode );
    internal_key_no = read_evaluated( value );
  }

  return e;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *switch_interval( void *value, int index )
{
  const _kernel_oserror *e = NULL; /* success */
  int type, new_delay;

  assert( value != NULL );
  switch ( index )
  {
    case Arg_AutoSync:
      interval = Interval_AutoSync;
      break;

    case Arg_HalfSync:
      interval = Interval_HalfSync;
      break;

    default:
      /* If called with Arg_END then this is *SGrabFilmDelay not *SGrabConfigure */
      assert( index == Arg_Interval || index == Arg_END );

      /* Attempt to extract interval from command argument */
      e = evaluate_int( value, &type, &new_delay );
      if ( e == NULL )
      {
        if ( type == 0 )
        {
          /* Ensure a minimum sensible interval */
          if ( new_delay < Interval_Minimum )
            e = &error_bad_interval; /* fail */
          else
            interval = new_delay;
        }
        else
        {
          e = &error_filmdelay_syntax;
        }
      }
      break;
  }

  return e;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *switch_palette( void *value, int index )
{
  const _kernel_oserror *e = NULL; /* success */

  NOT_USED( value );

  switch ( index )
  {
    case Arg_Palette:
      save_palette = true;
      break;

    case Arg_NoPalette:
      save_palette = false;
      break;

    default:
      /* If called with Arg_END then this is *SGrabPalette not *SGrabConfigure */
      assert( index == Arg_END );
      e = &error_palette_syntax;
      break;
  }

  return e;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *switch_new_sprite( void *value, int index )
{
  NOT_USED( value );

  /* Only called for *SGrabConfigure */
  if ( index == Arg_NewSprite )
  {
    new_sprite = true;
  }
  else
  {
    assert( index == Arg_OldSprite );
    new_sprite = false;
  }

  return NULL; /* success */
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *switch_filename( void *value, int index )
{
  const _kernel_oserror *e = NULL; /* success */

  assert( value != NULL );
  /* If called with Arg_END then this is *SGrabFilename not *SGrabConfigure */
  assert( index == Arg_Filename || index == Arg_END );
  NOT_USED( index );

  assert(new_name != NULL);
  if ( strcmp( file_path, value ) != 0 )
  {
    char *dup = strdup( value );
    if ( dup == NULL )
    {
      e = no_mem_error();
    }
    else
    {
      DEBUGF( "Replacing filename %s with %s\n", file_path, dup );
      free( file_path );
      file_path = dup; /* Change base file path */
      shot_num = 0;    /* and reset counter */
    }
  }
  else
  {
    DEBUGF( "No change to filename %s\n", file_path );
  }

  return e;
}

/* ----------------------------------------------------------------------- */

_kernel_oserror *cmd_handler(const char *arg_string, int argc, int cmd_no, void *pw)
{
  const _kernel_oserror *e = NULL;
  void *read_args_buf[80];

  assert(arg_string != NULL || argc == 0);
  NOT_USED(pw);
  DEBUGF("No. of arguments:%d\nCommand number: %d\n", argc, cmd_no);

  /* If any arguments were passed in the command tail then decode them */
  if (argc > 0)
  {
    const char *syntax;

    if (cmd_no == CMD_SGrabConfigure)
      syntax = "On/s,Off/s,Single/s,Film/s,KeyName=KN/k,KeyCode=KC/k/e,"
               "Interval/k,AutoSync=AS/s,HalfSync=HS/s,"
               "NoPalette=NP/s,Palette/s,NewSprite=NS/s,OldSprite=OS/s,"
               "Filename/k";
    else
      syntax = "/a";

    e = _swix(OS_ReadArgs,
              _INR(0,3),
              syntax,
              arg_string,
              read_args_buf,
              sizeof(read_args_buf));
  }
  else
  {
    read_args_buf[0] = NULL;
  }

  if (e == NULL)
  {
    static const ArgSwitch switches[] =
    {
      {
        show_state_enabled,
        switch_on_or_off,
        CMD_SGrabHotKey,
        { { Arg_On, "on" }, { Arg_Off, "off" }, { Arg_END } }
      },
      {
        NULL, /* Key name/number already shown by show_state_enabled */
        switch_key_name_or_code, /* Only handles *SGrabConfigure */
        -1, /* Key name/number are configured by *SGrabHotKey */
        { { Arg_KeyName }, { Arg_KeyCode }, { Arg_END } }
      },
      {
        show_state_savepalette,
        switch_palette,
        CMD_SGrabPalette,
        { { Arg_Palette, "on" }, { Arg_NoPalette, "off" }, { Arg_END } }
      },
      {
        show_state_counter, /* Only present for *SGrabStatus */
        NULL,
        -1, /* Must handle *SGrabResetCount separately because of unusual behaviour */
        { { Arg_END } }
      },
      {
        show_state_file_path,
        switch_filename,
        CMD_SGrabFilename,
        { { Arg_Filename }, { Arg_END } }
      },
      {
        show_state_film,
        switch_single_or_film,
        CMD_SGrabFilm,
        { { Arg_Film, "on" }, { Arg_Single, "off" }, { Arg_END } }
      },
      {
        show_state_filmdelay,
        switch_interval,
        CMD_SGrabFilmDelay,
        { { Arg_Interval }, { Arg_AutoSync, "auto" }, { Arg_HalfSync, "half" }, { Arg_END } }
      },
      {
        show_state_sprite_type,
        switch_new_sprite, /* Only handles *SGrabConfigure */
        -1, /* No command to configure sprite format except *SGrabConfigure */
        { { Arg_NewSprite }, { Arg_OldSprite }, { Arg_END } }
      }
    };
    unsigned int i;

    switch (cmd_no)
    {
      case CMD_SGrab:/* SGrab [<file path>] */
        if (read_args_buf[0] == NULL)
          e = take_numbered_shot();
        else
          e = save_screen(read_args_buf[0]);
        break;

      case CMD_SGrabResetCount:
        shot_num = 0;
        break;

      case CMD_SGrabStatus:
        puts(MOD_NAME" status:\n---------------------");
        for ( i = 0; e == NULL && i < ARRAY_SIZE(switches); i++ )
        {
          if ( switches[i].show != NULL )
            switches[i].show();
        }
        show_background();
        break;

      case CMD_SGrabConfigure:
        /* SGrabConfigure [-On|-Off] [-Single|-Film]
           [-KeyName <key name>|-KeyCode <key code>]
           [-Interval <centiseconds>|-AutoSync|-HalfSync]
           [-[No]Palette] [-NewSprite|-OldSprite] [-Filename <filename>]" */

        /* Two passes are required to implement the strong exception guarantee */
        for ( i = 0; e == NULL && i < 2; i++ )
        {
          unsigned int j;

          /* Examine each group of mutually-exclusive arguments in turn */
          for ( j = 0; e == NULL && j < ARRAY_SIZE(switches); j++ )
          {
            unsigned int k;
            const ArgSwitch * const sw = switches + j;
            bool found = false;

            /* Examine each argument in the mutually-exclusive group */
            for ( k = 0; e == NULL && sw->states[k].index != Arg_END; k++ )
            {
              const int index = sw->states[k].index;
              if ( read_args_buf[index] == NULL )
                continue; /* argument wasn't provided */

              if ( i == 0 )
              {
                /* First pass: check no arguments already found in this group */
                if ( found )
                  e = &error_configure_syntax;
                else
                  found = true;
              }
              else
              {
                /* Second pass: handle the argument */
                if ( sw->handler != NULL )
                  e = sw->handler( read_args_buf[index], index );

                break; /* there should be no more arguments in this group */
              }
            }
          }
        }
        break;

      default:
        /* Check for other commands */
        for ( i = 0; e == NULL && i < ARRAY_SIZE(switches); i++ )
        {
          const ArgSwitch * const sw = switches + i;

          if ( sw->cmd_no != cmd_no )
            continue;

          /* Identified the command */
          if ( read_args_buf[0] == NULL )
          {
            /* Command has no parameter, so show the currently configured state */
            if ( sw->show != NULL )
              sw->show();
          }
          else if ( sw->handler != NULL )
          {
            unsigned int j;
            bool found = false;

            /* Try to match the command parameter with the name of a state */
            for ( j = 0; !found && e == NULL && sw->states[j].index != Arg_END; j++ )
            {
              int index;

              if ( sw->states[j].string == NULL ||
                   stricmp( read_args_buf[0], sw->states[j].string ) != 0 )
                continue;

              /* Translate state name into equivalent SGrabConfigure argument index */
              index = sw->states[j].index;
              e = sw->handler( read_args_buf[0], index );
              found = true;
            }

            if ( !found )
            {
              /* Failed to match the command parameter, so tell the handler to parse it */
              e = sw->handler( read_args_buf[0], Arg_END );
            }
          }
          break; /* Stop trying to identify the command */
        }
        break;
    }
  }
  return (_kernel_oserror *)e;
}

/* ----------------------------------------------------------------------- */

void svc_handler(int service_number, _kernel_swi_regs *r, void *pw)
{
  NOT_USED(r);
  NOT_USED(pw);

  /* This service is issued when a screen mode change has taken place, so
     we need to update our cached mode information */
  if (service_number == Service_ModeChange)
  {
    /* Service call handlers can't return errors */
    record_error(read_mode_vars());
  }
}
