/* SFI - Synthesis Fusion Kit Interface
 * Copyright (C) 2002 Stefan Westerfeld
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
#include <sfi/glib-extra.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "sfidl-parser.h"
#include "sfidl-namespace.h"
#include "sfidl-options.h"
#include <iostream>

using namespace Sfidl;
using namespace std;

/* --- variables --- */
static  GScannerConfig  scanner_config_template = {
  const_cast<gchar *>   /* FIXME: glib should use const gchar* here */
  (
   " \t\r\n"
   )                    /* cset_skip_characters */,
  const_cast<gchar *>
  (
   G_CSET_a_2_z
   "_"
   G_CSET_A_2_Z
   )                    /* cset_identifier_first */,
  const_cast<gchar *>
  (
   G_CSET_a_2_z
   "_0123456789"
   G_CSET_A_2_Z
   )                    /* cset_identifier_nth */,
  0			/* cpair_comment_single */,
  
  TRUE                  /* case_sensitive */,
  
  TRUE                  /* skip_comment_multi */,
  TRUE                  /* skip_comment_single */,
  TRUE                  /* scan_comment_multi */,
  TRUE                  /* scan_identifier */,
  TRUE                  /* scan_identifier_1char */,
  FALSE                 /* scan_identifier_NULL */,
  TRUE                  /* scan_symbols */,
  FALSE                 /* scan_binary */,
  TRUE                  /* scan_octal */,
  TRUE                  /* scan_float */,
  TRUE                  /* scan_hex */,
  FALSE                 /* scan_hex_dollar */,
  FALSE                 /* scan_string_sq */,
  TRUE                  /* scan_string_dq */,
  TRUE                  /* numbers_2_int */,
  FALSE                 /* int_2_float */,
  FALSE                 /* identifier_2_string */,
  TRUE                  /* char_2_token */,
  TRUE                  /* symbol_2_token */,
  FALSE                 /* scope_0_fallback */,
};

/* --- defines --- */

#define debug(x)         /* nothing */

#define TOKEN_CHOICE     GTokenType(G_TOKEN_LAST + 1)
#define TOKEN_CLASS      GTokenType(G_TOKEN_LAST + 2)
#define TOKEN_CONST      GTokenType(G_TOKEN_LAST + 3)
#define TOKEN_GROUP      GTokenType(G_TOKEN_LAST + 4)
#define TOKEN_INFO       GTokenType(G_TOKEN_LAST + 5)
#define TOKEN_NAMESPACE  GTokenType(G_TOKEN_LAST + 6)
#define TOKEN_PROPERTY   GTokenType(G_TOKEN_LAST + 7)
#define TOKEN_RECORD     GTokenType(G_TOKEN_LAST + 8)
#define TOKEN_SEQUENCE   GTokenType(G_TOKEN_LAST + 9)
#define TOKEN_ISTREAM    GTokenType(G_TOKEN_LAST + 10)
#define TOKEN_JSTREAM    GTokenType(G_TOKEN_LAST + 11)
#define TOKEN_OSTREAM    GTokenType(G_TOKEN_LAST + 12)

#define parse_or_return(token)  G_STMT_START{ \
  GTokenType _t = GTokenType(token); \
  if (g_scanner_get_next_token (scanner) != _t) \
    return _t; \
}G_STMT_END
#define peek_or_return(token)   G_STMT_START{ \
  GScanner *__s = (scanner); GTokenType _t = GTokenType(token); \
  if (g_scanner_peek_next_token (__s) != _t) { \
    g_scanner_get_next_token (__s); /* advance position for error-handler */ \
    return _t; \
  } \
}G_STMT_END
#define parse_string_or_return(str) G_STMT_START{ \
  string& __s = str; /* type safety - assume argument is a std::string */ \
  GTokenType __t, __expected; \
  bool __bracket = (g_scanner_peek_next_token (scanner) == GTokenType('(')); \
  if (__bracket) \
    parse_or_return ('('); \
  __expected = parseStringOrConst (__s); \
  if (__expected != G_TOKEN_NONE) return __expected; \
  __t = g_scanner_peek_next_token (scanner); \
  while (__t == G_TOKEN_STRING || __t == G_TOKEN_IDENTIFIER) { \
    string __snew; \
    __expected = parseStringOrConst (__snew); \
    if (__expected != G_TOKEN_NONE) return __expected; \
    __s += __snew; \
    __t = g_scanner_peek_next_token (scanner); \
  } \
  if (__bracket) \
    parse_or_return (')'); \
}G_STMT_END
#define parse_int_or_return(i) G_STMT_START{ \
  bool negate = (g_scanner_peek_next_token (scanner) == GTokenType('-')); \
  if (negate) \
    g_scanner_get_next_token(scanner); \
  if (g_scanner_get_next_token (scanner) != G_TOKEN_INT) \
    return G_TOKEN_INT; \
  i = scanner->value.v_int; \
  if (negate) i = -i; \
}G_STMT_END
#define parse_float_or_return(f) G_STMT_START{ \
  bool negate = false; \
  GTokenType t = g_scanner_get_next_token (scanner); \
  if (t == GTokenType('-')) \
  { \
    negate = true; \
    t = g_scanner_get_next_token (scanner); \
  } \
  if (t == G_TOKEN_INT) \
    f = scanner->value.v_int; \
  else if (t == G_TOKEN_FLOAT) \
    f = scanner->value.v_float; \
  else \
    return G_TOKEN_FLOAT; \
  if (negate) f = -f; \
}G_STMT_END

