/* GSL - Generic Sound Layer
 * Copyright (C) 2001-2005 Tim Janik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "gslloader.h"

#include "gsldatahandle.h"
#include "gsldatahandle-vorbis.h"
#include "bsemath.h"
#include <sfi/sfistore.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


#define parse_or_return(scanner, token) { guint _t = (token); \
                                          if (g_scanner_get_next_token (scanner) != _t) \
                                            return _t; \
                                        }
#define DEBUG(...)                      sfi_debug ("bsewave", __VA_ARGS__)


/* --- token types --- */
typedef enum
{
  /* keyword tokens */
  BSEWAVE_TOKEN_WAVE           = 512,
  BSEWAVE_TOKEN_CHUNK,
  BSEWAVE_TOKEN_NAME,
  BSEWAVE_TOKEN_N_CHANNELS,
  BSEWAVE_TOKEN_MIDI_NOTE,
  BSEWAVE_TOKEN_OSC_FREQ,
  BSEWAVE_TOKEN_XINFO,
  BSEWAVE_TOKEN_OGG_LINK,
  BSEWAVE_TOKEN_FILE,
  BSEWAVE_TOKEN_INDEX,          /* file (auto detect loader) */
  BSEWAVE_TOKEN_RAW_FILE,
  BSEWAVE_TOKEN_BOFFSET,        /* raw-file */
  BSEWAVE_TOKEN_N_VALUES,       /* raw-file */
  BSEWAVE_TOKEN_RAW_LINK,
  BSEWAVE_TOKEN_BYTE_ORDER,     /* raw-file, raw-link */
  BSEWAVE_TOKEN_FORMAT,         /* raw-file, raw-link */
  BSEWAVE_TOKEN_MIX_FREQ,       /* raw-file, raw-link */
  /* data tokens */
  BSEWAVE_TOKEN_BIG_ENDIAN,
  BSEWAVE_TOKEN_BIG,
  BSEWAVE_TOKEN_LITTLE_ENDIAN,
  BSEWAVE_TOKEN_LITTLE,
  BSEWAVE_TOKEN_SIGNED_8,
  BSEWAVE_TOKEN_SIGNED_12,
  BSEWAVE_TOKEN_SIGNED_16,
  BSEWAVE_TOKEN_UNSIGNED_8,
  BSEWAVE_TOKEN_UNSIGNED_12,
  BSEWAVE_TOKEN_UNSIGNED_16,
  BSEWAVE_TOKEN_ALAW,
  BSEWAVE_TOKEN_ULAW,
  BSEWAVE_TOKEN_FLOAT,
  BSEWAVE_TOKEN_NONE,
  BSEWAVE_TOKEN_JUMP,
  BSEWAVE_TOKEN_PINGPONG,
  BSEWAVE_TOKEN_LAST,
  /* compat tokens */
  BSEWAVE_TOKEN_LOOP_TYPE,      /* compat <= 0.6.4 */
  BSEWAVE_TOKEN_LOOP_START,     /* compat <= 0.6.4 */
  BSEWAVE_TOKEN_LOOP_END,       /* compat <= 0.6.4 */
  BSEWAVE_TOKEN_LOOP_COUNT,     /* compat <= 0.6.4 */
  BSEWAVE_TOKEN_RAWLINK,        /* compat <= 0.6.4 */
  BSEWAVE_TOKEN_OGGLINK,        /* compat <= 0.6.4 */
} GslWaveTokenType;


/* --- tokens --- */
static const char *bsewave_tokens[] = {
  /* keyword tokens */
  "wave",       "chunk",        "name",         "n-channels",
  "midi-note",  "osc-freq",     "xinfo",        "ogg-link",
  "file",       "index",        "raw-file",     "boffset",
  "n-values",   "raw-link",     "byte-order",   "format",
  "mix-freq",
  /* data tokens */
  "big-endian", "big",          "little-endian", "little",
  "signed-8",   "signed-12",    "signed-16",
  "unsigned-8", "unsigned-12",  "unsigned-16",
  "alaw",       "ulaw",         "float",
  "none",	"jump",		"pingpong",
};


/* --- structures --- */
typedef struct
{
  GslWaveFileInfo wfi;
  gchar          *cwd;
} FileInfo;

typedef struct
{
  GslWaveDsc        wdsc;
  GslWaveFormatType dfl_format;
  guint		    dfl_byte_order;
  gfloat	    dfl_mix_freq;
} WaveDsc;

/* GslWaveChunkDsc accessors */
#define LOADER_TYPE(wcd)        ((wcd)->loader_data[0].uint)
#define LOADER_FILE(wcd)        ((wcd)->loader_data[1].ptr)
#define LOADER_INDEX(wcd)       ((wcd)->loader_data[2].ptr)
#define LOADER_FORMAT(wcd)      ((wcd)->loader_data[4].uint)
#define LOADER_BYTE_ORDER(wcd)  ((wcd)->loader_data[5].uint)
#define LOADER_BOFFSET(wcd)     ((wcd)->loader_data[6].uint)
#define LOADER_LENGTH(wcd)      ((wcd)->loader_data[7].uint)
/* loader types */
#define AUTO_FILE_MAGIC         (('A' << 24) | ('u' << 16) | ('t' << 8) | 'F')
#define RAW_FILE_MAGIC          (('R' << 24) | ('a' << 16) | ('w' << 8) | 'F')
#define RAW_LINK_MAGIC          (('R' << 24) | ('a' << 16) | ('w' << 8) | 'L')
#define OGG_LINK_MAGIC          (('O' << 24) | ('g' << 16) | ('g' << 8) | 'L')

