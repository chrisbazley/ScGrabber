# ScGrabber

ScreenGrabber is a RISC OS relocatable module that saves pictures of the
screen as numbered Sprite files. It can be triggered by a hot key or the
`*SGrab` command. A desktop application, `!ScGrabber`, configures the module.
The current palette can be included in each screenshot.

The module was designed primarily for single-tasking games and graphical
demos. It monitors low-level key transitions, so pressing its hot key may
also affect a desktop application that uses the same key.

## Requirements

- RISC OS 3.10 or later for the module.
- The Toolbox, Window, ProgInfo, Menu, Iconbar and SaveAs modules for the
  desktop application, either in ROM or installed in `!System`.
- `Choices$Write` set to save choices from the desktop application.

## Quick start

1. Double-click `!ScGrabber`. Its icon appears on the icon bar and the
   ScreenGrabber module is loaded.
2. Click Select on the icon to open Choices.
3. Drag the Sprite icon in **Saving files** to the directory where you want
   screenshots to go. The filename field is updated with the full path.
4. Click **Set**.
5. Press **Print** (also labelled Prt Sc or Print Scrn). The first files
   will be named `SGrab00000`, `SGrab00001`, and so on.

## Recording with the hot key

Press the configured hot key (Print by default) to save a screenshot. Hold
it down to record a sequence, and release it to stop. Hold Ctrl while
pressing the hot key to keep recording after release; press and release the
hot key without Ctrl to stop. The **Record until stopped** option reverses
this Ctrl behaviour.

The default fixed interval is 10 centiseconds. Longer intervals reduce the
load on the computer. Continuous recording can consume considerable storage
and slow the machine down.

## Choices

Changes take effect when you click **Set** or press Return. **Cancel**
discards them. **Save** applies them and retains them for the next time the
front end starts.

- **Hot key:** Enable or disable it, select a key name, or enter its internal
  key code. The supplied name-to-code mapping is for a UK keyboard. On a
  different keyboard, the name controls are faded unless a matching mapping
  file has been added under `!ScGrabber.Keyboards` (named after the country
  number, such as `6` for France).
- **Record until stopped:** Reverse the normal effect of Ctrl when starting
  or stopping a recording.
- **Saving files:** Enter a full base path or drag the Sprite icon to a
  directory display. ScreenGrabber appends a five-digit, zero-padded number.
  Changing the base path resets the counter. It does not create missing
  directories. On filing systems with a ten-character leafname limit, keep
  the base leafname to five characters or fewer.
- **Save palette:** Include palette data in each Sprite file. This can add
  up to 2 KB, but preserves colours when the default palette is not in use.
- **New sprite type:** Prefer a RISC OS 3.5 style type specifier to an old
  mode number. The default is off for backward compatibility; a type
  specifier is still used when no suitable mode number exists.
- **Recording rate:** Choose a fixed interval of at least two centiseconds,
  **Auto synchronise** to save when a different display bank appears, or
  **Half speed** to save on alternate bank changes. The first screenshot is
  saved immediately when the hot key is pressed.

The icon-bar menu also provides **Reset counter**, which makes subsequent
screenshots overwrite existing files with the same numbered names, and
**Save script**, which writes the current choices as an Obey file of star
commands. The default script path is
`<Choices$Write>.Boot.Tasks.SGrabSetup`; `ScGrabber$Dir` must be set before
that script runs so the application can be found.

## Star commands

The module's commands can be used without the desktop front end. The front
end does not track changes made through commands, so mixing the two ways of
configuring it can leave the Choices display out of sync.

| Command | Purpose |
| --- | --- |
| `*SGrab [<file path>]` | Save the current screen as a Sprite file; use the configured path if omitted. |
| `*SGrabHotKey [On\|Off\|<key name>\|!<key number>]` | Enable, disable, or change the hot key; show its setting if omitted. Prefix a numeric key code with `!`. |
| `*SGrabPalette [On\|Off]` | Control whether the current palette is saved; show its setting if omitted. |
| `*SGrabResetCount` | Reset the screenshot filename counter. |
| `*SGrabFilename [<filename>]` | Set or show the base path; changing it resets the counter. |
| `*SGrabFilm [On\|Off]` | Set or show whether recording continues after the hot key is released by default. |
| `*SGrabFilmDelay [<centiseconds>\|Auto\|Half]` | Set or show the recording rate. `Auto` follows display-bank changes; `Half` records alternate changes. |
| `*SGrabStatus` | Show the module status and any background OS error since the last invocation. |

`*SGrabConfigure` changes several settings at once, leaving unspecified
settings untouched:

```text
*SGrabConfigure [-On|-Off] [-Single|-Film]
                [-KeyName <key name>|-KeyCode <key code>]
                [-Interval <centiseconds>|-AutoSync|-HalfSync]
                [-[No]Palette] [-NewSprite|-OldSprite] [-Filename <filename>]
```

The full UK key name and internal-code mapping is in `KeyNames.c` and the
application's original Help file.

## Limitations

- Palette capture may be wrong for games that program the palette directly
  instead of through RISC OS.
- The five-digit suffix wraps after 100,000 screenshots, so subsequent files
  with the same base name can overwrite earlier ones.
- Very short intervals may exceed the machine's ability to write files.
  Screenshots are saved in transient callbacks; if one is still in progress,
  the module does not queue another. Auto synchronisation can therefore miss
  some display-bank changes when callbacks are delayed.
- `*SGrabHotKey` recognises key names for a standard UK keyboard.

## CMake build

Configure with `cmake -S . -B build` and compile with
`cmake --build build`. The `ScGrabberApp` target compiles the desktop
front end; `ScGrabberModule` compiles the back end. On host platforms
both are compile-only targets because their RISC OS entry points and
libraries cannot be linked there. On RISC OS the front end is an executable.

The native `MakeBE` remains necessary to generate the CMHG veneer and link
the relocatable module. `Makefile` remains the native front-end build.

ScreenGrabber is free software under the GNU General Public Licence,
version 2 or later. See [LICENSE](LICENSE).