/* --- methods --- */

bool Parser::isChoice(const string& type) const
{
  map<string,int>::const_iterator i = typeMap.find (type);
  return (i != typeMap.end()) && ((i->second & (~tdProto)) == tdChoice);
}

bool Parser::isSequence(const string& type) const
{
  map<string,int>::const_iterator i = typeMap.find (type);
  return (i != typeMap.end()) && ((i->second & (~tdProto)) == tdSequence);
}

bool Parser::isRecord(const string& type) const
{
  map<string,int>::const_iterator i = typeMap.find (type);
  return (i != typeMap.end()) && ((i->second & (~tdProto)) == tdRecord);
}

bool Parser::isClass(const string& type) const
{
  map<string,int>::const_iterator i = typeMap.find (type);
  return (i != typeMap.end()) && ((i->second & (~tdProto)) == tdClass);
}

Sequence Parser::findSequence(const string& name) const
{
  vector<Sequence>::const_iterator i;
  
  for(i=sequences.begin();i != sequences.end(); i++)
    if(i->name == name) return *i;
  
  return Sequence();
}

Record Parser::findRecord(const string& name) const
{
  vector<Record>::const_iterator i;
  
  for(i=records.begin();i != records.end(); i++)
    if(i->name == name) return *i;
  
  return Record();
}

Parser::Parser () : options (*Options::the())
{
  scanner = g_scanner_new (&scanner_config_template);
  
  const char *syms[] = {
    "choice",
    "class",
    "Const",
    "group",
    "Info",
    "namespace",
    "property",
    "record",
    "sequence",
    "IStream",
    "JStream",
    "OStream",
    0
  };
  for (int n = 0; syms[n]; n++)
    g_scanner_add_symbol (scanner, syms[n], GUINT_TO_POINTER (G_TOKEN_LAST + 1 + n));
  
  scanner->max_parse_errors = 1;
  scanner->parse_errors = 0;
  scanner->msg_handler = scannerMsgHandler;
  scanner->user_data = this;
}

void Parser::printError(const gchar *format, ...)
{
  va_list args;
  gchar *string;
  
  va_start (args, format);
  string = g_strdup_vprintf (format, args);
  va_end (args);
  
  if (scanner->parse_errors < scanner->max_parse_errors)
    g_scanner_error (scanner, "%s", string);
  
  g_free (string);
}

void Parser::scannerMsgHandler (GScanner *scanner, gchar *message, gboolean is_error)
{
  g_return_if_fail (scanner != NULL);
  g_return_if_fail (scanner->user_data != NULL);

  Parser *parser = static_cast<Parser *>(scanner->user_data);
  if (scanner->line > 0 && parser->scannerLineInfo.size() >= scanner->line)
    {
      const LineInfo& info = parser->scannerLineInfo[scanner->line-1];
      fprintf (stderr, "%s:%d: ", info.filename.c_str(), info.line);
    }
  else
    {
      fprintf (stderr, "%s:%d: ", scanner->input_name, scanner->line);
    }
  if (is_error)
    fprintf (stderr, "error: ");
  fprintf (stderr, "%s\n", message);
}

/* --- preprocessing related functions --- */

static bool match(vector<char>::iterator start, const char *string)
{
  while(*string && *start)
    if(*string++ != *start++) return false;

  return (*string == 0);
}

static bool fileExists(const char *filename)
{
  FILE *test = fopen(filename,"r");
  if(test)
    {
      fclose(test);
      return true;
    }
  return false;
}

static string searchFile(const char *filename, const vector<string>& path)
{
  if(fileExists(filename)) return filename;

  vector<string>::const_iterator i;
  for(i = path.begin(); i != path.end(); i++)
    {
      string location = *i + "/" + filename;
      if(fileExists(location.c_str())) return location;
    }
  fprintf(stderr,"file '%s' not found\n",filename);
  exit(1);
}