/* --- functions --- */
static GTokenType
bsewave_skip_rest_statement (GScanner *scanner,
			     guint     level)
{
  g_return_val_if_fail (scanner != NULL, G_TOKEN_ERROR);
  
  while (level)
    {
      g_scanner_get_next_token (scanner);
      switch (scanner->token)
	{
	case G_TOKEN_EOF: case G_TOKEN_ERROR:                   return '}';
	case '(': case '{': case '[':           level++;        break;
	case ')': case '}': case ']':           level--;        break;
	default:                                                break;
	}
    }
  
  return G_TOKEN_NONE;
}

static GslWaveFileInfo*
bsewave_load_file_info (gpointer      data,
			const gchar  *_file_name,
			BseErrorType *error_p)
{
  FileInfo *fi = NULL;
  gboolean in_wave = FALSE, abort = FALSE;
  SfiRing *wave_names = NULL;
  GScanner *scanner;
  gchar *cwd, *file_name;
  gint fd;
  guint i;
  
  if (g_path_is_absolute (_file_name))
    {
      gchar *p = strrchr (_file_name, G_DIR_SEPARATOR);
      
      g_assert (p != NULL);
      cwd = g_strndup (_file_name, p - _file_name + 1);
      file_name = g_strdup (_file_name);
    }
  else
    {
      cwd = g_get_current_dir ();
      file_name = g_strdup_printf ("%s%c%s", cwd, G_DIR_SEPARATOR, _file_name);
    }
  
  fd = open (file_name, O_RDONLY);
  if (fd < 0)
    {
      *error_p = gsl_error_from_errno (errno, BSE_ERROR_FILE_OPEN_FAILED);
      g_free (cwd);
      g_free (file_name);
      return NULL;
    }
  
  scanner = g_scanner_new64 (sfi_storage_scanner_config);
  scanner->config->cpair_comment_single = "#\n";
  g_scanner_scope_add_symbol (scanner, 0, "wave", GUINT_TO_POINTER (BSEWAVE_TOKEN_WAVE));
  g_scanner_scope_add_symbol (scanner, 0, "name", GUINT_TO_POINTER (BSEWAVE_TOKEN_NAME));
  g_scanner_input_file (scanner, fd);
  while (!abort)
    {
      g_scanner_get_next_token (scanner);
      switch (scanner->token)
	{
	case BSEWAVE_TOKEN_WAVE:
	  if (g_scanner_peek_next_token (scanner) == '{')
	    {
	      g_scanner_get_next_token (scanner); /* eat '{' */
	      in_wave = TRUE;
	    }
	  break;
	case '{':
	  if (bsewave_skip_rest_statement (scanner, 1) != G_TOKEN_NONE)
	    abort = TRUE;
	  break;
	case BSEWAVE_TOKEN_NAME:
	  if (in_wave && g_scanner_peek_next_token (scanner) == '=')
	    {
	      g_scanner_get_next_token (scanner); /* eat '=' */
	      if (g_scanner_peek_next_token (scanner) == G_TOKEN_STRING)
		{
		  gchar *wave_name;
		  
		  g_scanner_get_next_token (scanner); /* eat string */
		  wave_name = g_strdup (scanner->value.v_string);
		  if (bsewave_skip_rest_statement (scanner, 1) == G_TOKEN_NONE)
		    {
		      in_wave = FALSE;
		      wave_names = sfi_ring_append (wave_names, wave_name);
		    }
		  else
		    {
		      g_free (wave_name);
		      abort = TRUE;
		    }
		}
	    }
	  break;
	default:
	  if (scanner->token == G_TOKEN_EOF || scanner->token == G_TOKEN_ERROR)
	    abort = TRUE;
	  break;
	}
    }
  g_scanner_destroy (scanner);
  close (fd);
  
  if (wave_names)
    {
      SfiRing *ring;
      
      fi = sfi_new_struct0 (FileInfo, 1);
      fi->wfi.n_waves = sfi_ring_length (wave_names);
      fi->wfi.waves = g_malloc0 (sizeof (fi->wfi.waves[0]) * fi->wfi.n_waves);
      for (i = 0, ring = wave_names; i < fi->wfi.n_waves; i++, ring = ring->next)
	fi->wfi.waves[i].name = ring->data;
      sfi_ring_free (wave_names);
      fi->cwd = cwd;
    }
  else
    g_free (cwd);
  g_free (file_name);
  
  /* FIXME: empty wave error? */
  
  return fi ? &fi->wfi : NULL;
}

