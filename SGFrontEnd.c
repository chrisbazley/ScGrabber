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

/* 26.08.04 CJB Changed a few instances of old-style CBlibrary macro names that
                had been missed.
   07.06.06 CJB Updated to use CBlibrary function DeIconise_show_object()
                instead of a local implementation.
                Eliminated opaque hard-wired values in configure_module() by
                defining macros for the module's commands.
                Program no longer terminates on receipt of a PreQuit message.
                Added support for saving user choices as an Obey file of
                configuration commands - see save_to_file_event().
   22.06.06 CJB Added support for a 'Save' button in the Choices dialogue box
                (functions save_config, load_config and interpret_line).
                Made most of the functions in this source file 'static'.
   23.06.06 CJB Fixed bugs where opensetup_event() and actionbutton_event()
                called shade_setup_window() only if write_setup_window() failed!
*/

/* ANSI headers */
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

/* RISC OS headers */
#include "wimp.h"
#include "toolbox.h"
#include "event.h"
#include "wimplib.h"
#include "window.h"
#include "gadgets.h"
#include "saveas.h"

/* CBLibrary headers */
#include "err.h"
#include "msgtrans.h"
#include "MessTrans.h"
#include "hourglass.h"
#include "Macros.h"
#include "StrExtra.h"
#include "FileUtils.h"
#include "Debug.h"

/* Local headers */
#include "FEutils.h"
#include "SGFrontEnd.h"
#include "ConfigFile.h"
#include "MakeConfig.h"
#include "SetupDbox.h"
#include "ScrGrabberHdr.h"

#ifdef FORTIFY
#include "FORTIFY:FORTIFY.h"
#endif /* FORTIFY */

#define APP_NAME "ScGrabber"

/* Toolbox events */
enum
{
  ToolboxEvent_Quit       = 1,
  ToolboxEvent_Choices    = 2,
  ToolboxEvent_Help       = 5,
  ToolboxEvent_ResetCount = 6
};

/* Window component IDs */
enum
{
  ComponentId_KeyEnabled    = 0x03,
  ComponentId_FilePath      = 0x05,
  ComponentId_IntervalValue = 0x0c,
  ComponentId_IntervalLabel = 0x0d,
  ComponentId_AutoSync      = 0x0e,
  ComponentId_HalfSync      = 0x0f,
  ComponentId_SetButton     = 0x10,
  ComponentId_UseInterval   = 0x12,
  ComponentId_CancelButton  = 0x14,
  ComponentId_SavePalette   = 0x15,
  ComponentId_SaveButton    = 0x16,
  ComponentId_KeyValue      = 0x18,
  ComponentId_OneTouchFilm  = 0x1a,
  ComponentId_NewSpriteType = 0x1c,
  ComponentId_KeyName       = 0x1d
};

/* Constant numeric values */
enum
{
  KnownWimpVersion       = 321, /* Latest version of window manager known */
  MaxTaskNameLen         = 31,
  MinExtErrorWimpVersion = 321, /* Earliest version of window manager to
                                   support Wimp_ReportError extensions */
  OSModule_Delete        = 4
};

typedef struct
{
  int task_handle;
  char *task_name;
  int slot_size;
  int flags;
}
TaskInfo; /* for use with TaskManager_EnumerateTasks */

static ObjectId save_id; /* IDs of interesting Toolbox objects */

/* --------------- Setup window and interface to module ----------------- */

int wimp_version;
char task_name[MaxTaskNameLen + 1];

/* Current configuration (hopefully the same as the module but not guaranteed)
 */
char *save_path = NULL; /* Base filename for screenshots */
bool grab_enable = true; /* Is hotkey enabled? */
bool force_film = false; /* If true then a single press starts filming */
bool save_palette = true; /* Should the palette be saved with screenshots? */
bool new_sprite = false; /* Generate new (RISC OS 3.5) sprite type */
RepeatType repeat_type = RepeatType_Interval;
unsigned int interval = 10; /* Interval (in cs) between screenshots */
unsigned int key_code = 13; /* Internal key number */

/* ----------------------------------------------------------------------- */

#ifdef FORTIFY
static void fortify_output(const char *text)
{
  DEBUGF(text);
}
#endif /* FORTIFY */

/* ----------------------------------------------------------------------- */

/* Check if two tasks of given name are running */
static bool may_kill_shared_module(const char *find_name)
{
  _kernel_swi_regs regs;
  TaskInfo buffer;
  int num_found = 0;
  _kernel_oserror *e = NULL;

  assert(find_name != NULL);

  regs.r[0] = 0;
  do
  {
    regs.r[1] = (int)&buffer;
    regs.r[2] = sizeof(buffer);
    e = _kernel_swi(TaskManager_EnumerateTasks, &regs, &regs);
    if (e == NULL)
    {
      if (regs.r[0] >= 0 && string_equals(buffer.task_name, find_name))
        num_found++;
    }
    else
    {
      num_found = INT_MAX;  /* don't kill module */
    }
  }
  while (regs.r[0] >= 0 && num_found < 2);

  ON_ERR_RPT(e);
  return num_found < 2; /* may only kill module if no other instances of the
                          front-end task are running */
}