static void loadFile(const char *filename, vector<char>& v)
{
  FILE *f = fopen(filename,"r");
  if(!f)
    {
      fprintf(stderr,"file '%s' not found\n",filename);
      exit(1);
    }

  char buffer[1024];
  long l;
  while((l = fread(buffer,1,1024,f)) > 0)
    v.insert(v.end(),buffer, buffer+l);
  fclose(f);
}

bool Parser::haveIncluded (const string& filename) const
{
  vector<string>::const_iterator i;

  for(i = includes.begin();i != includes.end();i++)
    if(*i == filename) return true;

  return false;
}

void Parser::preprocess (const string& input_filename)
{
  string filename;
  enum
    {
      lineStart, idlCode, commentC, commentCxx,
      filenameFind, filenameIn1, filenameIn2
    } state = lineStart;
  static int incdepth = 0;

  LineInfo linfo;
  linfo.line = 1;
  linfo.isInclude = (incdepth != 0);
  linfo.filename = input_filename;

  vector<char> input;
  loadFile (input_filename.c_str(), input);
  input.push_back('\n'); // line number counting assumes files end with a newline

  vector<char>::iterator i = input.begin();
  while(i != input.end())
    {
      if(state != commentCxx && state != commentC && match (i, "//")) // C++ style comment?
	{
	  state = commentCxx;
	  i += 2;
	}
      else if(state != commentCxx && state != commentC && match(i,"/*")) // C style comment
	{
	  state = commentC;
	  i += 2;
	}
      else if(state == commentCxx) // eat C++ style comments
	{
	  if(*i == '\n')
	    {
	      scannerInputData.push_back(*i); // keep line numbering
	      scannerLineInfo.push_back(linfo);
	      linfo.line++;
	      state = lineStart;
	    }
	  i++;
	}
      else if(state == commentC) // eat C style comments
	{
	  if(match(i,"*/")) // leave comment state?
	    {
	      state = idlCode;
	      i += 2;
	    }
	  else // skip comments
	    {
	      if(*i == '\n')
		{
		  scannerInputData.push_back(*i); // keep line numbering
		  scannerLineInfo.push_back(linfo);
		  linfo.line++;
		}
	      i++;
	    }
	}
      else if(state == filenameFind)
	{
	  switch(*i++)
	    {
	    case ' ':	// skip whitespaces
	    case '\t':
	      break;

	    case '"':	state = filenameIn1;
			break;
	    case '<':	state = filenameIn2;
			break;
	    default:	cerr << "bad char after #include statement" << endl;
			g_assert_not_reached (); // error handling!
	    }
	}
      else if((state == filenameIn1 && *i == '"')
	  || (state == filenameIn2 && *i == '>'))
	{
	  if(!haveIncluded(filename))
	    {
	      includes.push_back(filename);

	      string location = searchFile(filename.c_str(), options.includePath);
	      incdepth++;
	      preprocess (location);
	      incdepth--;
	    }

	  state = idlCode;
	  i++;
	}
      else if(state == filenameIn1 || state == filenameIn2)
	{
	  filename += *i++;
	}
      else if(state == lineStart) // check if we're on lineStart
	{
	  if(match(i,"#include"))
	    {
	      i += 8;
	      state = filenameFind;
	      filename = "";
	    }
	  else
	    {
	      if(*i != ' ' && *i != '\t' && *i != '\n') state = idlCode;
	      if(*i == '\n')
		{
		  state = lineStart;	// newline handling
		  scannerLineInfo.push_back(linfo);
		  linfo.line++;
		}
	      scannerInputData.push_back(*i++);
	    }
	}
      else
	{
	  if(*i == '\n')
	    {
	      state = lineStart;	// newline handling
	      scannerLineInfo.push_back(linfo);
	      linfo.line++;
	    }
	  scannerInputData.push_back(*i++);
	}
    }
}

bool Parser::insideInclude () const
{
  int scanner_line = scanner->line - 1;
  g_return_val_if_fail (scanner_line >= 0 && scanner_line < (gint) scannerLineInfo.size(), false);

  return scannerLineInfo[scanner_line].isInclude;
}

/* --- parsing functions --- */