static void
bsewave_free_file_info (gpointer         data,
			GslWaveFileInfo *file_info)
{
  FileInfo *fi = (FileInfo*) file_info;
  guint i;
  
  for (i = 0; i < fi->wfi.n_waves; i++)
    g_free (fi->wfi.waves[i].name);
  g_free (fi->wfi.waves);
  g_free (fi->cwd);
  sfi_delete_struct (FileInfo, fi);
}

static guint
bsewave_parse_chunk_dsc (GScanner        *scanner,
			 GslWaveChunkDsc *chunk)
{
  parse_or_return (scanner, '{');
  do
    switch (g_scanner_get_next_token (scanner))
      {
        gchar *key;
        gfloat vfloat;
      case '}':
	return G_TOKEN_NONE;
      default:
	return '}';
      case BSEWAVE_TOKEN_MIDI_NOTE:
	parse_or_return (scanner, '=');
	parse_or_return (scanner, G_TOKEN_INT);
	chunk->osc_freq = bse_temp_freq (gsl_get_config ()->kammer_freq,
					 ((gint) scanner->value.v_int64) - gsl_get_config ()->midi_kammer_note);
        chunk->xinfos = bse_xinfos_add_num (chunk->xinfos, "midi-note", scanner->value.v_int64);
	break;
      case BSEWAVE_TOKEN_OSC_FREQ:
	parse_or_return (scanner, '=');
	switch (g_scanner_get_next_token (scanner))
	  {
	  case G_TOKEN_FLOAT:	vfloat = scanner->value.v_float;	break;
	  case G_TOKEN_INT:	vfloat = scanner->value.v_int64;	break;
	  default:		return G_TOKEN_FLOAT;
	  }
        chunk->osc_freq = vfloat;
        chunk->xinfos = bse_xinfos_add_num (chunk->xinfos, "osc-freq", vfloat);
	break;
      case BSEWAVE_TOKEN_XINFO:
        parse_or_return (scanner, '[');
        parse_or_return (scanner, G_TOKEN_STRING);
        key = g_strdup (scanner->value.v_string);
        if (g_scanner_peek_next_token (scanner) != ']')
          g_free (key);
        parse_or_return (scanner, ']');
        if (g_scanner_peek_next_token (scanner) != '=')
          g_free (key);
        parse_or_return (scanner, '=');
        if (g_scanner_peek_next_token (scanner) != G_TOKEN_STRING)
          g_free (key);
        parse_or_return (scanner, G_TOKEN_STRING);
        chunk->xinfos = bse_xinfos_add_value (chunk->xinfos, key, scanner->value.v_string);
        g_free (key);
        break;
      case BSEWAVE_TOKEN_OGGLINK:       /* compat <= 0.6.4 */
      case BSEWAVE_TOKEN_OGG_LINK:
	parse_or_return (scanner, '=');
	parse_or_return (scanner, '(');
	parse_or_return (scanner, G_TOKEN_IDENTIFIER);
	if (strcmp (scanner->value.v_identifier, "binary-appendix") != 0)
	  return G_TOKEN_IDENTIFIER;
	parse_or_return (scanner, G_TOKEN_INT);
	LOADER_BOFFSET (chunk) = scanner->value.v_int64;        /* byte offset */
	parse_or_return (scanner, G_TOKEN_INT);
	LOADER_LENGTH (chunk) = scanner->value.v_int64;         /* byte length */
	parse_or_return (scanner, ')');
	g_free (LOADER_FILE (chunk));
	LOADER_FILE (chunk) = NULL;
        LOADER_TYPE (chunk) = OGG_LINK_MAGIC;
	break;
      case BSEWAVE_TOKEN_FILE:
	parse_or_return (scanner, '=');
	parse_or_return (scanner, G_TOKEN_STRING);
        g_free (LOADER_FILE (chunk));
	LOADER_FILE (chunk) = g_strdup (scanner->value.v_string);
        LOADER_TYPE (chunk) = AUTO_FILE_MAGIC;
	break;
      case BSEWAVE_TOKEN_INDEX:
	parse_or_return (scanner, '=');
	parse_or_return (scanner, G_TOKEN_STRING);
	g_free (LOADER_INDEX (chunk));                          /* wave name */
	LOADER_INDEX (chunk) = g_strdup (scanner->value.v_string);
	break;
      case BSEWAVE_TOKEN_RAW_FILE:
	parse_or_return (scanner, '=');
	parse_or_return (scanner, G_TOKEN_STRING);
        g_free (LOADER_FILE (chunk));
	LOADER_FILE (chunk) = g_strdup (scanner->value.v_string);
        LOADER_TYPE (chunk) = RAW_FILE_MAGIC;
	break;
      case BSEWAVE_TOKEN_BOFFSET:
	parse_or_return (scanner, '=');
	parse_or_return (scanner, G_TOKEN_INT);
        LOADER_BOFFSET (chunk) = scanner->value.v_int64;	/* byte offset */
	break;
      case BSEWAVE_TOKEN_N_VALUES:
	parse_or_return (scanner, '=');
	parse_or_return (scanner, G_TOKEN_INT);
        LOADER_LENGTH (chunk) = scanner->value.v_int64;	        /* n_values */
	break;
      case BSEWAVE_TOKEN_RAWLINK:       /* compat <= 0.6.4 */
      case BSEWAVE_TOKEN_RAW_LINK:
	parse_or_return (scanner, '=');
	parse_or_return (scanner, '(');
	parse_or_return (scanner, G_TOKEN_IDENTIFIER);
	if (strcmp (scanner->value.v_identifier, "binary-appendix") != 0)
	  return G_TOKEN_IDENTIFIER;
	parse_or_return (scanner, G_TOKEN_INT);
	LOADER_BOFFSET (chunk) = scanner->value.v_int64;        /* byte offset */
	parse_or_return (scanner, G_TOKEN_INT);
	LOADER_LENGTH (chunk) = scanner->value.v_int64;         /* byte length */
	parse_or_return (scanner, ')');
        LOADER_TYPE (chunk) = RAW_LINK_MAGIC;
	break;
      case BSEWAVE_TOKEN_BYTE_ORDER:
        parse_or_return (scanner, '=');
        g_scanner_get_next_token (scanner);
        switch (scanner->token)
          {
          case BSEWAVE_TOKEN_LITTLE_ENDIAN:
          case BSEWAVE_TOKEN_LITTLE:        LOADER_BYTE_ORDER (chunk) = G_LITTLE_ENDIAN; break;
          case BSEWAVE_TOKEN_BIG_ENDIAN:
          case BSEWAVE_TOKEN_BIG:           LOADER_BYTE_ORDER (chunk) = G_BIG_ENDIAN;    break;
          default:                          return BSEWAVE_TOKEN_LITTLE_ENDIAN;
          }
        break;
      case BSEWAVE_TOKEN_FORMAT:
        parse_or_return (scanner, '=');
        g_scanner_get_next_token (scanner);
        switch (scanner->token)
          {
          case BSEWAVE_TOKEN_SIGNED_8:      LOADER_FORMAT (chunk) = GSL_WAVE_FORMAT_SIGNED_8;    break;
          case BSEWAVE_TOKEN_SIGNED_12:     LOADER_FORMAT (chunk) = GSL_WAVE_FORMAT_SIGNED_12;   break;
          case BSEWAVE_TOKEN_SIGNED_16:     LOADER_FORMAT (chunk) = GSL_WAVE_FORMAT_SIGNED_16;   break;
          case BSEWAVE_TOKEN_UNSIGNED_8:    LOADER_FORMAT (chunk) = GSL_WAVE_FORMAT_UNSIGNED_8;  break;
          case BSEWAVE_TOKEN_UNSIGNED_12:   LOADER_FORMAT (chunk) = GSL_WAVE_FORMAT_UNSIGNED_12; break;
          case BSEWAVE_TOKEN_UNSIGNED_16:   LOADER_FORMAT (chunk) = GSL_WAVE_FORMAT_UNSIGNED_16; break;
          case BSEWAVE_TOKEN_ALAW:          LOADER_FORMAT (chunk) = GSL_WAVE_FORMAT_ALAW;        break;
          case BSEWAVE_TOKEN_ULAW:          LOADER_FORMAT (chunk) = GSL_WAVE_FORMAT_ULAW;        break;
          case BSEWAVE_TOKEN_FLOAT:         LOADER_FORMAT (chunk) = GSL_WAVE_FORMAT_FLOAT;       break;
          default:                          return BSEWAVE_TOKEN_SIGNED_16;
          }
        break;
      case BSEWAVE_TOKEN_MIX_FREQ:
	parse_or_return (scanner, '=');
	switch (g_scanner_get_next_token (scanner))
	  {
	  case G_TOKEN_FLOAT:	chunk->mix_freq = scanner->value.v_float;	break;
	  case G_TOKEN_INT:	chunk->mix_freq = scanner->value.v_int64;	break;
	  default:		return G_TOKEN_FLOAT;
	  }
	break;
      case BSEWAVE_TOKEN_LOOP_TYPE:     /* compat <= 0.6.4 */
	parse_or_return (scanner, '=');
	switch (g_scanner_get_next_token (scanner))
	  {
	  case BSEWAVE_TOKEN_PINGPONG:
            chunk->xinfos = bse_xinfos_add_value (chunk->xinfos, "loop-type", gsl_wave_loop_type_to_string (GSL_WAVE_LOOP_PINGPONG));
            break;
	  case BSEWAVE_TOKEN_JUMP:
            chunk->xinfos = bse_xinfos_add_value (chunk->xinfos, "loop-type", gsl_wave_loop_type_to_string (GSL_WAVE_LOOP_JUMP));
            break;
	  case BSEWAVE_TOKEN_NONE:
            chunk->xinfos = bse_xinfos_del_value (chunk->xinfos, "loop-type");
            break;
	  default:
            break;
	  }
	break;
      case BSEWAVE_TOKEN_LOOP_START:    /* compat <= 0.6.4 */
	parse_or_return (scanner, '=');
	parse_or_return (scanner, G_TOKEN_INT);
        chunk->xinfos = bse_xinfos_add_num (chunk->xinfos, "loop-start", scanner->value.v_int64);
	break;
      case BSEWAVE_TOKEN_LOOP_END:      /* compat <= 0.6.4 */
	parse_or_return (scanner, '=');
	parse_or_return (scanner, G_TOKEN_INT);
        chunk->xinfos = bse_xinfos_add_num (chunk->xinfos, "loop-end", scanner->value.v_int64);
	break;
      case BSEWAVE_TOKEN_LOOP_COUNT:    /* compat <= 0.6.4 */
	parse_or_return (scanner, '=');
	parse_or_return (scanner, G_TOKEN_INT);
        chunk->xinfos = bse_xinfos_add_num (chunk->xinfos, "loop-count", scanner->value.v_int64);
	break;
      }
  while (TRUE);
}

