/*
 * ScreenGrabber (back-end module error blocks)
 * Copyright (C) 2000 Chris Bazley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public Licence as published by
 * the Free Software Foundation; either version 2 of the Licence, or
 * (at your option) any later version.
 */

#include "kernel.h"

const _kernel_oserror error_bad_interval =
  { 0x81a721, "The interval between frames must be at least 2 centiseconds" };

const _kernel_oserror error_uk_key_name =
  { 0x81a722, "Unknown key name" };

const _kernel_oserror error_hotkey_syntax =
  { 0xdc, "Syntax: *SGrabHotKey [On|Off|<key name>|!<key code>]" };

const _kernel_oserror error_palette_syntax =
  { 0xdc, "Syntax: *SGrabPalette [On|Off]" };

const _kernel_oserror error_film_syntax =
  { 0xdc, "Syntax: *SGrabFilm [On|Off]" };

const _kernel_oserror error_filmdelay_syntax =
  { 0xdc, "Syntax: *SGrabFilmDelay [<centiseconds>|Auto|Half]" };

const _kernel_oserror error_configure_syntax =
  { 0xdc, "Syntax: *SGrabConfigure [-On|-Off] [-Single|-Film] "
          "[-KeyName <key name>|-KeyCode <key code>] "
          "[-Interval <centiseconds>|-AutoSync|-HalfSync] "
          "[-[No]Palette] [-NewSprite|-OldSprite] [-Filename <filename>]" };
