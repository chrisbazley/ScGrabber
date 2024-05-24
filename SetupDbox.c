/*
 *  ScreenGrabber (choices dialogue box)
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

/* 04.09.09 CJB Moved this code to a separate source file of its own.
*/

/* ANSI headers */
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* RISC OS headers */
#include "kernel.h"
#include "toolbox.h"
#include "event.h"
#include "window.h"
#include "gadgets.h"
#include "wimp.h"
#include "wimplib.h"

/* CBLibrary headers */
#include "debug.h"
#include "macros.h"
#include "msgtrans.h"
#include "err.h"
#include "deiconise.h"
#include "gadgetutil.h"
#include "pathtail.h"
#include "strextra.h"

/* Local headers */
#include "FEutils.h"
#include "SGFrontEnd.h"
#include "MakeConfig.h"
#include "SetupDbox.h"
#include "ConfigFile.h"
#include "KeyNames.h"

#ifdef FORTIFY
#include "FORTIFY:FORTIFY.h"
#endif

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
  ComponentId_KeyNumber     = 0x18,
  ComponentId_DefaultIsFilm = 0x1a,
  ComponentId_NewSpriteType = 0x1c,
  ComponentId_KeyName       = 0x1d
};

enum
{
  InitialBufferSize      = 16,
  BufferGrowthMultiplier = 2,
  ErrNum_MessageNotFound = 2754,
  MaxInternalKeyNumber   = 256
};

static ObjectId setup_id;
static int my_ref; /* for connecting DataSave and DataSaveAck messages */
static bool unknown_key = false; /* If true then 'Unknown' is currently
                                    displayed as the hot key name */
static const struct
{
  ComponentId  component_id;
  bool        *flag;
}
opt_mapping[] =
{
  {
    ComponentId_KeyEnabled,
    &grab_enable
  },
  {
    ComponentId_SavePalette,
    &save_palette
  },
  {
    ComponentId_DefaultIsFilm,
    &force_film
  },
  {
    ComponentId_NewSpriteType,
    &new_sprite
  }
};

/* Array of internal key numbers corresponding to string set indices */
static unsigned char stringset_mapping[MaxInternalKeyNumber];

static ToolboxEventHandler radiobutton_event, actionbutton_event, dragended_event, numberrange_event, stringset_event;
static WimpMessageHandler datasaveack_message;
static const _kernel_oserror *setup_set_faded(bool use_interval);
static bool setup_get_state(void);
static const _kernel_oserror *setup_set_state(void);
static void close_msgs_exit(void);
static const _kernel_oserror *setup_set_name(unsigned int key_code);

/* ----------------------------------------------------------------------- */