static guint
bsewave_parse_wave_dsc (GScanner    *scanner,
			WaveDsc     *dsc,
			const gchar *wave_name)
{
  parse_or_return (scanner, '{');
  do
    switch (g_scanner_get_next_token (scanner))
      {
	guint i, token;
      case '}':
	return G_TOKEN_NONE;
      default:
	return '}';
      case BSEWAVE_TOKEN_N_CHANNELS:
        if (dsc->wdsc.n_channels != 0)
          return '}';   /* may specify n_channels only once */
	parse_or_return (scanner, '=');
	parse_or_return (scanner, G_TOKEN_INT);
	dsc->wdsc.n_channels = scanner->value.v_int64;
	break;
      case BSEWAVE_TOKEN_NAME:
	if (dsc->wdsc.name)
	  return '}';
	parse_or_return (scanner, '=');
	parse_or_return (scanner, G_TOKEN_STRING);
	if (wave_name)
	  {
	    if (strcmp (wave_name, scanner->value.v_string) == 0)
	      dsc->wdsc.name = g_strdup (scanner->value.v_string);
	    else
	      return bsewave_skip_rest_statement (scanner, 1);
	  }
	else
	  dsc->wdsc.name = g_strdup (scanner->value.v_string);
	break;
      case BSEWAVE_TOKEN_BYTE_ORDER:
	parse_or_return (scanner, '=');
	token = g_scanner_get_next_token (scanner);
	switch (token)
	  {
	  case BSEWAVE_TOKEN_LITTLE_ENDIAN:
	  case BSEWAVE_TOKEN_LITTLE:        dsc->dfl_byte_order = G_LITTLE_ENDIAN; break;
	  case BSEWAVE_TOKEN_BIG_ENDIAN:
	  case BSEWAVE_TOKEN_BIG:	    dsc->dfl_byte_order = G_BIG_ENDIAN;    break;
	  default:			    return BSEWAVE_TOKEN_LITTLE_ENDIAN;
	  }
	break;
      case BSEWAVE_TOKEN_FORMAT:
	parse_or_return (scanner, '=');
	token = g_scanner_get_next_token (scanner);
	switch (token)
	  {
	  case BSEWAVE_TOKEN_SIGNED_8:	    dsc->dfl_format = GSL_WAVE_FORMAT_SIGNED_8;    break;
	  case BSEWAVE_TOKEN_SIGNED_12:     dsc->dfl_format = GSL_WAVE_FORMAT_SIGNED_12;   break;
	  case BSEWAVE_TOKEN_SIGNED_16:     dsc->dfl_format = GSL_WAVE_FORMAT_SIGNED_16;   break;
	  case BSEWAVE_TOKEN_UNSIGNED_8:    dsc->dfl_format = GSL_WAVE_FORMAT_UNSIGNED_8;  break;
	  case BSEWAVE_TOKEN_UNSIGNED_12:   dsc->dfl_format = GSL_WAVE_FORMAT_UNSIGNED_12; break;
	  case BSEWAVE_TOKEN_UNSIGNED_16:   dsc->dfl_format = GSL_WAVE_FORMAT_UNSIGNED_16; break;
	  case BSEWAVE_TOKEN_ALAW:	    dsc->dfl_format = GSL_WAVE_FORMAT_ALAW;	   break;
	  case BSEWAVE_TOKEN_ULAW:	    dsc->dfl_format = GSL_WAVE_FORMAT_ULAW;	   break;
	  case BSEWAVE_TOKEN_FLOAT:	    dsc->dfl_format = GSL_WAVE_FORMAT_FLOAT;       break;
	  default:			    return BSEWAVE_TOKEN_SIGNED_16;
	  }
	break;
      case BSEWAVE_TOKEN_MIX_FREQ:
	parse_or_return (scanner, '=');
	switch (g_scanner_get_next_token (scanner))
	  {
	  case G_TOKEN_FLOAT:   dsc->dfl_mix_freq = scanner->value.v_float;	break;
	  case G_TOKEN_INT:     dsc->dfl_mix_freq = scanner->value.v_int64;	break;
	  default:		return G_TOKEN_FLOAT;
	  }
	break;
      case BSEWAVE_TOKEN_CHUNK:
        if (dsc->wdsc.n_channels < 1)   /* must have n_channels specification */
          {
            g_scanner_warn (scanner, "wave with unspecified number of channels, assuming 1 (mono)");
            dsc->wdsc.n_channels = 1;
          }
	if (g_scanner_peek_next_token (scanner) != '{')
	  parse_or_return (scanner, '{');
	i = dsc->wdsc.n_chunks++;
	dsc->wdsc.chunks = g_realloc (dsc->wdsc.chunks, sizeof (dsc->wdsc.chunks[0]) * dsc->wdsc.n_chunks);
	memset (dsc->wdsc.chunks + i, 0, sizeof (dsc->wdsc.chunks[0]) * 1);
	dsc->wdsc.chunks[i].mix_freq = 0;
	dsc->wdsc.chunks[i].osc_freq = -1;
	token = bsewave_parse_chunk_dsc (scanner, dsc->wdsc.chunks + i);
	if (token != G_TOKEN_NONE)
	  return token;
        if (dsc->wdsc.chunks[i].mix_freq <= 0)
          dsc->wdsc.chunks[i].mix_freq = dsc->dfl_mix_freq;
	if (dsc->wdsc.chunks[i].osc_freq <= 0)
	  g_scanner_error (scanner, "wave chunk \"%s\" without oscilator frequency: mix_freq=%f osc_freq=%f",
			   LOADER_FILE (&dsc->wdsc.chunks[i]) ? (gchar*) LOADER_FILE (&dsc->wdsc.chunks[i]) : "",
			   dsc->wdsc.chunks[i].mix_freq, dsc->wdsc.chunks[i].osc_freq);
        if (dsc->wdsc.chunks[i].osc_freq >= dsc->wdsc.chunks[i].mix_freq / 2.)
          g_scanner_error (scanner, "wave chunk \"%s\" with invalid mixing/oscilator frequency: mix_freq=%f osc_freq=%f",
			   LOADER_FILE (&dsc->wdsc.chunks[i]) ? (gchar*) LOADER_FILE (&dsc->wdsc.chunks[i]) : "",
                           dsc->wdsc.chunks[i].mix_freq, dsc->wdsc.chunks[i].osc_freq);
        break;
      }
  while (TRUE);
}

