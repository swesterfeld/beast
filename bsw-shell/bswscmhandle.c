/* BSW-SCM - Bedevilled Sound Engine Scheme Wrapper
 * Copyright (C) 2002 Tim Janik
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */
#define G_LOG_DOMAIN "BswShell"

#include "bswscmhandle.h"

#include <bse/bse.h>


/* --- typedefs & structures --- */
struct _BswSCMHandle
{
  BseErrorType error;
  GValue       rvalue;
  GValue       uvalue;
  BseStorage  *storage;
};


/* --- variables --- */
static GTrashStack *handle_trash = NULL;


/* --- functions --- */
BswSCMHandle*
bsw_scm_handle_alloc (void)
{
  BswSCMHandle *handle;

  handle = g_trash_stack_pop (&handle_trash);
  if (!handle)
    {
      handle = g_new (BswSCMHandle, 1);
      handle->rvalue.g_type = 0;
      handle->uvalue.g_type = 0;
      handle->storage = bse_storage_new ();
    }
  handle->error = 0;
  g_assert (handle->rvalue.g_type == 0);
  g_assert (handle->uvalue.g_type == 0);
  g_assert (handle->storage != NULL);
  bse_storage_enable_proxies (handle->storage);
  bse_storage_prepare_write (handle->storage, TRUE);

  return handle;
}

void
bsw_scm_handle_set_proc (BswSCMHandle *handle,
			 const gchar  *proc_name)
{
  g_return_if_fail (handle != NULL);
  g_return_if_fail (proc_name != NULL);

  bse_storage_handle_break (handle->storage);
  bse_storage_printf (handle->storage, "(bse-proc-call \"%s\"", proc_name);
  bse_storage_push_level (handle->storage);
}

void
bsw_scm_handle_clean (BswSCMHandle *handle)
{
  g_return_if_fail (handle != NULL);

  bse_storage_reset (handle->storage);
  if (G_VALUE_TYPE (&handle->rvalue) != 0)
    g_value_unset (&handle->rvalue);
  if (G_VALUE_TYPE (&handle->uvalue) != 0)
    g_value_unset (&handle->uvalue);
}

void
bsw_scm_handle_destroy (BswSCMHandle *handle)
{
  g_return_if_fail (handle != NULL);

  bsw_scm_handle_clean (handle);
  g_trash_stack_push (&handle_trash, handle);
}

void
bsw_scm_handle_putunset (BswSCMHandle *handle,
			 GValue       *value)
{
  g_return_if_fail (handle != NULL);
  g_return_if_fail (G_IS_VALUE (value));

  bse_storage_break (handle->storage);
  bse_storage_put_value (handle->storage, value, NULL);
  g_value_unset (value);
}

BswErrorType
bsw_scm_handle_eval (BswSCMHandle *handle)
{
  g_return_val_if_fail (handle != NULL, BSW_ERROR_INTERNAL);
  g_return_val_if_fail (G_VALUE_TYPE (&handle->rvalue) == 0, BSW_ERROR_INTERNAL);

  /* close statement */
  bse_storage_pop_level (handle->storage);
  bse_storage_handle_break (handle->storage);
  bse_storage_putc (handle->storage, ')');

  {
    const gchar *expr;
    gchar *rstr, *warnings;

    expr = bse_storage_peek_text (handle->storage, NULL);

    // g_print ("CALL: %s\n", expr);
    {
      BseErrorType error;
      GValue value = { 0, };
      gchar *warnings;

      warnings = bse_procedure_eval (expr, &error, &value);
      rstr = bse_procedure_marshal_retval (error, &value, warnings);
      g_value_unset (&value);
    }
    // g_print ("RETURN: %s\n", rstr);

    warnings = bse_procedure_unmarshal_retval (rstr, &handle->error, &handle->rvalue);
    g_free (rstr);
    if (warnings)
      g_printerr ("during remote procedure call:\n%s\n", warnings);
    g_free (warnings);
  }

  return BSW_ERROR_NONE;
}

GValue*
bsw_scm_handle_peekret (BswSCMHandle *handle,
			GType         type)
{
  g_return_val_if_fail (handle != NULL, NULL);
  g_return_val_if_fail (G_IS_VALUE (&handle->rvalue), NULL);
  g_return_val_if_fail (G_VALUE_TYPE (&handle->uvalue) == 0, NULL);

  g_value_init (&handle->uvalue, type);
  g_value_transform (&handle->rvalue, &handle->uvalue);

  return &handle->uvalue;
}
