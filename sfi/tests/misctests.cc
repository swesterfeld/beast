// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#undef G_LOG_DOMAIN
#define  G_LOG_DOMAIN __FILE__
// #define TEST_VERBOSE
#include <sfi/testing.hh>
#include <sfi/path.hh>
#include <unistd.h>
#include <string.h>
#include <signal.h>	/* G_BREAKPOINT() */
#include <math.h>

using namespace Bse;

/* provide IDL type initializers */
#define sfidl_pspec_Real(group, name, nick, blurb, dflt, min, max, step, hints)  \
  sfi_pspec_real (name, nick, blurb, dflt, min, max, step, hints)
#define sfidl_pspec_Record(group, name, nick, blurb, hints, fields)            \
  sfi_pspec_rec (name, nick, blurb, fields, hints)
#define sfidl_pspec_Choice(group, name, nick, blurb, default_value, hints, choices) \
  sfi_pspec_choice (name, nick, blurb, default_value, choices, hints)

/* FIXME: small hackery */
#define sfidl_pspec_Rec(group, name, nick, blurb, hints)            \
  sfi_pspec_int (name, nick, blurb, 0, 0, 0, 0, hints)
#define sfidl_pspec_PSpec(group, name, nick, blurb, hints)            \
  sfi_pspec_int (name, nick, blurb, 0, 0, 0, 0, hints)

typedef enum /*< skip >*/
{
  SERIAL_TEST_TYPED = 1,
  SERIAL_TEST_PARAM,
  SERIAL_TEST_PSPEC
} SerialTest;
static SerialTest serial_test_type = SerialTest (0);
static void
serial_pspec_check (GParamSpec *pspec,
		    GScanner   *scanner)
{
  GValue *value = sfi_value_pspec (pspec), rvalue = { 0, };
  GString *s1 = g_string_new (NULL);
  GString *s2 = g_string_new (NULL);
  GTokenType token;
  sfi_value_store_typed (value, s1);
  g_scanner_input_text (scanner, s1->str, s1->len);
  token = sfi_value_parse_typed (&rvalue, scanner);
  if (token != G_TOKEN_NONE)
    {
      printout ("{while parsing pspec \"%s\":\n\t%s\n", pspec->name, s1->str);
      g_scanner_unexp_token (scanner, token, NULL, NULL, NULL,
			     g_strdup_format ("failed to serialize pspec \"%s\"", pspec->name), TRUE);
    }
  TASSERT (token == G_TOKEN_NONE);
  sfi_value_store_typed (&rvalue, s2);
  if (strcmp (s1->str, s2->str))
    printout ("{while comparing pspecs \"%s\":\n\t%s\n\t%s\n", pspec->name, s1->str, s2->str);
  TASSERT (strcmp (s1->str, s2->str) == 0);
  g_value_unset (&rvalue);
  sfi_value_free (value);
  g_string_free (s1, TRUE);
  g_string_free (s2, TRUE);
}
// serialize @a value according to @a pspec, deserialize and assert a matching result
static void
serialize_cmp (GValue     *value,
	       GParamSpec *pspec)
{
  GScanner *scanner = g_scanner_new64 (sfi_storage_scanner_config);
  GString *gstring = g_string_new (NULL);
  GValue rvalue = { 0, };
  GTokenType token;
  gint cmp;
  if (serial_test_type == SERIAL_TEST_PSPEC)
    serial_pspec_check (pspec, scanner);
  else
    {
      // if (pspec && strcmp (pspec->name, "real-max") == 0) G_BREAKPOINT ();
      if (serial_test_type == SERIAL_TEST_TYPED)
	sfi_value_store_typed (value, gstring);
      else /* SERIAL_TEST_PARAM */
	sfi_value_store_param (value, gstring, pspec, 2);
      g_scanner_input_text (scanner, gstring->str, gstring->len);
      if (serial_test_type == SERIAL_TEST_TYPED)
	token = sfi_value_parse_typed (&rvalue, scanner);
      else /* SERIAL_TEST_PARAM */
	{
	  if (g_scanner_get_next_token (scanner) == '(')
	    if (g_scanner_get_next_token (scanner) == G_TOKEN_IDENTIFIER &&
		strcmp (scanner->value.v_identifier, pspec->name) == 0)
	      token = sfi_value_parse_param_rest (&rvalue, scanner, pspec);
	    else
	      token = G_TOKEN_IDENTIFIER;
	  else
	    token = GTokenType ('(');
	}
      if (0)
	printout ("{parsing:%s}", gstring->str);
      if (token != G_TOKEN_NONE)
	{
	  printout ("{while parsing \"%s\":\n\t%s\n", pspec->name, gstring->str);
	  g_scanner_unexp_token (scanner, token, NULL, NULL, NULL,
				 g_strdup_format ("failed to serialize \"%s\"", pspec->name), TRUE);
	}
      TASSERT (token == G_TOKEN_NONE);
      cmp = g_param_values_cmp (pspec, value, &rvalue);
      if (cmp)
	{
	  printout ("{after parsing:\n\t%s\n", gstring->str);
	  printout ("while comparing:\n\t%s\nwith:\n\t%s\nresult:\n\t%d\n",
		   g_strdup_value_contents (value),
		   g_strdup_value_contents (&rvalue),
		   cmp);
	  if (0)
	    {
	      G_BREAKPOINT ();
	      g_value_unset (&rvalue);
	      g_scanner_input_text (scanner, gstring->str, gstring->len);
	      token = sfi_value_parse_typed (&rvalue, scanner);
	    }
	}
      TASSERT (cmp == 0);
      if (0) /* generate testoutput */
	printout ("OK=================(%s)=================:\n%s\n", pspec->name, gstring->str);
    }
  g_scanner_destroy (scanner);
  g_string_free (gstring, TRUE);
  if (G_VALUE_TYPE (&rvalue))
    g_value_unset (&rvalue);
  sfi_value_free (value);
  sfi_pspec_sink (pspec);
}