static void
bsewave_wave_dsc_free (WaveDsc *dsc)
{
  guint i;
  for (i = 0; i < dsc->wdsc.n_chunks; i++)
    {
      g_strfreev (dsc->wdsc.chunks[i].xinfos);
      g_free (LOADER_FILE (&dsc->wdsc.chunks[i]));
      g_free (LOADER_INDEX (&dsc->wdsc.chunks[i]));
    }
  g_free (dsc->wdsc.chunks);
  g_free (dsc->wdsc.name);
  sfi_delete_struct (WaveDsc, dsc);
}

static GslWaveDsc*
bsewave_load_wave_dsc (gpointer         data,
		       GslWaveFileInfo *file_info,
		       guint            nth_wave,
		       BseErrorType    *error_p)
{
  guint token, i;
  
  gint fd = open (file_info->file_name, O_RDONLY);
  if (fd < 0)
    {
      *error_p = gsl_error_from_errno (errno, BSE_ERROR_FILE_OPEN_FAILED);
      return NULL;
    }
  
  GScanner *scanner = g_scanner_new64 (sfi_storage_scanner_config);
  scanner->config->cpair_comment_single = "#\n";
  scanner->input_name = file_info->file_name;
  g_scanner_input_file (scanner, fd);
  for (i = BSEWAVE_TOKEN_WAVE; i < BSEWAVE_TOKEN_LAST; i++)
    g_scanner_scope_add_symbol (scanner, 0, bsewave_tokens[i - BSEWAVE_TOKEN_WAVE], GUINT_TO_POINTER (i));
  /* compat tokens */
  g_scanner_scope_add_symbol (scanner, 0, "n_channels", GUINT_TO_POINTER (BSEWAVE_TOKEN_N_CHANNELS));
  g_scanner_scope_add_symbol (scanner, 0, "midi_note", GUINT_TO_POINTER (BSEWAVE_TOKEN_MIDI_NOTE));
  g_scanner_scope_add_symbol (scanner, 0, "osc_freq", GUINT_TO_POINTER (BSEWAVE_TOKEN_OSC_FREQ));
  g_scanner_scope_add_symbol (scanner, 0, "n_values", GUINT_TO_POINTER (BSEWAVE_TOKEN_N_VALUES));
  g_scanner_scope_add_symbol (scanner, 0, "byte_order", GUINT_TO_POINTER (BSEWAVE_TOKEN_BYTE_ORDER));
  g_scanner_scope_add_symbol (scanner, 0, "mix_freq", GUINT_TO_POINTER (BSEWAVE_TOKEN_MIX_FREQ));
  g_scanner_scope_add_symbol (scanner, 0, "loop_type", GUINT_TO_POINTER (BSEWAVE_TOKEN_LOOP_TYPE));
  g_scanner_scope_add_symbol (scanner, 0, "loop_start", GUINT_TO_POINTER (BSEWAVE_TOKEN_LOOP_START));
  g_scanner_scope_add_symbol (scanner, 0, "loop_end", GUINT_TO_POINTER (BSEWAVE_TOKEN_LOOP_END));
  g_scanner_scope_add_symbol (scanner, 0, "loop_count", GUINT_TO_POINTER (BSEWAVE_TOKEN_LOOP_COUNT));
  g_scanner_scope_add_symbol (scanner, 0, "rawlink", GUINT_TO_POINTER (BSEWAVE_TOKEN_RAW_LINK));
  g_scanner_scope_add_symbol (scanner, 0, "ogglink", GUINT_TO_POINTER (BSEWAVE_TOKEN_OGG_LINK));

  WaveDsc *dsc = sfi_new_struct0 (WaveDsc, 1);
  dsc->wdsc.name = NULL;
  dsc->wdsc.n_chunks = 0;
  dsc->wdsc.chunks = NULL;
  dsc->wdsc.n_channels = 0;
  dsc->wdsc.xinfos = NULL;
  dsc->dfl_format = GSL_WAVE_FORMAT_SIGNED_16;
  dsc->dfl_byte_order = G_LITTLE_ENDIAN;
  dsc->dfl_mix_freq = 44100;
  if (g_scanner_get_next_token (scanner) != BSEWAVE_TOKEN_WAVE)
    token = BSEWAVE_TOKEN_WAVE;
  else
    token = bsewave_parse_wave_dsc (scanner, dsc, file_info->waves[nth_wave].name);
  if (token != G_TOKEN_NONE || scanner->parse_errors) // FIXME: untested/broken branch?
    {
      bsewave_wave_dsc_free (dsc);
      dsc = NULL;
      if (!scanner->parse_errors)
	g_scanner_unexp_token (scanner, token, "identifier", "keyword", NULL, "discarding wave", TRUE);
    }
  else
    {
      if (dsc->wdsc.name)       /* found and parsed the correctly named wave */
	{
          if (dsc->wdsc.n_channels > 2)
            {
              g_scanner_error (scanner, "waves with n-channels > 2 (%d) are not currently supported", dsc->wdsc.n_channels);
              bsewave_wave_dsc_free (dsc);
              dsc = NULL;
            }
	}
      else
	{
	  /* got invalid/wrong wave */
	  bsewave_wave_dsc_free (dsc);
	  dsc = NULL;
        }
    }
  g_scanner_destroy (scanner);
  close (fd);
  
  return dsc ? &dsc->wdsc : NULL;
}