/* ----------------------------------------------------------------------- */

static void simple_exit(const _kernel_oserror *e)
{
  /* Limited amount we can do with no messages file... */
  assert(e != NULL);
  wimp_report_error((_kernel_oserror *)e, Wimp_ReportError_Cancel, APP_NAME);
  exit(EXIT_FAILURE);
}

/* ----------------------------------------------------------------------- */

static void exit_tidy(void)
{
  if (may_kill_shared_module(task_name))
    (void)_swix(OS_Module, _INR(0,1), OSModule_Delete, Module_Title);

  free(save_path);

#ifdef FORTIFY
  Fortify_CheckAllMemory();
  Fortify_ListAllMemory();
#endif /* FORTIFY */
}

/* -------------------------- Event handlers ---------------------------- */

/*
 * Event handler to be called when toolbox event ToolboxEvent_Quit
 * is generated (click on the 'Quit' entry of the iconbar menu)
 */

static int quit_event(int event_code, ToolboxEvent *event, IdBlock *id_block, void *handle)
{
  NOT_USED(event_code);
  NOT_USED(event);
  NOT_USED(id_block);
  NOT_USED(handle);

  exit(EXIT_SUCCESS);
  return 1; /* claim event */
}

/*
 * Event handler for custom Toolbox events
 */
static int misc_event_handler(int event_code, ToolboxEvent *event, IdBlock *id_block,void *handle)
{
  const _kernel_oserror *e = NULL;

  NOT_USED(event);
  NOT_USED(id_block);
  NOT_USED(handle);

  switch (event_code)
  {
    case ToolboxEvent_Choices:
      /* Show the configuration dialogue box */
      e = show_setup();
      break;

    case ToolboxEvent_ResetCount:
      /* Reset the counter used as a screenshot filename suffix */
      if (_kernel_oscli("SGrabResetCount") == _kernel_ERROR)
        e = _kernel_last_oserror();
      break;

    case ToolboxEvent_Help:
      /* Open the application's user manual */
      if (_kernel_oscli("Filer_Run "APP_NAME"Res:!Help") == _kernel_ERROR)
        e = _kernel_last_oserror();
      break;

    default:
      return 0; /* event not handled */
  }

  ON_ERR_RPT(e);
  return 1; /* claim event */
}

/*
 * Event handler to be called when toolbox event SaveAs_SaveToFile is generated
 * (user wants to export a file of commands that will reconfigure the module).
 */
static int save_to_file_event(int event_code, ToolboxEvent *event, IdBlock *id_block, void *handle)
{
  SaveAsSaveToFileEvent *sastfe = (SaveAsSaveToFileEvent *) event;
  FILE *f; /* output file handle */
  int chars_out; /* no. of characters transmitted by fprintf() */
  int req;
  char *cmd_buffer;
  const _kernel_oserror *e = NULL;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  /* Find buffer size required for the configuration command
     (an extra byte will be required for the nul terminator) */
  req = make_config_cmd(NULL, 0) + 1;

  /* Allocate a buffer for the configuration command */
  cmd_buffer = malloc(req);
  if (cmd_buffer == NULL)
  {
    e = msgs_error(DUMMY_ERRNO, "NoMem");
  }
  else
  {
    /* Synthesise the configuration command */
    (void)make_config_cmd(cmd_buffer, req);

    _kernel_last_oserror(); /* clear any previous OS error */

    f = fopen(sastfe->filename, "w"); /* open text file for writing */
    if (f == NULL)
    {
      e = _kernel_last_oserror();
      if (e == NULL)
        e = msgs_error_subn(DUMMY_ERRNO, "OpenOutFail", 1, sastfe->filename);
    }
    else
    {
      chars_out = fprintf(f,
                          "| This file was generated automatically by the "
                            APP_NAME" application\n");

      if (chars_out > 0)
      {
        chars_out = fprintf(f,
                            "RMEnsure "Module_Title" "Module_VersionString" "
                              "If \"<"APP_NAME"$Dir>\" <> \"\" Then "
                              "RMLoad <"APP_NAME"$Dir>.ScrGrabber Else Error "
                              "\"Can't find the !"APP_NAME" application.\"\n");
      }

      if (chars_out > 0)
        chars_out = fprintf(f, "%s\n", cmd_buffer);

      fclose(f);

      if (chars_out <= 0)
      {
        /* Failed whilst writing to output file */
        e = _kernel_last_oserror();
        if (e == NULL)
          e = msgs_error_subn(DUMMY_ERRNO, "WriteFail", 1, sastfe->filename);
      }
      else
      {
        /* Set file type */
        e = set_file_type(sastfe->filename, FileType_Obey);
      }
    }

    /* Deallocate the configuration command buffer */
    free(cmd_buffer);
  }

  ON_ERR_RPT(e);

  /* Notify the SaveAs module that save to file succeeded or failed */
  (void)saveas_file_save_completed(e == NULL,
                                   id_block->self_id,
                                   sastfe->filename);
  return 1; /* claim event */
}