static void
test_typed_serialization (SerialTest test_type)
{
  static const SfiChoiceValue test_choices[] = {
    { "ozo-foo", "Ozo-foo blurb", },
    { "emptyblurb", "", },
    { "noblurb", NULL, },
  };
  static const SfiChoiceValues choice_values = { G_N_ELEMENTS (test_choices), test_choices };
  SfiRecFields rec_fields = { 0, NULL, };
  GParamSpec *pspec_homo_seq;
  SfiFBlock *fblock;
  SfiBBlock *bblock;
  SfiSeq *seq;
  SfiRec *rec;
  GValue *val;
  gchar str256[257];
  guint i;
  serial_test_type = test_type;
  switch (serial_test_type)
    {
    case SERIAL_TEST_TYPED:	TSTART ("Typed Serialization"); break;
    case SERIAL_TEST_PARAM:	TSTART ("Param Serialization"); break;
    case SERIAL_TEST_PSPEC:	TSTART ("Pspec Serialization"); break;
    }
  serialize_cmp (sfi_value_bool (FALSE),
		 sfi_pspec_bool ("bool-false", NULL, NULL, FALSE, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_bool (TRUE),
		 sfi_pspec_bool ("bool-true", NULL, NULL, FALSE, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_int (SFI_MAXINT),
		 sfi_pspec_int ("int-max", NULL, NULL, 0, SFI_MININT, SFI_MAXINT, 1, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_int (SFI_MININT),
		 sfi_pspec_int ("int-min", NULL, NULL, 0, SFI_MININT, SFI_MAXINT, 1, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_num (SFI_MAXNUM),
		 sfi_pspec_num ("num-max", NULL, NULL, 0, SFI_MINNUM, SFI_MAXNUM, 1, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_num (SFI_MINNUM),
		 sfi_pspec_num ("num-min", NULL, NULL, 0, SFI_MINNUM, SFI_MAXNUM, 1, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_real (SFI_MINREAL),
		 sfi_pspec_real ("real-min", NULL, NULL, 0, -SFI_MAXREAL, SFI_MAXREAL, 1, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_real (SFI_MAXREAL),
		 sfi_pspec_real ("real-max", NULL, NULL, 0, -SFI_MAXREAL, SFI_MAXREAL, 1, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_real (-SFI_MINREAL),
		 sfi_pspec_real ("real-min-neg", NULL, NULL, 0, -SFI_MAXREAL, SFI_MAXREAL, 1, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_real (-SFI_MAXREAL),
		 sfi_pspec_real ("real-max-neg", NULL, NULL, 0, -SFI_MAXREAL, SFI_MAXREAL, 1, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_real (SFI_MININT),
		 sfi_pspec_real ("real-minint", NULL, NULL, 0, -SFI_MAXREAL, SFI_MAXREAL, 1, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_real (SFI_MINNUM),
		 sfi_pspec_real ("real-minnum", NULL, NULL, 0, -SFI_MAXREAL, SFI_MAXREAL, 1, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_note (SFI_MIN_NOTE),
		 sfi_pspec_note ("vnote-min", NULL, NULL, SFI_KAMMER_NOTE, SFI_MIN_NOTE, SFI_MAX_NOTE, TRUE, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_note (SFI_MAX_NOTE),
		 sfi_pspec_note ("vnote-max", NULL, NULL, SFI_KAMMER_NOTE, SFI_MIN_NOTE, SFI_MAX_NOTE, TRUE, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_note (SFI_NOTE_VOID),
		 sfi_pspec_note ("vnote-void", NULL, NULL, SFI_KAMMER_NOTE, SFI_MIN_NOTE, SFI_MAX_NOTE, TRUE, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_note (SFI_MIN_NOTE),
		 sfi_pspec_note ("note-min", NULL, NULL, SFI_KAMMER_NOTE, SFI_MIN_NOTE, SFI_MAX_NOTE, FALSE, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_note (SFI_MAX_NOTE),
		 sfi_pspec_note ("note-max", NULL, NULL, SFI_KAMMER_NOTE, SFI_MIN_NOTE, SFI_MAX_NOTE, FALSE, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_string (NULL),
		 sfi_pspec_string ("string-nil", NULL, NULL, NULL, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_string ("test\"string'with\\character-?\007rubbish\177H"),
		 sfi_pspec_string ("string", NULL, NULL, NULL, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_string (""),
		 sfi_pspec_string ("string-empty", NULL, NULL, NULL, SFI_PARAM_STANDARD));
  for (i = 0; i < 255; i++)
    str256[i] = i + 1;
  str256[i] = 0;
  serialize_cmp (sfi_value_string (str256),
		 sfi_pspec_string ("string-255", NULL, NULL, NULL, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_choice (NULL),
		 sfi_pspec_choice ("choice-nil", NULL, NULL, NULL, choice_values, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_choice ("test-choice-with-valid-characters_9876543210"),
		 sfi_pspec_choice ("choice", NULL, NULL, NULL, choice_values, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_proxy (SFI_MAXINT),
		 sfi_pspec_proxy ("proxy-max", NULL, NULL, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_proxy (G_MAXUINT),
		 sfi_pspec_proxy ("proxy-umax", NULL, NULL, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_bblock (NULL),
		 sfi_pspec_bblock ("bblock-nil", NULL, NULL, SFI_PARAM_STANDARD));
  bblock = sfi_bblock_new ();
  serialize_cmp (sfi_value_bblock (bblock),
		 sfi_pspec_bblock ("bblock-empty", NULL, NULL, SFI_PARAM_STANDARD));
  for (i = 0; i < 256; i++)
    sfi_bblock_append1 (bblock, i);
  serialize_cmp (sfi_value_bblock (bblock),
		 sfi_pspec_bblock ("bblock", NULL, NULL, SFI_PARAM_STANDARD));
  sfi_bblock_unref (bblock);
  serialize_cmp (sfi_value_fblock (NULL),
		 sfi_pspec_fblock ("fblock-nil", NULL, NULL, SFI_PARAM_STANDARD));
  fblock = sfi_fblock_new ();
  serialize_cmp (sfi_value_fblock (fblock),
		 sfi_pspec_fblock ("fblock-empty", NULL, NULL, SFI_PARAM_STANDARD));
  sfi_fblock_append1 (fblock, -G_MINFLOAT);
  sfi_fblock_append1 (fblock, G_MINFLOAT);
  sfi_fblock_append1 (fblock, -G_MAXFLOAT);
  sfi_fblock_append1 (fblock, G_MAXFLOAT);
  sfi_fblock_append1 (fblock, SFI_MININT);
  sfi_fblock_append1 (fblock, -SFI_MAXINT);
  sfi_fblock_append1 (fblock, SFI_MAXINT);
  sfi_fblock_append1 (fblock, SFI_MINNUM);
  sfi_fblock_append1 (fblock, -SFI_MAXNUM);
  sfi_fblock_append1 (fblock, SFI_MAXNUM);
  serialize_cmp (sfi_value_fblock (fblock),
		 sfi_pspec_fblock ("fblock", NULL, NULL, SFI_PARAM_STANDARD));
  serialize_cmp (sfi_value_seq (NULL),
		 sfi_pspec_seq ("seq-nil", NULL, NULL, NULL, SFI_PARAM_STANDARD));
  seq = sfi_seq_new ();
  serialize_cmp (sfi_value_seq (seq),
		 sfi_pspec_seq ("seq-empty", NULL, NULL, NULL, SFI_PARAM_STANDARD));
  val = sfi_value_bool (TRUE);
  sfi_seq_append (seq, val);
  sfi_value_free (val);
  val = sfi_value_int (42);
  sfi_seq_append (seq, val);
  sfi_value_free (val);
  val = sfi_value_string ("huhu");
  sfi_seq_append (seq, val);
  sfi_value_free (val);
  val = sfi_value_fblock (fblock);
  sfi_seq_append (seq, val);
  sfi_value_free (val);
  if (serial_test_type != SERIAL_TEST_PARAM)
    serialize_cmp (sfi_value_seq (seq),
		   sfi_pspec_seq ("seq-heterogeneous", NULL, NULL,
				  sfi_pspec_real ("dummy", NULL, NULL,
						  0, -SFI_MAXREAL, SFI_MAXREAL, 1, SFI_PARAM_STANDARD),
				  SFI_PARAM_STANDARD));
  sfi_seq_clear (seq);
  for (i = 0; i < 12; i++)
    {
      val = sfi_value_int (2000 - 3 * i);
      sfi_seq_append (seq, val);
      sfi_value_free (val);
    }
  pspec_homo_seq = sfi_pspec_seq ("seq-homogeneous", NULL, NULL,
				  sfi_pspec_int ("integer", NULL, NULL,
						 1500, 1000, 2000, 1, SFI_PARAM_STANDARD),
				  SFI_PARAM_STANDARD);
  sfi_pspec_ref (pspec_homo_seq);
  serialize_cmp (sfi_value_seq (seq),
		 sfi_pspec_seq ("seq-homogeneous", NULL, NULL,
				sfi_pspec_int ("integer", NULL, NULL,
					       1500, 1000, 2000, 1, SFI_PARAM_STANDARD),
				SFI_PARAM_STANDARD));
  if (serial_test_type == SERIAL_TEST_PSPEC)
    {
      serialize_cmp (sfi_value_pspec (NULL),
		     sfi_pspec_pspec ("pspec-nil", NULL, NULL, SFI_PARAM_STANDARD));
      serialize_cmp (sfi_value_pspec (pspec_homo_seq),
		     sfi_pspec_pspec ("pspec-hseq", NULL, NULL, SFI_PARAM_STANDARD));
    }
  serialize_cmp (sfi_value_rec (NULL),
		 sfi_pspec_rec ("rec-nil", NULL, NULL, rec_fields, SFI_PARAM_STANDARD));
  rec = sfi_rec_new ();
  serialize_cmp (sfi_value_rec (rec),
		 sfi_pspec_rec ("rec-empty", NULL, NULL, rec_fields, SFI_PARAM_STANDARD));
  val = sfi_value_string ("huhu");
  sfi_rec_set (rec, "exo-string", val);
  if (serial_test_type != SERIAL_TEST_PARAM)
    sfi_rec_set (rec, "exo-string2", val);
  sfi_value_free (val);
  val = sfi_value_seq (seq);
  sfi_rec_set (rec, "seq-homogeneous", val);
  sfi_value_free (val);
  val = sfi_value_proxy (102);
  sfi_rec_set (rec, "baz-proxy", val);
  sfi_value_free (val);
  rec_fields.fields = g_new (GParamSpec*, 20); /* should be static mem */
  rec_fields.fields[rec_fields.n_fields++] = sfi_pspec_proxy ("baz-proxy", NULL, NULL, SFI_PARAM_STANDARD);
  rec_fields.fields[rec_fields.n_fields++] = sfi_pspec_string ("exo-string", NULL, NULL, NULL, SFI_PARAM_STANDARD);
  rec_fields.fields[rec_fields.n_fields++] = pspec_homo_seq;
  serialize_cmp (sfi_value_rec (rec),
		 sfi_pspec_rec ("rec", NULL, NULL, rec_fields, SFI_PARAM_STANDARD));
  sfi_fblock_unref (fblock);
  sfi_seq_unref (seq);
  sfi_pspec_unref (pspec_homo_seq);
  sfi_rec_unref (rec);
  TDONE ();
}


static void
test_renames (void)
{
  gchar *str;
  str = g_type_name_to_cname ("PrefixTypeName");
  TASSERT (strcmp (str, "prefix_type_name") == 0);
  g_free (str);
  str = g_type_name_to_sname ("PrefixTypeName");
  TASSERT (strcmp (str, "prefix-type-name") == 0);
  g_free (str);
  str = g_type_name_to_cupper ("PrefixTypeName");
  TASSERT (strcmp (str, "PREFIX_TYPE_NAME") == 0);
  g_free (str);
  str = g_type_name_to_type_macro ("PrefixTypeName");
  TASSERT (strcmp (str, "PREFIX_TYPE_TYPE_NAME") == 0);
  g_free (str);
  str = g_type_name_to_sname ("prefix_type_name");
  TASSERT (strcmp (str, "prefix-type-name") == 0);
  g_free (str);
  str = g_type_name_to_cname ("prefix-type-name");
  TASSERT (strcmp (str, "prefix_type_name") == 0);
  g_free (str);
}
TEST_ADD (test_renames);

static gboolean vmarshal_switch = TRUE;
static guint    vmarshal_count = 0;
static void
generate_vmarshal (guint sig)
{
  gchar *s, signame[32 * 4 + 1];
  guint i;
  vmarshal_count++;
  s = signame + sizeof (signame);
  *--s = 0;
  for (i = sig; i; i >>= 2)
    *--s = '0' + (i & 3);
  if (!vmarshal_switch)
    {
      printout ("static void /* %u */\nsfi_vmarshal_%s (gpointer func, gpointer arg0, Arg *alist)\n{\n",
	       vmarshal_count, s);
      printout ("  void (*f) (gpointer");
      for (i = 0; s[i]; i++)
	switch (s[i] - '0')
	  {
	  case 1:	printout (", guint32");		break;
	  case 2:	printout (", guint64");		break;
	  case 3:	printout (", double");		break;
	  }
      printout (", gpointer) = func;\n");
      printout ("  f (arg0");
      for (i = 0; s[i]; i++)
	switch (s[i] - '0')
	  {
	  case 1:	printout (", alist[%u].v32", i);		break;
	  case 2:	printout (", alist[%u].v64", i);		break;
	  case 3:	printout (", alist[%u].vdbl", i);	break;
	  }
      printout (", alist[%u].vpt);\n}\n", i);
    }
  else
    printout ("    case 0x%03x: return sfi_vmarshal_%s; /* %u */\n", sig, s, vmarshal_count);
}

static void
generate_vmarshal_loop (void)
{
  guint sig, i, ki[SFI_VMARSHAL_MAX_ARGS + 1];
  vmarshal_count = 0;
  /* initialize digits */
  for (i = 0; i < SFI_VMARSHAL_MAX_ARGS; i++)
    ki[i] = 1;
  /* initialize overflow */
  ki[SFI_VMARSHAL_MAX_ARGS] = 0;
  while (ki[SFI_VMARSHAL_MAX_ARGS] == 0)	/* abort on overflow */
    {
      /* construct signature */
      sig = 0;
      for (i = SFI_VMARSHAL_MAX_ARGS; i > 0; i--)
	{
	  sig <<= 2;
	  sig |= ki[i - 1];
	}
      /* generate */
      generate_vmarshal (sig);
      /* increase digit wise: 1, 2, 3, 11, 12, 13, 21, 22, 23, 31, ... */
      for (i = 0; ; i++)
	{
	  if (++ki[i] <= 3)
	    break;
	  ki[i] = 1;
	}
    }
}

static void
generate_vmarshal_code (void)
{
  vmarshal_switch = FALSE;
  generate_vmarshal_loop ();
  vmarshal_switch = TRUE;
  printout ("static VMarshal\nsfi_vmarshal_switch (guint sig)\n{\n");
  printout ("  switch (sig)\n    {\n");
  generate_vmarshal_loop ();
  printout ("    default: assert_unreached (); return NULL;\n");
  printout ("    }\n}\n");
}
static const char *pointer1 = "huhu";
static const char *pointer2 = "haha";
static const char *pointer3 = "zoot";
static void
test_vmarshal_func4 (gpointer o,
		     SfiReal  r,
		     SfiNum   n,
		     gpointer data)
{
  TASSERT (o == pointer1);
  TASSERT (r == -426.9112e-267);
  TASSERT (n == -2598768763298128732LL);
  TASSERT (data == pointer3);
}
static void
test_vmarshal_func7 (gpointer o,
		     SfiReal  r,
		     SfiNum   n,
		     SfiProxy p,
		     SfiInt   i,
		     SfiNum   self,
		     gpointer data)
{
  TASSERT (o == pointer1);
  TASSERT (r == -426.9112e-267);
  TASSERT (n == -2598768763298128732LL);
  TASSERT (p == (SfiProxy) pointer2);
  TASSERT (i == -2134567);
  TASSERT (self == (long) test_vmarshal_func7);
  TASSERT (data == pointer3);
}
static void
test_vmarshals (void)
{
  SfiSeq *seq = sfi_seq_new ();
  TSTART ("Vmarshals");
  sfi_seq_append_real (seq, -426.9112e-267);
  sfi_seq_append_num (seq, -2598768763298128732LL);
  sfi_vmarshal_void ((void*) test_vmarshal_func4, (void*) pointer1, seq->n_elements, seq->elements, (void*) pointer3);
  sfi_seq_append_proxy (seq, (SfiProxy) pointer2);
  sfi_seq_append_int (seq, -2134567);
  sfi_seq_append_num (seq, (long) test_vmarshal_func7);
  sfi_vmarshal_void ((void*) test_vmarshal_func7, (void*) pointer1, seq->n_elements, seq->elements, (void*) pointer3);
  TDONE ();
  sfi_seq_unref (seq);
}
TEST_ADD (test_vmarshals);

#include "../private.hh"

int
main (int   argc,
      char *argv[])
{
  Bse::Test::init (&argc, argv);

  if (0)
    {
      generate_vmarshal_code ();
      return 0;
    }

  if (0 != Bse::Test::run())
    return -1;

  test_typed_serialization (SERIAL_TEST_PARAM);
  test_typed_serialization (SERIAL_TEST_TYPED);
  test_typed_serialization (SERIAL_TEST_PSPEC);

  return 0;
}