bool Parser::parse (const string& filename)
{
  /* preprocess (throws includes into contents, removes C-style comments) */
  preprocess (filename);
  g_scanner_input_text (scanner, &scannerInputData[0], scannerInputData.size());

  /* define primitive types into the basic namespace */
  ModuleHelper::define("Bool");
  ModuleHelper::define("Int");
  ModuleHelper::define("Num");
  ModuleHelper::define("Real");
  ModuleHelper::define("String");
  ModuleHelper::define("Proxy"); /* FIXME: remove this as soon as "real" interface types exist */
  ModuleHelper::define("BBlock");
  ModuleHelper::define("FBlock");
  ModuleHelper::define("PSpec");
  ModuleHelper::define("Rec");
  
  GTokenType expected_token = G_TOKEN_NONE;
  
  while (!g_scanner_eof (scanner) && expected_token == G_TOKEN_NONE)
    {
      g_scanner_get_next_token (scanner);
      
      if (scanner->token == G_TOKEN_EOF)
        break;
      else if (scanner->token == TOKEN_NAMESPACE)
        expected_token = parseNamespace ();
      else
        expected_token = G_TOKEN_EOF; /* '('; */
    }
  
  if (expected_token != G_TOKEN_NONE)
    {
      g_scanner_unexp_token (scanner, expected_token, NULL, NULL, NULL, NULL, TRUE);
      return false;
    }

  return true;
}

GTokenType Parser::parseNamespace()
{
  debug("parse namespace\n");
  parse_or_return (G_TOKEN_IDENTIFIER);
  ModuleHelper::enter (scanner->value.v_identifier);
  
  parse_or_return (G_TOKEN_LEFT_CURLY);

  bool ready = false;
  do
    {
      switch (g_scanner_peek_next_token (scanner))
	{
	  case TOKEN_CHOICE:
	    {
	      GTokenType expected_token = parseChoice ();
	      if (expected_token != G_TOKEN_NONE)
		return expected_token;
	    }
	    break;
	  case TOKEN_RECORD:
	    {
	      GTokenType expected_token = parseRecord ();
	      if (expected_token != G_TOKEN_NONE)
		return expected_token;
	    }
	    break;
	  case TOKEN_SEQUENCE:
	    {
	      GTokenType expected_token = parseSequence ();
	      if (expected_token != G_TOKEN_NONE)
		return expected_token;
	    }
	    break;
	  case TOKEN_CLASS:
	    {
	      GTokenType expected_token = parseClass ();
	      if (expected_token != G_TOKEN_NONE)
		return expected_token;
	    }
	    break;
	  case G_TOKEN_IDENTIFIER:
	    {
	      Method procedure;
	      GTokenType expected_token = parseMethod (procedure);
	      if (expected_token != G_TOKEN_NONE)
		return expected_token;

	      procedure.name = ModuleHelper::define (procedure.name.c_str());
	      addProcedureTodo (procedure);
	    }
	    break;
	  case TOKEN_CONST:
	    {
	      GTokenType expected_token = parseConstant ();
	      if (expected_token != G_TOKEN_NONE)
		return expected_token;
	    }
	    break;
	  default:
	    ready = true;
	}
    }
  while (!ready);
  
  parse_or_return (G_TOKEN_RIGHT_CURLY);
  parse_or_return (';');
  
  ModuleHelper::leave();
  
  return G_TOKEN_NONE;
}

GTokenType Parser::parseStringOrConst (string &s)
{
  if (g_scanner_peek_next_token (scanner) == G_TOKEN_IDENTIFIER)
    {
      parse_or_return (G_TOKEN_IDENTIFIER);
      s = ModuleHelper::qualify (scanner->value.v_identifier);

      for(vector<Constant>::iterator ci = constants.begin(); ci != constants.end(); ci++)
	{
	  if (ci->name == s)
	    {
	      char *x = 0;
	      switch (ci->type)
		{
		  case Constant::tInt:
		    s = x = g_strdup_printf ("%d", ci->i);
		    g_free (x);
		    break;
		  case Constant::tFloat:
		    s = x = g_strdup_printf ("%f", ci->f);
		    g_free (x);
		    break;
		  case Constant::tString:
		    s = ci->str;
		    break;
		  default:
		    g_assert_not_reached ();
		    break;
		}
	      return G_TOKEN_NONE;
	    }
	}
      printError("undeclared constant %s used", s.c_str());
    }

  parse_or_return (G_TOKEN_STRING);
  s = scanner->value.v_string;
  return G_TOKEN_NONE;
}

GTokenType Parser::parseConstant ()
{
  /*
   * constant BAR = 3;
   */
  Constant cdef;

  parse_or_return (TOKEN_CONST);
  parse_or_return (G_TOKEN_IDENTIFIER);
  cdef.name = ModuleHelper::define (scanner->value.v_identifier);

  parse_or_return ('=');

  GTokenType t = g_scanner_peek_next_token (scanner);

  bool negate = (t == GTokenType('-')); /* negative number? */
  if (negate)
  {
    parse_or_return ('-');
    t = g_scanner_peek_next_token (scanner);
  }

  if (t == G_TOKEN_INT)
  {
    parse_or_return (G_TOKEN_INT);

    cdef.type = Constant::tInt;
    cdef.i = scanner->value.v_int;
    if (negate)
      cdef.i = -cdef.i;
  }
  else if (t == G_TOKEN_FLOAT)
  {
    parse_or_return (G_TOKEN_FLOAT);

    cdef.type = Constant::tFloat;
    cdef.f = scanner->value.v_float;
    if (negate)
      cdef.f = -cdef.f;
  }
  else if (!negate)
  {
    parse_string_or_return (cdef.str);

    cdef.type = Constant::tString;
  }
  else
  {
    return G_TOKEN_FLOAT;
  }
  parse_or_return (';');

  addConstantTodo (cdef);
  return G_TOKEN_NONE;
}