static void
bsewave_free_wave_dsc (gpointer    data,
		       GslWaveDsc *wave_dsc)
{
  WaveDsc *dsc = (WaveDsc*) wave_dsc;
  
  bsewave_wave_dsc_free (dsc);
}

static GslDataHandle*
bsewave_load_singlechunk_wave (GslWaveFileInfo *fi,
			       const gchar     *wave_name,
                               gfloat           osc_freq,
			       BseErrorType    *error_p)
{
  GslWaveDsc *wdsc;
  guint i;
  
  if (fi->n_waves == 1 && !wave_name)
    i = 0;
  else if (!wave_name)
    {
      /* don't know which wave to pick */
      *error_p = BSE_ERROR_FORMAT_INVALID;
      return NULL;
    }
  else /* find named wave */
    for (i = 0; i < fi->n_waves; i++)
      if (strcmp (fi->waves[i].name, wave_name) == 0)
	break;
  if (i >= fi->n_waves)
    {
      *error_p = BSE_ERROR_WAVE_NOT_FOUND;
      return NULL;
    }
  
  wdsc = gsl_wave_dsc_load (fi, i, FALSE, error_p);
  if (!wdsc)
    return NULL;
  
  if (wdsc->n_chunks == 1)
    {
      GslDataHandle *dhandle = gsl_wave_handle_create (wdsc, 0, error_p);
      if (dhandle && osc_freq > 0)
        {
          gchar **xinfos = NULL;
          xinfos = bse_xinfos_add_float (xinfos, "osc-freq", osc_freq);
          GslDataHandle *tmp_handle = gsl_data_handle_new_add_xinfos (dhandle, xinfos);
          g_strfreev (xinfos);
          gsl_data_handle_unref (dhandle);
          dhandle = tmp_handle;
        }
      gsl_wave_dsc_free (wdsc);
      return dhandle;
    }
  
  /* this is ridiculous, letting the chunk of a wave
   * point to a wave with multiple chunks...
   */
  gsl_wave_dsc_free (wdsc);
  *error_p = BSE_ERROR_FORMAT_INVALID;
  return NULL;
}