/*
 * Message handler to be called on receipt of a Quit message from the Wimp.
 */
static int quit_message(WimpMessage *message, void *handle)
{
  NOT_USED(message);
  NOT_USED(handle);

  exit(EXIT_SUCCESS);
  return 1;
}

/*
 * Event handler to be called when toolbox event Toolbox_ObjectAutoCreated
 * is generated (for recording IDs)
 */
static int objectcreated_event(int event_code, ToolboxEvent *event, IdBlock *id_block, void *handle)
{
  /* An object has been created */
  const ToolboxObjectAutoCreatedEvent *toace =
          (ToolboxObjectAutoCreatedEvent *) event;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  if (strcmp(toace->template_name, "Window") == NULL)
  {
    /* The Choices dialogue box has been created */
    setup_created(id_block->self_id);
  }
  else if (strcmp(toace->template_name, "SaveAs") == NULL)
  {
    save_id = id_block->self_id;

    EF(event_register_toolbox_handler(save_id, SaveAs_SaveToFile,
    save_to_file_event, 0));

    return 1; /* claim event */
  }
  else
  {
    return 0;/* not handled */
  }
  return 1; /* claim event */
}

/* ------------------------ Program framework --------------------------- */

static  WimpPollBlock  poll_block;
static  IdBlock        id_block;
static  MessagesFD     mfd;

static void initialise()
{

  int    toolbox_events = 0,
         wimp_messages = 0;
  const _kernel_oserror *e;

  DEBUG_SET_OUTPUT(DebugOutput_Reporter, APP_NAME);

#ifdef FORTIFY
  Fortify_SetOutputFunc(fortify_output);
  Fortify_SetAllocateFailRate(5);
#endif /* FORTIFY */

  hourglass_on();

  /*
   * register ourselves with the Toolbox.
   */
  e = toolbox_initialise(0,
                         KnownWimpVersion,
                         &wimp_messages,
                         &toolbox_events,
                         "<"APP_NAME"Res$Dir>",
                         &mfd,
                         &id_block,
                         &wimp_version,
                         NULL,
                         NULL);
  if (e != NULL)
    simple_exit(e);

  /*
   * Look up the localised task name and use it to initialise the error
   * reporting module.
   */
  e = messagetrans_lookup(&mfd,
                          "_TaskName",
                          task_name,
                          sizeof(task_name),
                          NULL,
                          0);
  if (e != NULL)
    simple_exit(e);

  e = err_initialise(task_name, wimp_version >= MinExtErrorWimpVersion, &mfd);
  if (e != NULL)
    simple_exit(e);

  /*
   * Install an exit handler to kill the ScreenGrabber module.
   */
  atexit(exit_tidy);

  /*
   * Initialise the message lookup module.
   */
  EF(msgs_initialise(&mfd));

  /*
   * initialise the event library.
   */

  EF(event_initialise (&id_block));
  EF(event_set_mask (Wimp_Poll_NullMask |
                     Wimp_Poll_PointerLeavingWindowMask |
                     Wimp_Poll_PointerEnteringWindowMask |
                     Wimp_Poll_KeyPressedMask | /* Dealt with by Toolbox */
                     Wimp_Poll_LoseCaretMask |
                     Wimp_Poll_GainCaretMask));

  EF(event_register_toolbox_handler(-1,
                                    Toolbox_ObjectAutoCreated,
                                    objectcreated_event,
                                    0));

  EF(event_register_toolbox_handler(-1, -1, misc_event_handler, 0));

  /*
   * register handler for toolbox event ToolboxEvent_Quit,
   * which is generated by the 'Quit' option on the
   * iconbar menu.  Also register message handlers
   * to quit properly when quit messages are
   * received from the wimp.
   */
  EF(event_register_toolbox_handler(-1, ToolboxEvent_Quit, quit_event, 0));
  EF(event_register_message_handler(Wimp_MQuit, quit_message, 0));

  /* Load default or user-saved configuration */
  save_path = strdup("SGrab"); /* in case it's missing from the config file */
  if (file_exists("Choices:"APP_NAME".Choices"))
    EF(load_config("Choices:"APP_NAME".Choices"));
  else
    EF(load_config("<"APP_NAME"$Dir>.Defaults"));

  EF(configure_module()); /* Whip the module into line */

  hourglass_off();
}

int main(int argc, const char *argv[])
{
  int event_code;

  NOT_USED(argc);
  NOT_USED(argv);

  initialise();

  /*
   * poll loop
   */
  while (TRUE)
  {
    event_poll (&event_code, &poll_block, 0);
  }
}