GTokenType Parser::parseChoice ()
{
  Choice choice;
  int value = 0;
  debug("parse choice\n");
  
  parse_or_return (TOKEN_CHOICE);
  parse_or_return (G_TOKEN_IDENTIFIER);
  choice.name = ModuleHelper::define (scanner->value.v_identifier);
  if (g_scanner_peek_next_token (scanner) == GTokenType(';'))
    {
      parse_or_return (';');
      addPrototype (choice.name, tdChoice);
      return G_TOKEN_NONE;
    }
  parse_or_return (G_TOKEN_LEFT_CURLY);
  while (g_scanner_peek_next_token (scanner) == G_TOKEN_IDENTIFIER)
    {
      ChoiceValue comp;
      
      GTokenType expected_token = parseChoiceValue (comp, value);
      if (expected_token != G_TOKEN_NONE)
	return expected_token;
      
      choice.contents.push_back(comp);
    }
  parse_or_return (G_TOKEN_RIGHT_CURLY);
  parse_or_return (';');
  
  addChoiceTodo (choice);
  return G_TOKEN_NONE;
}

GTokenType Parser::parseChoiceValue (ChoiceValue& comp, int& value)
{
  /* MASTER @= (25, "Master Volume"), */
  
  parse_or_return (G_TOKEN_IDENTIFIER);
  comp.name = scanner->value.v_identifier;
  comp.neutral = false;

  /* the hints are optional */
  if (g_scanner_peek_next_token (scanner) == GTokenType('@'))
    {
      g_scanner_get_next_token (scanner);
      
      parse_or_return ('=');
      if (g_scanner_peek_next_token (scanner) == G_TOKEN_IDENTIFIER)
	{
	  g_scanner_get_next_token (scanner);
	  if (strcmp (scanner->value.v_string, "Neutral") == 0)
	    {
	      comp.neutral = true;
	    }
	  else
	    {
	      printError("expected 'Neutral' or nothing as choice value type");
	      return GTokenType('(');
	    }
	}
      parse_or_return ('(');
      parse_or_return (G_TOKEN_INT);
      value = scanner->value.v_int;
      parse_or_return (',');
      parse_string_or_return (comp.text);
      parse_or_return (')');
    }
  
  comp.value = value;
  
  if (g_scanner_peek_next_token (scanner) == GTokenType(','))
    parse_or_return (',');
  else
    peek_or_return ('}');
  
  return G_TOKEN_NONE;
}

GTokenType Parser::parseRecord ()
{
  string group = "";
  Record record;
  debug("parse record\n");
  
  parse_or_return (TOKEN_RECORD);
  parse_or_return (G_TOKEN_IDENTIFIER);
  record.name = ModuleHelper::define (scanner->value.v_identifier);
  if (g_scanner_peek_next_token (scanner) == GTokenType(';'))
    {
      parse_or_return (';');
      addPrototype (record.name, tdRecord);
      return G_TOKEN_NONE;
    }
  parse_or_return (G_TOKEN_LEFT_CURLY);

  bool ready = false;
  do
    {
      switch (g_scanner_peek_next_token (scanner))
	{
	  case TOKEN_GROUP:
	    {
	      parse_or_return (TOKEN_GROUP);
	      parse_or_return (':');
	      parse_string_or_return (group);
	      parse_or_return (';');
	    }
	    break;

	  case G_TOKEN_IDENTIFIER:
	    {
	      Param def;

	      GTokenType expected_token = parseRecordField (def, group);
	      if (expected_token != G_TOKEN_NONE)
		return expected_token;

	      if (def.type != "")
		record.contents.push_back(def);
	    }
	    break;

	  case TOKEN_INFO:
	    {
	      GTokenType expected_token = parseInfoOptional (record.infos);
	      if (expected_token != G_TOKEN_NONE)
		return expected_token;
	    }
	  break;
	  
	default:
	    ready = true;
	  break;
      }
    }
  while (!ready);
  parse_or_return (G_TOKEN_RIGHT_CURLY);
  parse_or_return (';');
  
  addRecordTodo (record);
  return G_TOKEN_NONE;
}