void setup_created(ObjectId id)
{
  const _kernel_oserror *e = NULL;
  static const struct
  {
    int                  event_code;
    ToolboxEventHandler *handler;
  }
  tbox_handlers[] =
  {
    {
      RadioButton_StateChanged,
      radiobutton_event
    },
    {
      ActionButton_Selected,
      actionbutton_event
    },
    {
      Draggable_DragEnded,
      dragended_event
    },
    {
      NumberRange_ValueChanged,
      numberrange_event
    },
    {
      StringSet_ValueChanged,
      stringset_event
    }
  };
  unsigned int i;

  setup_id = id;

  /* Register various Toolbox event handlers */
  for (i = 0; i < ARRAY_SIZE(tbox_handlers); i++)
  {
    EF(event_register_toolbox_handler(id,
                                      tbox_handlers[i].event_code,
                                      tbox_handlers[i].handler,
                                      0));
  }

  /* Register a DataSaveAck message handler */
  EF(event_register_message_handler(Wimp_MDataSaveAck, datasaveack_message, 0));

  /* Try to open a file containing names for internal key numbers */
  e = open_key_msgs();
  if (e == NULL)
  {
    unsigned int k, l;
    char *available = NULL;
    size_t av_len = 0, av_size = 0;

    /* Ensure that the key names file is closed on exit */
    atexit(close_msgs_exit);

    /* Populate a string set with the key names and record the key number
       corresponding to each member of the string set. */
    l = 0;
    for (k = 0; k < ARRAY_SIZE(stringset_mapping); k++)
    {
      const char *key_name;
      size_t new_size;
      int len;
      static const char *esc_seq[] = {"\\,","\\\\"}; /* multi-character escape sequences */

      e = lookup_key_name(k, &key_name);
      if (e != NULL && e->errnum == ErrNum_MessageNotFound)
      {
        DEBUGF("Suppressing error '%s'\n", e->errmess);
        e = NULL;
      }
      EF(e);

      if (key_name == NULL)
        continue;

      /* Find the buffer size required to inflate the key name string */
      len = strinflate(NULL, 0, key_name, ",\\", esc_seq);
      DEBUGF("String will be inflated from %d to %d bytes\n", strlen(key_name), len);

      /* Check that there is enough space in the string buffer for the
         key name and a trailing comma */
      new_size = av_len + len + 1;
      DEBUGF("Required buffer length will be %d bytes\n", new_size);
      if (new_size > av_size)
      {
        /* Extend the string buffer to accommodate more key names */
        char *new_av;

        if (av_size == 0)
          av_size = InitialBufferSize;

        while (new_size > av_size)
          av_size *= BufferGrowthMultiplier; /* geometric growth */

        DEBUGF("About to extend string buffer to %u bytes\n", av_size);
        new_av = realloc(available, av_size);
        if (new_av == NULL)
        {
          EF(msgs_error(DUMMY_ERRNO, "NoMem"));
          break;
        }
        else
        {
          available = new_av;
        }
      }

      /* Inflate the key name string by replacing characters that would otherwise have a
         special meaning for stringset_set_available with escape sequences. */
      assert(av_size >= av_len);
      (void)strinflate(available + av_len, av_size - av_len, key_name, ",\\", esc_seq);

      //DEBUGF("String set is now: '%s'\n", available);

      av_len = new_size;
      available[new_size - 1] = ','; /* overwrite nul terminator */

      /* Record the internal key number corresponding to this member of the
         string set. */
      assert(l < ARRAY_SIZE(stringset_mapping));
      stringset_mapping[l++] = k;
    }
    available[av_len - 1] = '\0'; /* reinstate nul terminator */
    EF(stringset_set_available(0, setup_id, ComponentId_KeyName, available));
    free(available);
  }
  else
  {
    /* 'File not found' errors cause the string set to be faded */
    if (e->errnum == 214)
      e = set_gadget_faded(setup_id, ComponentId_KeyName, true);
    EF(e);
  }
}

/* ----------------------------------------------------------------------- */

const _kernel_oserror *show_setup(void)
{
  ON_ERR_RTN_E(setup_set_state());

  return DeIconise_show_object(0,
                               setup_id,
                               Toolbox_ShowObject_Centre,
                               0,
                               NULL_ObjectId,
                               NULL_ComponentId);
}

/* ----------------------------------------------------------------------- */

const _kernel_oserror *configure_module(void)
{
  const _kernel_oserror *e = NULL;
  char *cmd_buffer;
  int req;

  DEBUGF("Configuring back-end\n");

  /* Find string buffer size required for the configuration command
     (an extra byte will be required for the nul terminator) */
  req = make_config_cmd(NULL, 0) + 1;
  DEBUGF("%u bytes required for star command\n", req);

  /* Allocate a string buffer of appropriate size */
  cmd_buffer = malloc(req);
  if (cmd_buffer == NULL)
  {
    e = msgs_error(DUMMY_ERRNO, "NoMem");
  }
  else
  {
    /* Execute the configuration command */
    (void)make_config_cmd(cmd_buffer, req);

    DEBUGF("Executing command '%s'\n", cmd_buffer);
    if (_kernel_oscli(cmd_buffer) == _kernel_ERROR)
      e = _kernel_last_oserror();

    /* Deallocate the string buffer */
    free(cmd_buffer);
  }

  return e;
}

/* ----------------------------------------------------------------------- */