static GslDataHandle*
bsewave_create_chunk_handle (gpointer      data,
			     GslWaveDsc   *wave_dsc,
			     guint         nth_chunk,
			     BseErrorType *error_p)
{
  WaveDsc *dsc = (WaveDsc*) wave_dsc;
  FileInfo *fi = (FileInfo*) dsc->wdsc.file_info;
  GslWaveChunkDsc *chunk = wave_dsc->chunks + nth_chunk;
  
  GslDataHandle *dhandle = NULL;
  switch (LOADER_TYPE (chunk))
    {
      gchar *string;
    case AUTO_FILE_MAGIC:
      /* construct chunk file name from (hopefully) relative path */
      if (g_path_is_absolute (LOADER_FILE (chunk)))
        string = g_strdup (LOADER_FILE (chunk));
      else
        string = g_strdup_printf ("%s%c%s", fi->cwd, G_DIR_SEPARATOR, (char*) LOADER_FILE (chunk));
      /* try to load the chunk via registered loaders */
      GslWaveFileInfo *cfi = gsl_wave_file_info_load (string, error_p);
      if (cfi)
	{
	  /* FIXME: there's a potential attack here, in letting a single chunk
	   * wave's chunk point to its own wave. this'll trigger recursions until
	   * stack overflow
	   */
	  dhandle = bsewave_load_singlechunk_wave (cfi, LOADER_INDEX (chunk), chunk->osc_freq, error_p);
          if (dhandle && chunk->xinfos)
            {
              GslDataHandle *tmp_handle = dhandle;
              dhandle = gsl_data_handle_new_add_xinfos (dhandle, chunk->xinfos);
              gsl_data_handle_unref (tmp_handle);
            }
	  gsl_wave_file_info_unref (cfi);
	}
      g_free (string);
      break;
    case RAW_FILE_MAGIC:
      /* construct chunk file name from (hopefully) relative path */
      if (g_path_is_absolute (LOADER_FILE (chunk)))
        string = g_strdup (LOADER_FILE (chunk));
      else
        string = g_strdup_printf ("%s%c%s", fi->cwd, G_DIR_SEPARATOR, (char*) LOADER_FILE (chunk));
      /* try to load a raw sample */
      dhandle = gsl_wave_handle_new (string,			/* file name */
				     dsc->wdsc.n_channels,
				     LOADER_FORMAT (chunk) ? LOADER_FORMAT (chunk) : dsc->dfl_format,
				     LOADER_BYTE_ORDER (chunk) ? LOADER_BYTE_ORDER (chunk) : dsc->dfl_byte_order,
                                     chunk->mix_freq > 0 ? chunk->mix_freq : dsc->dfl_mix_freq,
                                     chunk->osc_freq,
				     LOADER_BOFFSET (chunk),    /* byte offset */
                                     LOADER_LENGTH (chunk) ? LOADER_LENGTH (chunk) : -1,        /* n_values */
                                     chunk->xinfos);
      *error_p = dhandle ? BSE_ERROR_NONE : BSE_ERROR_IO;
      g_free (string);
      break;
    case RAW_LINK_MAGIC:
      if (LOADER_LENGTH (chunk))        /* inlined binary data */
	{
	  dhandle = gsl_wave_handle_new_zoffset (fi->wfi.file_name,
						 dsc->wdsc.n_channels,
                                                 LOADER_FORMAT (chunk) ? LOADER_FORMAT (chunk) : dsc->dfl_format,
                                                 LOADER_BYTE_ORDER (chunk) ? LOADER_BYTE_ORDER (chunk) : dsc->dfl_byte_order,
                                                 chunk->mix_freq > 0 ? chunk->mix_freq : dsc->dfl_mix_freq,
                                                 chunk->osc_freq,
                                                 LOADER_BOFFSET (chunk),        /* byte offset */
						 LOADER_LENGTH (chunk),         /* byte length */
                                                 chunk->xinfos);
	  *error_p = dhandle ? BSE_ERROR_NONE : BSE_ERROR_IO;
	}
      else
        *error_p = BSE_ERROR_WAVE_NOT_FOUND;
      break;
    case OGG_LINK_MAGIC:
      if (LOADER_LENGTH (chunk))        /* inlined binary data */
	{
	  dhandle = gsl_data_handle_new_ogg_vorbis_zoffset (fi->wfi.file_name,
                                                            chunk->osc_freq,
                                                            LOADER_BOFFSET (chunk),     /* byte offset */
                                                            LOADER_LENGTH (chunk));     /* byte length */
          if (dhandle && chunk->xinfos)
            {
              GslDataHandle *tmp_handle = dhandle;
              dhandle = gsl_data_handle_new_add_xinfos (dhandle, chunk->xinfos);
              gsl_data_handle_unref (tmp_handle);
            }
	  *error_p = dhandle ? BSE_ERROR_NONE : BSE_ERROR_IO;
	}
      else
        *error_p = BSE_ERROR_WAVE_NOT_FOUND;
      break;
    default:    /* no file_name and no loader specified */
      *error_p = BSE_ERROR_FORMAT_UNKNOWN;
      break;
    }
  return dhandle;
}

void
_gsl_init_loader_gslwave (void)
{
  static const gchar *file_exts[] = { "bsewave", NULL, };
  static const gchar *mime_types[] = { "audio/x-bsewave", NULL, };
  static const gchar *magics[] = { "0 string #BseWave", "0 string #GslWave", NULL, };
  static GslLoader loader = {
    "BseWave",
    file_exts,
    mime_types,
    0,	/* flags */
    magics,
    0,  /* priority */
    NULL,
    bsewave_load_file_info,
    bsewave_free_file_info,
    bsewave_load_wave_dsc,
    bsewave_free_wave_dsc,
    bsewave_create_chunk_handle,
  };
  static gboolean initialized = FALSE;
  
  g_assert (initialized == FALSE);
  initialized = TRUE;
  
  gsl_loader_register (&loader);
}