GTokenType Parser::parseRecordField (Param& def, const string& group)
{
  /* FooVolumeType volume_type; */
  /* float         volume_perc @= ("Volume[%]", "Set how loud something is",
     50, 0.0, 100.0, 5.0,
     ":dial:readwrite"); */
  
  parse_or_return (G_TOKEN_IDENTIFIER);
  def.type = ModuleHelper::qualify (scanner->value.v_identifier);
  def.pspec = def.type;
  def.group = group;
  def.line = scanner->line;
  
  parse_or_return (G_TOKEN_IDENTIFIER);
  def.name = scanner->value.v_identifier;
  
  /* the hints are optional */
  if (g_scanner_peek_next_token (scanner) == GTokenType('@'))
    {
      g_scanner_get_next_token (scanner);
      
      parse_or_return ('=');
      
      GTokenType expected_token = parseParamHints (def);
      if (expected_token != G_TOKEN_NONE)
	return expected_token;
    }
  
  parse_or_return (';');
  return G_TOKEN_NONE;
}

GTokenType
Parser::parseStream (Stream&      stream,
                     Stream::Type stype)
{
  /* OStream wave_out @= ("Audio Out", "Wave Output"); */

  stream.type = stype;
  stream.line = scanner->line;

  parse_or_return (G_TOKEN_IDENTIFIER);
  stream.ident = scanner->value.v_identifier;

  parse_or_return ('@');
  parse_or_return ('=');

  parse_or_return ('(');
  parse_string_or_return (stream.name);
  parse_or_return (',');
  parse_string_or_return (stream.blurb);
  parse_or_return (')');

  parse_or_return (';');
  return G_TOKEN_NONE;
}

GTokenType Parser::parseParamHints (Param &def)
{
  if (g_scanner_peek_next_token (scanner) == G_TOKEN_IDENTIFIER)
    {
      parse_or_return (G_TOKEN_IDENTIFIER);
      def.pspec = scanner->value.v_identifier;
    }
  
  parse_or_return ('(');

  int bracelevel = 1;
  string args;
  while (!g_scanner_eof (scanner) && bracelevel > 0)
    {
      GTokenType t = g_scanner_get_next_token (scanner);
      gchar *token_as_string = 0, *x = 0;
      
      if(int(t) > 0 && int(t) <= 255)
	{
	  token_as_string = (char *)calloc(2, 1);
	  token_as_string[0] = char(t);
	}
      switch (t)
	{
	case '(':		  bracelevel++;
	  break;
	case ')':		  bracelevel--;
	  break;
	case G_TOKEN_STRING:	  x = g_strescape (scanner->value.v_string, 0);
				  token_as_string = g_strdup_printf ("\"%s\"", x);
				  g_free (x);
	  break;
	case G_TOKEN_INT:	  token_as_string = g_strdup_printf ("%lu", scanner->value.v_int);
	  break;
	case G_TOKEN_FLOAT:	  token_as_string = g_strdup_printf ("%.17g", scanner->value.v_float);
	  break;
	case G_TOKEN_IDENTIFIER:  token_as_string = g_strdup_printf ("%s", scanner->value.v_identifier);
	  break;
	default:
	  if (!token_as_string)
	    return GTokenType (')');
	}
      if (token_as_string && bracelevel)
	{
	  if (args != "")
	    args += ' ';
	  args += token_as_string;
	  g_free (token_as_string);
	}
    }
  def.args = args;
  return G_TOKEN_NONE;
}

GTokenType Parser::parseInfoOptional (map<string,string>& infos)
{
  /*
   * info HELP = "don't panic";
   * info FOO = "bar";
   */
  while (g_scanner_peek_next_token (scanner) == TOKEN_INFO)
  {
    string key, value;

    parse_or_return (TOKEN_INFO);
    parse_or_return (G_TOKEN_IDENTIFIER);
    key = scanner->value.v_identifier;
    parse_or_return ('=');
    parse_string_or_return (value);
    parse_or_return (';');

    infos[key] = value;
  }
  return G_TOKEN_NONE;
}

GTokenType Parser::parseSequence ()
{
  GTokenType expected_token;
  Sequence sequence;
  string group;

  /*
   * sequence IntSeq {
   *   Int ints @= (...);
   * };
   */
  
  parse_or_return (TOKEN_SEQUENCE);
  parse_or_return (G_TOKEN_IDENTIFIER);
  sequence.name = ModuleHelper::define (scanner->value.v_identifier);
  if (g_scanner_peek_next_token (scanner) == GTokenType(';'))
    {
      parse_or_return (';');
      addPrototype (sequence.name, tdSequence);
      return G_TOKEN_NONE;
    }
  parse_or_return ('{');

  expected_token = parseInfoOptional (sequence.infos);
  if (expected_token != G_TOKEN_NONE)
    return expected_token;

  expected_token = parseRecordField (sequence.content, "");
  if (expected_token != G_TOKEN_NONE)
    return expected_token;

  expected_token = parseInfoOptional (sequence.infos);
  if (expected_token != G_TOKEN_NONE)
    return expected_token;

  parse_or_return ('}');
  parse_or_return (';');
  
  addSequenceTodo (sequence);
  return G_TOKEN_NONE;
}