static void close_msgs_exit(void)
{
  ON_ERR_RPT(close_key_msgs());
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *setup_set_state(void)
{
  ComponentId radio;
  unsigned int i;

  /* Set the displayed key code / name */
  ON_ERR_RTN_E(numberrange_set_value(0,
                                     setup_id,
                                     ComponentId_KeyNumber,
                                     key_code));

  ON_ERR_RTN_E(setup_set_name(key_code));

  /* Set the state of the option buttons */
  for (i = 0; i < ARRAY_SIZE(opt_mapping); i++)
  {
    ON_ERR_RTN_E(optionbutton_set_state(0,
                                        setup_id,
                                        opt_mapping[i].component_id,
                                        *opt_mapping[i].flag ? 1 : 0));
  }

  /* Set the displayed base file path */
  ON_ERR_RTN_E(writablefield_set_value(0,
                                       setup_id,
                                       ComponentId_FilePath,
                                       save_path));

  /* Set the displayed filming speed */
  switch (repeat_type)
  {
    case RepeatType_AutoSync:
      radio = ComponentId_AutoSync;
      break;

    case RepeatType_HalfSync:
      radio = ComponentId_HalfSync;
      break;

    default:
      assert(repeat_type == RepeatType_Interval);
      radio = ComponentId_UseInterval;
      break;
  }

  ON_ERR_RTN_E(radiobutton_set_state(0, setup_id, radio, 1));

  ON_ERR_RTN_E(numberrange_set_value(0,
                                     setup_id,
                                     ComponentId_IntervalValue,
                                     interval));

  return setup_set_faded(repeat_type == RepeatType_Interval);
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *setup_set_faded(bool use_interval)
{
  ON_ERR_RTN_E(set_gadget_faded(setup_id,
                                ComponentId_IntervalLabel,
                                !use_interval));

  return set_gadget_faded(setup_id, ComponentId_IntervalValue, !use_interval);
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *setup_set_name(unsigned int key_code)
{
  const char *key_name;
  const _kernel_oserror *e;

  e = lookup_key_name(key_code, &key_name);
  if (e != NULL && e->errnum == ErrNum_MessageNotFound)
  {
    DEBUGF("Suppressing error '%s'\n", e->errmess);
    e = NULL;
  }

  if (e == NULL)
  {
    /* If the previously selected key was nameless and so is the newly
       selected key then don't update the string set (avoids flicker). */
    if (key_name == NULL && unknown_key)
    {
      DEBUGF("Key name is still unknown\n");
    }
    else
    {
      if (key_name == NULL)
      {
        unknown_key = true;
        key_name = msgs_lookup("UK");
      }
      else
      {
        unknown_key = false;
      }

      DEBUGF("Displaying key name '%s'\n", key_name);
      e = stringset_set_selected(0,
                                 setup_id,
                                 ComponentId_KeyName,
                                 (char *)key_name);
    }
  }
  return e; /* success */
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *get_file_path(char **new_fname)
{
  const _kernel_oserror *e = NULL;
  int new_fname_len;

  assert(new_fname != NULL);

  /* Find buffer size required to get the base file path */
  e = writablefield_get_value(0,
                              setup_id,
                              ComponentId_FilePath,
                              NULL,
                              0,
                              &new_fname_len);
  if (e == NULL)
  {
    /* Allocate a string buffer of appropriate size */
    char *fname;

    DEBUGF("%u bytes required for displayed base file path\n", new_fname_len);
    fname = malloc(new_fname_len);
    if (fname == NULL)
    {
      e = msgs_error(DUMMY_ERRNO, "NoMem");
    }
    else
    {
      /* Get the new base file path */
      e = writablefield_get_value(0,
                                  setup_id,
                                  ComponentId_FilePath,
                                  fname,
                                  new_fname_len,
                                  NULL);
      if (e == NULL)
      {
        *new_fname = fname;
        DEBUGF("Displayed base file path is '%s'\n", fname);
      }
      else
      {
        free(fname);
      }
    }
  }
  return e;
}

/* ----------------------------------------------------------------------- */

static bool setup_get_state(void)
{
  char *new_fname = NULL;
  int state, len;
  ComponentId selected;
  bool success = false;
  const _kernel_oserror *e = NULL;
  unsigned int i;

  /* Get the new base file path */
  e = get_file_path(&new_fname);
  if (e != NULL)
    goto error;

  /* Check the length of the new leaf name */
  len = strlen(pathtail(new_fname, 1));
  DEBUGF("Length of leaf name is %d\n", len);
  if (len > 5)
  {
    /* Leaf name may be too long for old filing systems */
    if (!dialogue_confirm(msgs_lookup("NameLen")))
      goto error;
  }

  len = strlen(new_fname);
  DEBUGF("Length of new base file path is %d\n", len);
  if (len < 1)
  {
    /* File path is too short (*SGrabFilename requires a parameter) */
    WARN("NameLen2");
    goto error;
  }

  /* Replace the current base file path */
  free(save_path);
  save_path = new_fname;
  new_fname = NULL;

  /* Get the displayed key code */
  ON_ERR_RTN_E(numberrange_get_value(0,
                                     setup_id,
                                     ComponentId_KeyNumber,
                                     &state));
  key_code = state;

  /* Get the state of the option buttons */
  for (i = 0; i < ARRAY_SIZE(opt_mapping); i++)
  {
    e = optionbutton_get_state(0,
                               setup_id,
                               opt_mapping[i].component_id,
                               &state);
    if (e != NULL)
      goto error;

    *opt_mapping[i].flag = (state != 0);
  }

  e = radiobutton_get_state(0, setup_id, ComponentId_AutoSync, NULL, &selected);
  if (e != NULL)
    goto error;

  switch (selected)
  {
    case ComponentId_AutoSync:
      repeat_type = RepeatType_AutoSync;
      break;

    case ComponentId_HalfSync:
      repeat_type = RepeatType_HalfSync;
      break;

    default:
      assert(selected == ComponentId_UseInterval);
      repeat_type = RepeatType_Interval;
      break;
  }

  e = numberrange_get_value(0,
                            setup_id,
                            ComponentId_IntervalValue,
                            &state);
  if (e == NULL)
  {
    interval = state;
    success = true;
  }

error:
  ON_ERR_RPT(e);
  free(new_fname);
  return success;
}

/* -------------------------- Event handlers ---------------------------- */

/*
 * Event handler to be called when toolbox event NumberRange_ValueChanged
 * is generated (update key name from number)
 */
static int numberrange_event(int event_code, ToolboxEvent *event, IdBlock *id_block,void *handle)
{
  const NumberRangeValueChangedEvent *nrvce =
    (NumberRangeValueChangedEvent *)event;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  /* Check that this event concerns the key code in the Choices dialogue box */
  if (id_block->self_id != setup_id ||
      id_block->self_component != ComponentId_KeyNumber)
    return 0; /* not handled */

  /* Update the key name displayed by the adjacent stringset gadget */
  ON_ERR_RPT(setup_set_name(nrvce->new_value));

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

/*
 * Event handler to be called when toolbox event StringSet_ValueChanged
 * is generated (update key number from name)
 */
static int stringset_event(int event_code, ToolboxEvent *event, IdBlock *id_block,void *handle)
{
  const _kernel_oserror *e = NULL;
  int selected;

  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  /* Check that this event concerns the key code in the Choices dialogue box */
  if (id_block->self_id != setup_id ||
      id_block->self_component != ComponentId_KeyName)
    return 0; /* not handled */

  /* Get the index of the newly-selected member of the string set */
  e = stringset_get_selected(StringSet_IndexedSelection,
                             setup_id,
                             ComponentId_KeyName,
                             &selected);
  if (e == NULL)
  {
    /* Update the key number displayed by the adjacent number range gadget */
    assert(selected < ARRAY_SIZE(stringset_mapping));
    e = numberrange_set_value(0,
                              setup_id,
                              ComponentId_KeyNumber,
                              stringset_mapping[selected]);
  }
  ON_ERR_RPT(e);
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

/*
 * Event handler to be called when toolbox event RadioButton_StateChanged
 * is generated (update shading in setup window)
 */
static int radiobutton_event(int event_code, ToolboxEvent *event, IdBlock *id_block, void *handle)
{
  const RadioButtonStateChangedEvent *rbsce =
    (RadioButtonStateChangedEvent *)event;

  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  /* Check that the state of the 'fixed interval' button has changed */
  if (id_block->self_id != setup_id ||
      id_block->self_component != ComponentId_UseInterval)
    return 0; /* not handled */

  ON_ERR_RPT(setup_set_faded(rbsce->state));

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

/*
 * Event handler to be called when toolbox event ActionButton_Selected
 * is generated (action buttons in Choices dialogue box).
 */
static int actionbutton_event(int event_code, ToolboxEvent *event, IdBlock *id_block, void *handle)
{
  const _kernel_oserror *e = NULL;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  /* Check that this event concerns the Choices dialogue box */
  if (id_block->self_id != setup_id)
    return 0; /* not handled */

  switch (id_block->self_component)
  {
    case ComponentId_CancelButton:
      /* Restore internal settings */
      setup_set_state();
      break;

    case ComponentId_SetButton:
    case ComponentId_SaveButton:
      /* Update internal settings */
      if (!setup_get_state())
        break;

      /* Hide dialogue box unless ADJUST-click on button */
      if (!TEST_BITS(event->hdr.flags, ActionButton_Selected_Adjust))
      {
        e = toolbox_hide_object(0, setup_id);
        if (e != NULL)
          break;
      }

      /* Reconfigure back-end module */
      e = configure_module();
      if (e != NULL)
        break;

      /* Save configuration to file if it's that button */
      if (id_block->self_component == ComponentId_SaveButton)
      {
        if (getenv("Choices$Write") == NULL)
        {
          WARN("NoChoices");
        }
        else
        {
          /* Ensure that our application's sub-directory exists in the
             global choices location */
          _kernel_osfile_block inout;
          inout.start = 0; /* default number of entries */
          if (_kernel_osfile(8, "<Choices$Write>.ScGrabber", &inout) ==
              _kernel_ERROR)
            e = _kernel_last_oserror();
          else
            e = save_config("<Choices$Write>.ScGrabber.Choices");
        }
      }
      break;

    default:
      return 0; /* event not handled */
  }

  ON_ERR_RPT(e);
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

/*
 * Event handler to be called when toolbox event DraggableDragEndedEvent
 * is generated (probe for file path)
 */
static int dragended_event(int event_code, ToolboxEvent *event, IdBlock *id_block,void *handle)
{
  const DraggableDragEndedEvent *todde = (DraggableDragEndedEvent *)event;
  char *new_fname = NULL;
  const _kernel_oserror *e = NULL;
  WimpMessage msg;
  const char *leaf_name;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  /* Check that this event concerns the Choices dialogue box */
  if (id_block->self_id != setup_id)
    return 0; /* not handled */

  /* Get the new base file path */
  e = get_file_path(&new_fname);
  if (e == NULL)
  {
    /* Copy only the leaf name into the body of the Wimp message */
    leaf_name = pathtail(new_fname, 1);
    STRCPY_SAFE(msg.data.data_save.leaf_name, leaf_name);
    free(new_fname);

    /* Send a DataSave message to ask for full path from Filer window */
    msg.hdr.size = sizeof(msg);
    msg.hdr.your_ref = 0;
    msg.hdr.action_code = Wimp_MDataSave;
    msg.data.data_save.destination_window = todde->window_handle;
    msg.data.data_save.destination_icon = todde->icon_handle;
    msg.data.data_save.destination_x = todde->x;
    msg.data.data_save.destination_y = todde->y;
    msg.data.data_save.estimated_size = 0;
    msg.data.data_save.file_type = FileType_Sprite;
    e = wimp_send_message(Wimp_EUserMessage,
                          &msg,
                          todde->window_handle,
                          todde->icon_handle,
                          0);
    if (e == NULL)
      my_ref = msg.hdr.my_ref;
  }

  ON_ERR_RPT(e);
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

/*
 * Message handler to be called on receipt of a DataSaveAck (set file path).
 */
static int datasaveack_message(WimpMessage *message, void *handle)
{
  assert(message != NULL);
  NOT_USED(handle);

  /* Check that it is a reply to our DataSave message */
  if (message->hdr.your_ref != my_ref)
    return 0; /* event not handled */

  /* Check that it is a proper file path, not just a temporary file */
  if (message->data.data_save_ack.estimated_size == -1)
  {
    err_report(0, msgs_lookup("NoAppDrag"));
  }
  else
  {
    /* Display the complete file path in the dialogue box */
    ON_ERR_RPT(writablefield_set_value(0,
                                       setup_id,
                                       ComponentId_FilePath,
                                       message->data.data_save_ack.leaf_name));
  }

  return 1; /* claim event */
}