GTokenType Parser::parseClass ()
{
  Class cdef;
  string group;
  debug("parse class\n");
  
  parse_or_return (TOKEN_CLASS);
  parse_or_return (G_TOKEN_IDENTIFIER);
  cdef.name = ModuleHelper::define (scanner->value.v_identifier);

  if (g_scanner_peek_next_token (scanner) == GTokenType(';'))
    {
      parse_or_return (';');
      addPrototype (cdef.name, tdClass);
      return G_TOKEN_NONE;
    }
  if (g_scanner_peek_next_token (scanner) == GTokenType(':'))
    {
      parse_or_return (':');
      parse_or_return (G_TOKEN_IDENTIFIER);
      cdef.inherits = ModuleHelper::qualify (scanner->value.v_identifier);
    }

  parse_or_return ('{');
  while (g_scanner_peek_next_token (scanner) != G_TOKEN_RIGHT_CURLY)
    {
      switch (g_scanner_peek_next_token (scanner))
      {
	case G_TOKEN_IDENTIFIER:
	  {
	    Method method;
	    GTokenType expected_token = parseMethod (method);
	    if (expected_token != G_TOKEN_NONE)
	      return expected_token;

	    if (method.result.type == "signal")
	      cdef.signals.push_back(method);
	    else
	      cdef.methods.push_back(method);
	  }
	  break;
	case TOKEN_INFO:
	  {
	    GTokenType expected_token = parseInfoOptional (cdef.infos);
	    if (expected_token != G_TOKEN_NONE)
	      return expected_token;
	  }
	  break;
	case TOKEN_PROPERTY:
	  {
	    parse_or_return (TOKEN_PROPERTY);

	    Param property;
	    GTokenType expected_token = parseRecordField (property, "");
	    if (expected_token != G_TOKEN_NONE)
	      return expected_token;

	    cdef.properties.push_back (property);
	  }
	  break;
      case TOKEN_ISTREAM:
      case TOKEN_JSTREAM:
      case TOKEN_OSTREAM:
        {
          Stream::Type stype = Stream::IStream;
          switch ((int) scanner->next_token) {
          case TOKEN_JSTREAM:   stype = Stream::JStream; break;
          case TOKEN_OSTREAM:   stype = Stream::OStream; break;
          }
          g_scanner_get_next_token (scanner); /* eat *Stream */
          Stream stream;
          GTokenType expected_token = parseStream (stream, stype);
          if (expected_token != G_TOKEN_NONE)
            return expected_token;

          switch (stream.type) {
          case Stream::IStream: cdef.istreams.push_back (stream); break;
          case Stream::JStream: cdef.jstreams.push_back (stream); break;
          case Stream::OStream: cdef.ostreams.push_back (stream); break;
          }
        }
        break;
	default:
	  parse_or_return (G_TOKEN_IDENTIFIER); /* will fail; */
      }
    }
  parse_or_return ('}');
  parse_or_return (';');
  
  addClassTodo (cdef);
  return G_TOKEN_NONE;
}

GTokenType Parser::parseMethod (Method& method)
{
  parse_or_return (G_TOKEN_IDENTIFIER);
  if (strcmp (scanner->value.v_identifier, "signal") == 0)
    method.result.type = "signal";
  else if (strcmp (scanner->value.v_identifier, "void") == 0)
    method.result.type = "void";
  else
    {
      method.result.type = ModuleHelper::qualify (scanner->value.v_identifier);
      method.result.name = "result";
    }

  method.result.pspec = method.result.type;

  parse_or_return (G_TOKEN_IDENTIFIER);
  method.name = scanner->value.v_identifier;

  parse_or_return ('(');
  while (g_scanner_peek_next_token (scanner) == G_TOKEN_IDENTIFIER)
    {
      Param def;

      parse_or_return (G_TOKEN_IDENTIFIER);
      def.type = ModuleHelper::qualify (scanner->value.v_identifier);
      def.pspec = def.type;
  
      parse_or_return (G_TOKEN_IDENTIFIER);
      def.name = scanner->value.v_identifier;
      method.params.push_back(def);

      if (g_scanner_peek_next_token (scanner) != GTokenType(')'))
	{
	  parse_or_return (',');
	  peek_or_return (G_TOKEN_IDENTIFIER);
	}
    }
  parse_or_return (')');

  if (g_scanner_peek_next_token (scanner) == GTokenType(';'))
    {
      parse_or_return (';');
      return G_TOKEN_NONE;
    }

  parse_or_return ('{');
  while (g_scanner_peek_next_token (scanner) == G_TOKEN_IDENTIFIER ||
         g_scanner_peek_next_token (scanner) == TOKEN_INFO)
    {
      if (g_scanner_peek_next_token (scanner) == TOKEN_INFO)
	{
	  GTokenType expected_token = parseInfoOptional (method.infos);
	  if (expected_token != G_TOKEN_NONE)
	    return expected_token;
	}
      else
	{
	  Param *pd = 0;

	  parse_or_return (G_TOKEN_IDENTIFIER);
	  string inout = scanner->value.v_identifier;

	  if (inout == "Out")
	  {
	    parse_or_return (G_TOKEN_IDENTIFIER);
	    method.result.name = scanner->value.v_identifier;
	    pd = &method.result;
	  }
	  else if(inout == "In")
	  {
	    parse_or_return (G_TOKEN_IDENTIFIER);
	    for (vector<Param>::iterator pi = method.params.begin(); pi != method.params.end(); pi++)
	    {
	      if (pi->name == scanner->value.v_identifier)
		pd = &*pi;
	    }
	  }
	  else
	  {
	    printError("In or Out expected in method/procedure details");
	    return G_TOKEN_IDENTIFIER;
	  }

	  if (!pd)
	  {
	    printError("can't associate method/procedure parameter details");
	    return G_TOKEN_IDENTIFIER;
	  }

	  pd->line = scanner->line;

	  parse_or_return ('@');
	  parse_or_return ('=');

	  GTokenType expected_token = parseParamHints (*pd);
	  if (expected_token != G_TOKEN_NONE)
	    return expected_token;

	  parse_or_return (';');
	}
    }
  parse_or_return ('}');

  return G_TOKEN_NONE;
}

void Parser::addConstantTodo(const Constant& constant)
{
  constants.push_back(constant);
  
  if (insideInclude ())
    {
      includedNames.push_back (constant.name);
    }
  else
    {
      types.push_back (constant.name);
    }
}

void Parser::addChoiceTodo(const Choice& choice)
{
  choices.push_back(choice);
  
  if (insideInclude ())
    {
      includedNames.push_back (choice.name);
    }
  else
    {
      types.push_back (choice.name);
    }
  addType (choice.name, tdChoice);
}

void Parser::addRecordTodo(const Record& record)
{
  records.push_back(record);
  
  if (insideInclude ())
    {
      includedNames.push_back (record.name);
    }
  else
    {
      types.push_back (record.name);
    }
  addType (record.name, tdRecord);
}

void Parser::addSequenceTodo(const Sequence& sequence)
{
  sequences.push_back(sequence);
  
  if (insideInclude ())
    {
      includedNames.push_back (sequence.name);
    }
  else
    {
      types.push_back (sequence.name);
    }
  addType (sequence.name, tdSequence);
}

void Parser::addClassTodo(const Class& cdef)
{
  classes.push_back(cdef);
  
  if (insideInclude ())
    {
      includedNames.push_back (cdef.name);
    }
  else
    {
      types.push_back (cdef.name);
    }
  addType (cdef.name, tdClass);
}

void Parser::addProcedureTodo(const Method& pdef)
{
  procedures.push_back(pdef);
  
  if (insideInclude ())
    {
      includedNames.push_back (pdef.name);
    }
  else
    {
      types.push_back (pdef.name);
    }
}

bool Parser::fromInclude(const string& type) const
{
  vector<string>::const_iterator ii;

  for (ii = includedNames.begin(); ii != includedNames.end(); ii++)
    if (*ii == type) return true;
  return false;
}

void Parser::addType (const std::string& type, TypeDeclaration typeDecl)
{
  int& m = typeMap[type];

  if (m == 0)
    {
      m = typeDecl;
    }
  else if (m == typeDecl)
    {
      printError ("double definition of '%s' as same type\n", type.c_str());
    }
  else if (m == (typeDecl | tdProto))
    {
      m = typeDecl;
    }
  else
    {
      printError ("double definition of '%s' as different types\n", type.c_str());
    }
}

void Parser::addPrototype (const std::string& type, TypeDeclaration typeDecl)
{
  int& m = typeMap[type];

  if (m == 0)
    {
      m = typeDecl | tdProto;
    }
  else if (m == typeDecl)
    {
      // prototype after full definition is okay
    }
  else if (m == (typeDecl | tdProto))
    {
      // double prototype is okay
    }
  else
    {
      printError ("double definition of '%s' as different types\n", type.c_str());
    }
}

/* vim:set ts=8 sts=2 sw=2: */
