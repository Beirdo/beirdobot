/* nXml - Copyright (C) 2005-2006 bakunin - Andrea Marchesini 
 *                                    <bakunin@autistici.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#else
# error Use configure; make; make install
#endif

#include "nxml.h"
#include "nxml_internal.h"

static nxml_error_t __nxml_dtd_valid_node (nxml_t * nxml, nxml_data_t * data);

static nxml_error_t
__nxml_dtd_parse_externalID (nxml_t * nxml, char **buffer, size_t * size,
			     char **system, char **pubid)
{
  char *system_literal = NULL, *pubid_literal = NULL;

  if (!strncmp (*buffer, "SYSTEM", 6))
    {
      (*buffer) += 6;
      (*size) -= 6;

      __nxml_escape_spaces (nxml, buffer, size);

      if (!(system_literal = __nxml_get_value (nxml, buffer, size)))
	{
	  if (nxml->priv.func)
	    nxml->priv.
	      func
	      ("%s: expected the SystemLiteral element after SYSTEM (line %d).\n",
	       nxml->file ? nxml->file : "", nxml->priv.line);

	  return NXML_ERR_PARSER;
	}
    }

  else if (!strncmp (*buffer, "PUBLIC", 6))
    {
      (*buffer) += 6;
      (*size) -= 6;

      __nxml_escape_spaces (nxml, buffer, size);

      if (!(pubid_literal = __nxml_get_value (nxml, buffer, size)))
	{
	  if (nxml->priv.func)
	    nxml->priv.
	      func
	      ("%s: expected the PubidLiteral element after PUBLIC (line %d).\n",
	       nxml->file ? nxml->file : "", nxml->priv.line);

	  return NXML_ERR_PARSER;
	}

      __nxml_escape_spaces (nxml, buffer, size);

      if (!(system_literal = __nxml_get_value (nxml, buffer, size)))
	{
	  if (nxml->priv.func)
	    nxml->priv.
	      func
	      ("%s: expected the SystemLiteral element after SYSTEM (line %d).\n",
	       nxml->file ? nxml->file : "", nxml->priv.line);

	  return NXML_ERR_PARSER;
	}
    }

  else
    {
      if (nxml->priv.func)
	nxml->priv.func ("%s: Expected PUBLIC or SYSTEM (line %d).\n",
			 nxml->file ? nxml->file : "", nxml->priv.line);

      return NXML_ERR_PARSER;
    }

  *system = system_literal;
  *pubid = pubid_literal;

  return NXML_OK;
}

static nxml_error_t
__nxml_dtd_parse_get_end (nxml_t * doc, char **buffer, size_t * size)
{
  /* ...> <- */
  if (**buffer != '>')
    {
      if (doc->priv.func)
	doc->priv.func ("%s: expected '>' as close of dtd item (line %d)\n",
			doc->file ? doc->file : "", doc->priv.line);

      return NXML_ERR_PARSER;
    }

  (*size)--;
  (*buffer)++;

  return NXML_OK;
}

static nxml_error_t
__nxml_dtd_parse_get_name (nxml_t * nxml, char **buffer, size_t * size,
			   char *name, size_t size_name)
{
  int64_t ch;
  int byte;
  int i;

  /* S <- */
  while ((**buffer == 0x20 || **buffer == 0x9 || **buffer == 0xa
	  || **buffer == 0xd) && *size > 0)
    {
      if (**buffer == 0xa && nxml->priv.func)
	nxml->priv.line++;
      (*buffer)++;
      (*size)--;
    }

  /* S Name <- */
  if (!__NXML_NAMESTARTCHARS)
    {
      if (nxml->priv.func)
	nxml->priv.func ("%s: abnormal char '%c' (line %d)\n",
			 nxml->file ? nxml->file : "", **buffer,
			 nxml->priv.line);
      return NXML_ERR_PARSER;
    }

  memcpy (&name[0], *buffer, byte);

  i = byte;
  (*buffer) += byte;
  (*size) -= byte;

  while (__NXML_NAMECHARS && *size && i < size_name - 1)
    {
      memcpy (&name[i], *buffer, byte);

      i += byte;
      (*buffer) += byte;
      (*size) -= byte;
    }

  name[i] = 0;

  /* S Name S <- */
  if (**buffer != 0x20 && **buffer != 0x9 && **buffer != 0xa
      && **buffer != 0xd)
    {
      if (nxml->priv.func)
	nxml->priv.func ("%s: abnormal char '%c' (line %d)\n",
			 nxml->file ? nxml->file : "", **buffer,
			 nxml->priv.line);
      return NXML_ERR_PARSER;
    }

  __nxml_escape_spaces (nxml, buffer, size);

  return NXML_OK;
}

static nxml_error_t
__nxml_dtd_parse_get_block (nxml_t * nxml, char **buffer, size_t * size,
			    char **ret)
{
  int i = 0;
  char *str;

  while (*(*buffer + i) != '>' && *size > i)
    {
      if (*(*buffer + i) == 0xa && nxml->priv.func)
	nxml->priv.line++;

      i++;
    }


  if (!(str = (char *) malloc (sizeof (char) * (i + 1))))
    return NXML_ERR_POSIX;

  memcpy (str, *buffer, i);
  str[i] = 0;
  (*buffer) += i;
  (*size) -= i;

  *ret = str;

  return NXML_OK;
}

static void
__nxml_dtd_parse_free_element_content (__nxml_doctype_element_content_t *
				       content)
{
  __nxml_doctype_element_content_t *tmp, *next;
  if (!content)
    return;

  tmp = content;
  while (tmp)
    {
      next = tmp->next;

      if (tmp->name)
	free (tmp->name);

      if (tmp->list)
	__nxml_dtd_parse_free_element_content (tmp->list);

      free (tmp);

      tmp = next;
    }
}

static void
__nxml_dtd_parse_free_attribute_attdef (__nxml_doctype_attribute_attdef_t *
					attdef)
{
  __nxml_doctype_attribute_attdef_t *tmp, *next;
  __nxml_doctype_attribute_attdef_list_t *list;

  if (!attdef)
    return;

  tmp = attdef;
  while (tmp)
    {
      next = tmp->next;

      if (tmp->name)
	free (tmp->name);

      if (tmp->fixed_value)
	free (tmp->fixed_value);

      while (tmp->list)
	{
	  list = tmp->list->next;

	  free (tmp->list->value);
	  free (tmp->list);

	  tmp->list = list;
	}

      free (tmp);

      tmp = next;
    }
}

/* Clear the memory */
static void
__nxml_dtd_clear (nxml_t * data)
{
  __nxml_doctype_element_t *e, *old_e;
  __nxml_doctype_attribute_t *a, *old_a;
  __nxml_doctype_entity_t *t, *old_t;
  __nxml_doctype_notation_t *n, *old_n;

  e = data->priv.elements;
  while (e)
    {
      old_e = e;

      if (e->name)
	free (e->name);

      if (e->content)
	__nxml_dtd_parse_free_element_content (e->content);

      e = e->next;
      free (old_e);
    }

  a = data->priv.attributes;
  while (a)
    {
      old_a = a;

      if (a->element)
	free (a->element);

      if (a->attdef)
	__nxml_dtd_parse_free_attribute_attdef (a->attdef);

      a = a->next;
      free (old_a);
    }

  t = data->priv.entities;
  while (t)
    {
      old_t = t;

      if (t->name)
	free (t->name);

      if (t->reference)
	free (t->reference);

      if (t->system)
	free (t->system);

      if (t->pubid)
	free (t->pubid);

      if (t->ndata)
	free (t->ndata);

      t = t->next;
      free (old_t);
    }

  n = data->priv.notations;
  while (n)
    {
      old_n = n;

      if (n->system_literal)
	free (n->system_literal);

      n = n->next;
      free (old_n);
    }

  data->priv.elements = NULL;
  data->priv.attributes = NULL;
  data->priv.entities = NULL;
  data->priv.notations = NULL;
}

/* This function parses something like file or like a URL */
static nxml_error_t
__nxml_dtd_parse_something (nxml_t * doc, char *system_literal, int flag)
{
  struct stat st;

  if (!stat (system_literal, &st))
    return nxml_dtd_parse_file (doc, system_literal, flag);

  else if ((flag & NXML_DOCTYPEFLAG_DOWNLOAD))
    return nxml_dtd_parse_url (doc, system_literal, flag);

  return NXML_OK;
}

/* I ignore the PI element... */
static nxml_error_t
__nxml_dtd_parse_pi (nxml_t * doc, char **buffer, size_t * size)
{
  /*
   * Rule [16] - PI ::= '<?' PITarget (S (Char * - (Char * '?>' Char *)))?
   *                    '?>'
   * Rule [17] - PITarget ::= Name - (('X' | 'x') ('M' | 'm') ('L' | 'l'))
   */

  int i = 0;

  if (!*size)
    return NXML_OK;

  while (strncmp (*buffer + i, "?>", 2) && i < *size)
    {
      if (*(*buffer + i) == 0xa && doc->priv.func)
	doc->priv.line++;
      i++;
    }

  if (strncmp (*buffer + i, "?>", 2))
    {
      if (doc->priv.func)
	doc->priv.func ("%s: expected '?' as close pi tag (line %d)\n",
			doc->file ? doc->file : "", doc->priv.line);

      return NXML_ERR_PARSER;
    }

  /* ... but I check if xml pi name is used. */
  if (!strncasecmp (*buffer, "?xml", 4))
    {
      if (doc->priv.func)
	doc->priv.func ("%s: pi xml is reserved! (line %d)\n",
			doc->file ? doc->file : "", doc->priv.line);

      return NXML_ERR_PARSER;
    }

  (*buffer) += i + 3;
  (*size) -= i + 3;

  return NXML_OK;
}

/* I ignore the comment. */
static nxml_error_t
__nxml_dtd_parse_comment (nxml_t * doc, char **buffer, size_t * size)
{
  /*
   * Rule [15] - Comment ::= '<!--' ((Char - '-') | ('-' (Char - '-')))* '-->'
   */

  int i = 0;

  while (strncmp (*buffer + i, "-->", 3) && i < *size)
    {
      if (*(*buffer + i) == 0xa && doc->priv.func)
	doc->priv.line++;
      i++;
    }

  if (strncmp (*buffer + i, "-->", 3))
    {
      if (doc->priv.func)
	doc->priv.func ("%s: expected '--' as close comment (line %d)\n",
			doc->file ? doc->file : "", doc->priv.line);

      return NXML_ERR_PARSER;
    }

  (*buffer) += i + 3;
  (*size) -= i + 3;

  return NXML_OK;
}

/* I must check the notation because I must download after... */
static nxml_error_t
__nxml_dtd_parse_notation (nxml_t * nxml, char **buffer, size_t * size)
{
  char name[1024];
  char *system_literal = NULL;
  char *pubid_literal = NULL;
  __nxml_doctype_notation_t *notation;
  nxml_error_t ret;

  /* !NOTATION S <- */
  if (**buffer != 0x20 && **buffer != 0x9 && **buffer != 0xa
      && **buffer != 0xd)
    {
      if (nxml->priv.func)
	nxml->priv.func ("%s: abnormal char '%c' (line %d)\n",
			 nxml->file ? nxml->file : "", **buffer,
			 nxml->priv.line);
      return NXML_ERR_PARSER;
    }

  if ((ret =
       __nxml_dtd_parse_get_name (nxml, buffer, size, name,
				  sizeof (name))) != NXML_OK)
    return ret;

  if ((ret =
       __nxml_dtd_parse_externalID (nxml, buffer, size, &system_literal,
				    &pubid_literal)) != NXML_OK)
    return ret;

  if ((ret = __nxml_dtd_parse_get_end (nxml, buffer, size)) != NXML_OK)
    return ret;

  /* I insert the new notation inside of the list. */
  if (!
      (notation =
       (__nxml_doctype_notation_t *)
       malloc (sizeof (__nxml_doctype_notation_t))))
    return NXML_ERR_POSIX;

  memset (notation, 0, sizeof (__nxml_doctype_notation_t));

  if (system_literal)
    {
      if (!(notation->system_literal = strdup (system_literal)))
	{
	  free (notation);
	  return NXML_ERR_POSIX;
	}
    }

  if (pubid_literal)
    {
      if (!(notation->pubid_literal = __nxml_trim (pubid_literal)))
	{
	  if (notation->system_literal)
	    free (notation->system_literal);
	  free (notation);
	  return NXML_ERR_POSIX;
	}
    }

  notation->next = nxml->priv.notations;
  nxml->priv.notations = notation;

  if (system_literal)
    free (system_literal);

  if (pubid_literal)
    free (pubid_literal);

  return NXML_OK;
}

static __nxml_doctype_element_content_t *
__nxml_dtd_parse_content (nxml_t * doc, char *str)
{
  __nxml_doctype_element_content_t *ret, *last, *old;

  /*
   * Rule [47] - children ::= (choice | seq) ('?' | '*' | '+')?
   * Rule [48] - cp ::= (Name | choice | seq) ('?' | '*' | '+')?
   * Rule [49] - choice ::= '(' S? cp ( S? '|' S? cp )+ S? ')'
   * Rule [50] - seq::= '(' S? cp ( S? ',' S? cp )* S? ')'
   */

  ret = last = old = NULL;

  if (*str != '(')
    {
      if (doc->priv.func)
	doc->priv.
	  func
	  ("%s: expected '(' for a Mixed o Children DTD element format (line %d)\n",
	   doc->file ? doc->file : "", doc->priv.line);

      return NULL;
    }

  str++;

  while (*str && *str != ')')
    {
      while (*str && (*str == 0xa || *str == 0x20 || *str == 0x9
		      || *str == 0xd))
	str++;

      if (!*str)
	continue;

      /* PCDATA */
      if (!strncmp (str, "#PCDATA", 7))
	{
	  if (!
	      (ret =
	       (__nxml_doctype_element_content_t *)
	       malloc (sizeof (__nxml_doctype_element_content_t))))
	    {
	      __nxml_dtd_parse_free_element_content (old);
	      return NULL;
	    }

	  memset (ret, 0, sizeof (__nxml_doctype_element_content_t));

	  ret->pcdata = 1;

	  if (!last)
	    old = ret;
	  else
	    last->next = ret;

	  last = ret;
	  str += 7;
	}

      /* RECURSIVE ITEM */
      else if (*str == '(')
	{
	  int a;
	  char *other;
	  __nxml_doctype_element_content_t *tmp;

	  a = 0;
	  while (*(str + a) && *(str + a) != ')')
	    a++;

	  if (!*(str + a))
	    {
	      __nxml_dtd_parse_free_element_content (old);
	      return NULL;
	    }

	  a++;

	  if (!(other = (char *) malloc (sizeof (char) * (a + 1))))
	    {
	      __nxml_dtd_parse_free_element_content (old);
	      return NULL;
	    }

	  memcpy (other, str, a);
	  other[a] = 0;

	  tmp = __nxml_dtd_parse_content (doc, other);

	  free (other);

	  if (!tmp)
	    {
	      __nxml_dtd_parse_free_element_content (old);
	      return NULL;
	    }

	  str += a;

	  if (!
	      (ret =
	       (__nxml_doctype_element_content_t *)
	       malloc (sizeof (__nxml_doctype_element_content_t))))
	    {
	      __nxml_dtd_parse_free_element_content (old);
	      return NULL;
	    }

	  memset (ret, 0, sizeof (__nxml_doctype_element_content_t));
	  ret->list = tmp;

	  if (!last)
	    old = ret;
	  else
	    last->next = ret;

	  last = ret;
	}

      /* THE NAME LIST */
      else if (*str != '+' && *str != '?' && *str != '*' && *str != ','
	       && *str != '|')
	{
	  int a = 0;
	  char *name;

	  while (*(str + a) && *(str + a) != 0xa && *(str + a) != 0x20
		 && *(str + a) != 0x9 && *(str + a) != 0xd
		 && *(str + a) != '*' && *(str + a) != '?'
		 && *(str + a) != '+' && *(str + a) != ','
		 && *(str + a) != '|' && *(str + a) != ')')
	    a++;

	  if (!(name = (char *) malloc (sizeof (char) * (a + 1))))
	    {
	      __nxml_dtd_parse_free_element_content (old);
	      return NULL;
	    }

	  memcpy (name, str, a);
	  name[a] = 0;

	  ret = old;
	  while (ret)
	    {
	      if (ret->name && !strcmp (ret->name, name))
		{

		  if (doc->priv.func)
		    doc->priv.
		      func
		      ("%s: The name '%s' exists in the list of choose|seq (line %d)\n",
		       doc->file ? doc->file : "", name, doc->priv.line);

		  free (name);
		  __nxml_dtd_parse_free_element_content (old);
		  return NULL;
		}
	      ret = ret->next;
	    }

	  if (!
	      (ret =
	       (__nxml_doctype_element_content_t *)
	       malloc (sizeof (__nxml_doctype_element_content_t))))
	    {
	      free (name);
	      __nxml_dtd_parse_free_element_content (old);
	      return NULL;
	    }

	  memset (ret, 0, sizeof (__nxml_doctype_element_content_t));
	  ret->name = name;

	  if (!last)
	    old = ret;
	  else
	    last->next = ret;

	  last = ret;

	  str += a;

	  while (*str && (*str == 0xa || *str == 0x20 || *str == 0x9
			  || *str == 0xd))
	    str++;
	}

      else
	{
	  if (!last)
	    {
	      if (doc->priv.func)
		doc->priv.func ("%s: unexpected char '%c' (line %d)\n",
				doc->file ? doc->file : "", *str,
				doc->priv.line);

	      __nxml_dtd_parse_free_element_content (old);
	      return NULL;
	    }

	  switch (*str)
	    {
	    case '+':
	      last->type = NXML_DOCTYPE_ELEMENT_CONTENT_1_OR_MORE;
	      break;

	    case '*':
	      last->type = NXML_DOCTYPE_ELEMENT_CONTENT_0_OR_MORE;
	      break;

	    case '?':
	      last->type = NXML_DOCTYPE_ELEMENT_CONTENT_0_OR_1;
	      break;

	    case '|':
	      last->choose = 1;
	      break;

	    case ',':
	      last->choose = 0;
	      break;

	    default:
	      if (doc->priv.func)
		doc->priv.func ("%s: unexpected char '%c' (line %d)\n",
				doc->file ? doc->file : "", *str,
				doc->priv.line);

	      __nxml_dtd_parse_free_element_content (old);
	      return NULL;
	    }

	  str++;
	}
    }

  if (*str != ')')
    {
      if (doc->priv.func)
	doc->priv.
	  func
	  ("%s: expected ')' for a Mixed o Children DTD element format (line %d)\n",
	   doc->file ? doc->file : "", doc->priv.line);

      __nxml_dtd_parse_free_element_content (old);
      return NULL;
    }

  return old;
}

/* I don't ignore the element: */
static nxml_error_t
__nxml_dtd_parse_element (nxml_t * doc, char **buffer, size_t * size)
{
  char name[1024];
  char *content;
  char *c;
  nxml_error_t ret;
  __nxml_doctype_element_t *element;

  /*
   * Rule [45] - elementdecl ::= '<!ELEMENT' S Name S contentspec S? '>'
   * Rule [46] - contentspec ::= 'EMPTY' | 'ANY' | Mixed | children
   */

  /* !ELEMENT S <- */
  if (**buffer != 0x20 && **buffer != 0x9 && **buffer != 0xa
      && **buffer != 0xd)
    {
      if (doc->priv.func)
	doc->priv.func ("%s: abnormal char '%c' (line %d)\n",
			doc->file ? doc->file : "", **buffer, doc->priv.line);
      return NXML_ERR_PARSER;
    }

  if ((ret =
       __nxml_dtd_parse_get_name (doc, buffer, size, name,
				  sizeof (name))) != NXML_OK)
    return ret;

  /* !ELEMENT S Name S Content <- */
  if ((ret =
       __nxml_dtd_parse_get_block (doc, buffer, size, &content)) != NXML_OK)
    return ret;

  if ((ret = __nxml_dtd_parse_get_end (doc, buffer, size)) != NXML_OK)
    return ret;

  /* I insert the new element inside of the list. */
  if (!
      (element =
       (__nxml_doctype_element_t *)
       malloc (sizeof (__nxml_doctype_element_t))))
    return NXML_ERR_POSIX;

  memset (element, 0, sizeof (__nxml_doctype_element_t));

  if (!(element->name = strdup (name)))
    {
      free (element);
      return NXML_ERR_POSIX;
    }

  if (!(c = __nxml_entity_trim (doc, content)))
    {
      free (content);
      free (element->name);
      free (element);
      return NXML_ERR_POSIX;
    }

  free (content);

  if (!strcmp (c, "ANY"))
    element->type = NXML_DOCTYPE_ELEMENT_ANY;

  else if (!strcmp (c, "EMPTY"))
    element->type = NXML_DOCTYPE_ELEMENT_EMPTY;

  else if (!(element->content = __nxml_dtd_parse_content (doc, c)))
    {
      free (element->name);
      free (element);
      free (c);

      return NXML_ERR_PARSER;
    }

  else
    element->type = NXML_DOCTYPE_ELEMENT_MIXED_OR_CHILDREN;

  free (c);

  element->next = doc->priv.elements;
  doc->priv.elements = element;

  return NXML_OK;
}

static __nxml_doctype_attribute_attdef_t *
__nxml_dtd_parse_attdef (nxml_t * doc, char *str)
{
  int a;
  char *name;
  char type[1024];
  __nxml_doctype_attribute_attdef_t *ret, *last, *old;

  /*
   * Rule [53] - AttDef ::= S Name S AttType S DefaultDecl
   */

  old = ret = last = NULL;

  while (*str)
    {

      /* Name <- */
      a = 0;
      while (*(str + a) && *(str + a) != 0xa && *(str + a) != 0x20
	     && *(str + a) != 0x9 && *(str + a) != 0xd)
	a++;

      if (!(name = (char *) malloc (sizeof (char) * (a + 1))))
	return NULL;

      memcpy (name, str, a);
      name[a] = 0;

      str += a;

      /* Name S <- */
      while (*str
	     && (*str == 0xa || *str == 0x20 || *str == 0x9 || *str == 0xd))
	str++;

      /* Name S AttType <- */
      a = 0;
      while (*str && *str != 0xa && *str != 0x20 && *str != 0x9 && *str != 0xd
	     && *str != '(' && a < sizeof (type))
	{
	  type[a] = *str;
	  str++;
	  a++;
	}

      type[a] = 0;

      if (*str != 0xa && *str != 0x20 && *str != 0x9 && *str != 0xd
	  && *str != '(')
	{
	  if (doc->priv.func)
	    doc->priv.func ("%s: abnormal char '%c' (line %d)\n",
			    doc->file ? doc->file : "", *str, doc->priv.line);
	  __nxml_dtd_parse_free_attribute_attdef (old);
	  free (name);
	  return NULL;
	}


      if (!
	  (ret =
	   (__nxml_doctype_attribute_attdef_t *)
	   malloc (sizeof (__nxml_doctype_attribute_attdef_t))))
	{
	  __nxml_dtd_parse_free_attribute_attdef (old);
	  free (name);
	  return NULL;
	}

      memset (ret, 0, sizeof (__nxml_doctype_attribute_attdef_t));

      if (!old)
	old = ret;
      else
	last->next = ret;

      last = ret;

      ret->name = name;

      /* Name S AttType <- */
      if (!strcmp (type, "CDATA"))
	ret->type = NXML_DOCTYPE_ATTRIBUTE_TYPE_CDATA;

      else if (!strcmp (type, "ID"))
	ret->type = NXML_DOCTYPE_ATTRIBUTE_TYPE_ID;

      else if (!strcmp (type, "IDREF"))
	ret->type = NXML_DOCTYPE_ATTRIBUTE_TYPE_IDREF;

      else if (!strcmp (type, "IDREFS"))
	ret->type = NXML_DOCTYPE_ATTRIBUTE_TYPE_IDREFS;

      else if (!strcmp (type, "ENTITY"))
	ret->type = NXML_DOCTYPE_ATTRIBUTE_TYPE_ENTITY;

      else if (!strcmp (type, "ENTITIES"))
	ret->type = NXML_DOCTYPE_ATTRIBUTE_TYPE_ENTITIES;

      else if (!strcmp (type, "NMTOKEN"))
	ret->type = NXML_DOCTYPE_ATTRIBUTE_TYPE_NMTOKEN;

      else if (!strcmp (type, "NMTOKENS"))
	ret->type = NXML_DOCTYPE_ATTRIBUTE_TYPE_NMTOKENS;

      else if (!strcmp (type, "NOTATION"))
	{
	  __nxml_doctype_attribute_attdef_list_t *list, *list_last = NULL;

	  ret->type = NXML_DOCTYPE_ATTRIBUTE_TYPE_NOTATION;

	  if (*str != 0xa && *str != 0x20 && *str != 0x9 && *str != 0xd)
	    {
	      if (doc->priv.func)
		doc->priv.func ("%s: abnormal char '%c' (line %d)\n",
				doc->file ? doc->file : "", *str,
				doc->priv.line);
	      __nxml_dtd_parse_free_attribute_attdef (old);
	      return NULL;
	    }

	  /* Name S 'NOTATION' S <- */
	  while (*str
		 && (*str == 0xa || *str == 0x20 || *str == 0x9
		     || *str == 0xd))
	    str++;

	  if (*str != '(')
	    {

	      if (doc->priv.func)
		doc->priv.
		  func ("%s: expected char '(' and not '%c' (line %d)\n",
			doc->file ? doc->file : "", *str, doc->priv.line);
	      __nxml_dtd_parse_free_attribute_attdef (old);
	      return NULL;
	    }

	  str++;

	  /* Name S 'NOTATION' S '(' S? <- */
	  while (*str
		 && (*str == 0xa || *str == 0x20 || *str == 0x9
		     || *str == 0xd))
	    str++;

	  while (*str)
	    {
	      /* 
	       * Name S 'NOTATION' S '(' S? Name <-
	       * or
	       * Name S 'NOTATION' S '(' S? Name S? '|' S? Name <-
	       */

	      a = 0;
	      while (*str && *str != 0xa && *str != 0x20 && *str != 0x9
		     && *str != 0xd && *str != '|' && *str != ')'
		     && a < sizeof (type))
		{
		  type[a] = *str;
		  str++;
		  a++;
		}

	      type[a] = 0;

	      if (!
		  (list =
		   (__nxml_doctype_attribute_attdef_list_t *)
		   malloc (sizeof (__nxml_doctype_attribute_attdef_list_t))))
		{
		  __nxml_dtd_parse_free_attribute_attdef (old);
		  return NULL;
		}

	      memset (list, 0,
		      sizeof (__nxml_doctype_attribute_attdef_list_t));

	      if (!(list->value = strdup (type)))
		{
		  __nxml_dtd_parse_free_attribute_attdef (old);
		  return NULL;
		}

	      if (ret->list)
		list_last->next = list;
	      else
		ret->list = list;

	      list_last = list;

	      /* Name S 'NOTATION' S '(' S? Name S? <- */
	      while (*str
		     && (*str == 0xa || *str == 0x20 || *str == 0x9
			 || *str == 0xd))
		str++;

	      if (*str == ')')
		break;

	      if (*str != '|')
		{
		  if (doc->priv.func)
		    doc->priv.
		      func
		      ("%s: expected char ')' or '|' and not '%c' (line %d)\n",
		       doc->file ? doc->file : "", *str, doc->priv.line);
		  __nxml_dtd_parse_free_attribute_attdef (old);
		  return NULL;
		}

	      str++;

	      /* Name S 'NOTATION' S '(' S? Name (S? '|' S? <- */
	      while (*str
		     && (*str == 0xa || *str == 0x20 || *str == 0x9
			 || *str == 0xd))
		str++;

	    }

	  str++;

	}

      /* Enumeration... */
      else if (!*type && *str == '(')
	{
	  __nxml_doctype_attribute_attdef_list_t *list, *list_last = NULL;

	  ret->type = NXML_DOCTYPE_ATTRIBUTE_TYPE_ENUMERATION;

	  str++;

	  /* Name S '(' S? <- */
	  while (*str
		 && (*str == 0xa || *str == 0x20 || *str == 0x9
		     || *str == 0xd))
	    str++;

	  while (*str)
	    {
	      /* Name S '(' <- */
	      a = 0;
	      while (*str && *str != 0xa && *str != 0x20 && *str != 0x9
		     && *str != 0xd && *str != ')' && *str != '|'
		     && a < sizeof (type))
		{
		  type[a] = *str;
		  str++;
		  a++;
		}

	      type[a] = 0;

	      if (!
		  (list =
		   (__nxml_doctype_attribute_attdef_list_t *)
		   malloc (sizeof (__nxml_doctype_attribute_attdef_list_t))))
		{
		  __nxml_dtd_parse_free_attribute_attdef (old);
		  return NULL;
		}

	      memset (list, 0,
		      sizeof (__nxml_doctype_attribute_attdef_list_t));

	      if (!(list->value = strdup (type)))
		{
		  __nxml_dtd_parse_free_attribute_attdef (old);
		  return NULL;
		}

	      if (ret->list)
		list_last->next = list;
	      else
		ret->list = list;

	      list_last = list;

	      /* Name S '(' Nmtoken (S? <- */
	      while (*str
		     && (*str == 0xa || *str == 0x20 || *str == 0x9
			 || *str == 0xd))
		str++;

	      if (*str == ')')
		break;

	      if (*str != '|')
		{
		  if (doc->priv.func)
		    doc->priv.
		      func
		      ("%s: expected char ')' or '|' and not '%c' (line %d)\n",
		       doc->file ? doc->file : "", *str, doc->priv.line);
		  __nxml_dtd_parse_free_attribute_attdef (old);
		  return NULL;
		}

	      str++;

	      /* Name S '(' Nmtoken (S? '|' S? <- */
	      while (*str
		     && (*str == 0xa || *str == 0x20 || *str == 0x9
			 || *str == 0xd))
		str++;

	    }

	  str++;

	}

      else
	{
	  if (doc->priv.func)
	    doc->priv.func ("%s: abnormal char '%c' (line %d)\n",
			    doc->file ? doc->file : "", *type,
			    doc->priv.line);
	  __nxml_dtd_parse_free_attribute_attdef (old);
	  return NULL;
	}

      if (!*str
	  || (*str != 0xa && *str != 0x20 && *str != 0x9 && *str != 0xd))
	{
	  if (doc->priv.func)
	    doc->priv.func ("%s: abnormal char '%c' (line %d)\n",
			    doc->file ? doc->file : "", *str, doc->priv.line);
	  __nxml_dtd_parse_free_attribute_attdef (old);
	  return NULL;
	}

      /* Name S AttType S <- */
      while (*str
	     && (*str == 0xa || *str == 0x20 || *str == 0x9 || *str == 0xd))
	str++;

      /* Name S AttType S DefaultDecl <- */
      a = 0;
      while (*str && *str != 0xa && *str != 0x20 && *str != 0x9 && *str != 0xd
	     && a < sizeof (type))
	{
	  type[a] = *str;
	  str++;
	  a++;
	}

      type[a] = 0;

      if (*str != 0 && *str != 0xa && *str != 0x20 && *str != 0x9
	  && *str != 0xd)
	{
	  if (doc->priv.func)
	    doc->priv.func ("%s: abnormal char '%c' (line %d)\n",
			    doc->file ? doc->file : "", *str, doc->priv.line);
	  __nxml_dtd_parse_free_attribute_attdef (old);
	  return NULL;
	}

      /* Name S AttType S #REQUIRED <- */
      if (!strcmp (type, "#REQUIRED"))
	ret->value = NXML_DOCTYPE_ATTRIBUTE_VALUE_REQUIRED;

      /* Name S AttType S #IMPLIED <- */
      else if (!strcmp (type, "#IMPLIED"))
	ret->value = NXML_DOCTYPE_ATTRIBUTE_VALUE_IMPLIED;

      /* Name S AttType S #FIXED <- */
      else if (!strncmp (type, "#FIXED", 6))
	{
	  ret->value = NXML_DOCTYPE_ATTRIBUTE_VALUE_FIXED;

	  if (*str != 0xa && *str != 0x20 && *str != 0x9 && *str != 0xd)
	    {
	      if (doc->priv.func)
		doc->priv.func ("%s: abnormal char '%c' (line %d)\n",
				doc->file ? doc->file : "", *str,
				doc->priv.line);
	      __nxml_dtd_parse_free_attribute_attdef (old);
	      return NULL;
	    }

	  /* Name S AttType S #FIXED S <- */
	  while (*str
		 && (*str == 0xa || *str == 0x20 || *str == 0x9
		     || *str == 0xd))
	    str++;

	  /* Name S AttType S #FIXED S AttValue <- */
	  a = 0;
	  while (*str && *str != 0xa && *str != 0x20 && *str != 0x9
		 && *str != 0xd && a < sizeof (type))
	    {
	      type[a] = *str;
	      str++;
	      a++;
	    }

	  type[a] = 0;
	  if (!(ret->fixed_value = strdup (type)))
	    {
	      __nxml_dtd_parse_free_attribute_attdef (old);
	      return NULL;
	    }

	}

      /* Name S AttType S AttValue <- */
      else
	{
	  if (!(ret->fixed_value = strdup (type)))
	    {
	      __nxml_dtd_parse_free_attribute_attdef (old);
	      return NULL;
	    }
	}

      /* Name S AttType S DefaultDecl S <- */
      while (*str
	     && (*str == 0xa || *str == 0x20 || *str == 0x9 || *str == 0xd))
	str++;
    }

  return old;
}

/* I don't ignore the attributes: */
static nxml_error_t
__nxml_dtd_parse_attribute (nxml_t * doc, char **buffer, size_t * size)
{
  char element[1024];
  char *def;
  char *c;
  nxml_error_t ret;
  __nxml_doctype_attribute_t *attribute;

  /*
   * Rule [52] - AttlistDecl ::= '<!ATTLIST' S Name AttDef* S? '>'
   */

  /* !ATTLIST S <- */
  if (**buffer != 0x20 && **buffer != 0x9 && **buffer != 0xa
      && **buffer != 0xd)
    {
      if (doc->priv.func)
	doc->priv.func ("%s: abnormal char '%c' (line %d)\n",
			doc->file ? doc->file : "", **buffer, doc->priv.line);
      return NXML_ERR_PARSER;
    }

  if ((ret =
       __nxml_dtd_parse_get_name (doc, buffer, size, element,
				  sizeof (element))) != NXML_OK)
    return ret;

  /* !ATTLIST S Name S AttDef* <- */
  if ((ret = __nxml_dtd_parse_get_block (doc, buffer, size, &def)) != NXML_OK)
    return ret;

  if ((ret = __nxml_dtd_parse_get_end (doc, buffer, size)) != NXML_OK)
    return ret;

  /* I insert the attribute declaration in the list */
  if (!
      (attribute =
       (__nxml_doctype_attribute_t *)
       malloc (sizeof (__nxml_doctype_attribute_t))))
    return NXML_ERR_POSIX;

  memset (attribute, 0, sizeof (__nxml_doctype_attribute_t));

  if (!(attribute->element = strdup (element)))
    {
      free (attribute);

      return NXML_ERR_POSIX;
    }

  if (!(c = __nxml_entity_trim (doc, def)))
    {
      free (attribute->element);
      free (attribute);
      free (def);

      return NXML_ERR_POSIX;
    }

  else if (!(attribute->attdef = __nxml_dtd_parse_attdef (doc, c)))
    {
      free (def);
      free (attribute->element);
      free (attribute);
      free (c);

      return NXML_ERR_PARSER;
    }

  free (def);
  free (c);

  attribute->next = doc->priv.attributes;
  doc->priv.attributes = attribute;

  return NXML_OK;
}

static nxml_error_t
__nxml_dtd_parse_entity_reference (nxml_t * nxml, char **buffer,
				   size_t * size, char **reference)
{
  char name[1024];
  int i = 0;
  int q;

  if (**buffer == '"')
    q = 0;
  else if (**buffer == '\'')
    q = 1;
  else
    {
      if (nxml->priv.func)
	nxml->priv.func ("%s: expected char '\"' and not '%c' (line %d)\n",
			 nxml->file ? nxml->file : "", **buffer,
			 nxml->priv.line);
      return NXML_ERR_PARSER;
    }

  (*buffer)++;
  (*size)--;
  i = 0;

  while (((!q && **buffer != '"') || (q && **buffer != '\'')) && *size
	 && i < sizeof (name) - 1)
    {
      name[i] = **buffer;

      i++;
      (*buffer)++;
      (*size)--;
    }

  name[i] = 0;

  if ((!q && **buffer != '"') || (q && **buffer != '\''))
    {
      if (nxml->priv.func)
	nxml->priv.func ("%s: expected char '\"' and not '%c' (line %d)\n",
			 nxml->file ? nxml->file : "", **buffer,
			 nxml->priv.line);
      return NXML_ERR_PARSER;
    }

  (*buffer)++;
  (*size)--;

  __nxml_escape_spaces (nxml, buffer, size);

  if (!(*reference = strdup (name)))
    return NXML_ERR_POSIX;

  return NXML_OK;
}

static nxml_error_t
__nxml_dtd_parse_entity_ndata (nxml_t * nxml, char **buffer, size_t * size,
			       char **ndata)
{
  nxml_error_t err;
  char name[1024];

  __nxml_escape_spaces (nxml, buffer, size);

  if (strcmp (*buffer, "NDATA"))
    return NXML_OK;

  if ((err =
       __nxml_dtd_parse_get_name (nxml, buffer, size, name,
				  sizeof (name))) != NXML_OK)
    return err;

  __nxml_escape_spaces (nxml, buffer, size);

  return NXML_OK;
}

/* I don't ignore the entitites: */
static nxml_error_t
__nxml_dtd_parse_entity (nxml_t * nxml, char **buffer, size_t * size)
{
  nxml_error_t ret;
  char name[1024];
  __nxml_doctype_entity_t *entity;
  int percent = 0;
  char *reference = NULL;
  char *system = NULL;
  char *pubid = NULL;
  char *ndata = NULL;

  /*
   * Rule [70] - EntityDecl ::= GEDecl | PEDecl
   * Rule [71] - GEDecl ::= '<!ENTITY' S Name S EntityDef S? '>'
   * Rule [72] - PEDecl ::= '<!ENTITY' S '%' S Name S PEDef S? '>'
   */

  /* !ENTITY S <- */
  if (**buffer != 0x20 && **buffer != 0x9 && **buffer != 0xa
      && **buffer != 0xd)
    {
      if (nxml->priv.func)
	nxml->priv.func ("%s: abnormal char '%c' (line %d)\n",
			 nxml->file ? nxml->file : "", **buffer,
			 nxml->priv.line);
      return NXML_ERR_PARSER;
    }

  while ((**buffer == 0x20 || **buffer == 0x9 || **buffer == 0xa
	  || **buffer == 0xd) && *size > 0)
    {
      if (**buffer == 0xa && nxml->priv.func)
	nxml->priv.line++;
      (*buffer)++;
      (*size)--;
    }

  if (**buffer == '%')
    {
      percent = 1;

      (*buffer)++;
      (*size)--;

      if (**buffer != 0x20 && **buffer != 0x9 && **buffer != 0xa
	  && **buffer != 0xd)
	{
	  if (nxml->priv.func)
	    nxml->priv.func ("%s: abnormal char '%c' after '%' (line %d)\n",
			     nxml->file ? nxml->file : "", **buffer,
			     nxml->priv.line);
	  return NXML_ERR_PARSER;
	}

      if ((ret =
	   __nxml_dtd_parse_get_name (nxml, buffer, size, name,
				      sizeof (name))) != NXML_OK)
	return ret;

    }

  else
    {
      if ((ret =
	   __nxml_dtd_parse_get_name (nxml, buffer, size, name,
				      sizeof (name))) != NXML_OK)
	return ret;
    }

  if (**buffer == '"' || **buffer == '\'')
    {
      if ((ret =
	   __nxml_dtd_parse_entity_reference (nxml, buffer, size,
					      &reference)) != NXML_OK)
	return ret;
    }

  else
    {
      if ((ret =
	   __nxml_dtd_parse_externalID (nxml, buffer, size, &system,
					&pubid)) != NXML_OK)
	return ret;
    }

  if (percent)
    {
      if ((ret =
	   __nxml_dtd_parse_entity_ndata (nxml, buffer, size,
					  &ndata)) != NXML_OK)
	{
	  if (system)
	    free (system);
	  if (pubid)
	    free (pubid);
	  return ret;
	}
    }

  if ((ret = __nxml_dtd_parse_get_end (nxml, buffer, size)) != NXML_OK)
    return ret;

  if (!
      (entity =
       (__nxml_doctype_entity_t *) malloc (sizeof (__nxml_doctype_entity_t))))
    return NXML_ERR_POSIX;

  memset (entity, 0, sizeof (__nxml_doctype_entity_t));

  if (!(entity->name = strdup (name)))
    {
      free (entity);

      if (system)
	free (system);
      if (pubid)
	free (pubid);
      if (reference)
	free (reference);
      if (ndata)
	free (ndata);

      return NXML_ERR_POSIX;
    }

  entity->percent = percent;
  entity->reference = reference;
  entity->ndata = ndata;
  entity->system = system;
  entity->pubid = pubid;

  entity->next = nxml->priv.entities;
  nxml->priv.entities = entity;

  return NXML_OK;
}

/* This function checks the name of the tag and runs the correct
 * function to parse it. */
static nxml_error_t
__nxml_dtd_parse_get_tag (nxml_t * doc, char **buffer, size_t * size,
			  int *done)
{

  __nxml_escape_spaces (doc, buffer, size);

  if (!*size)
    {
      *done = 0;
      return NXML_OK;
    }

  *done = 1;

  if (**buffer != '<')
    {
      if (doc->priv.func)
	doc->priv.func ("%s: abnormal char '%c' (line %d)\n",
			doc->file ? doc->file : "", **buffer, doc->priv.line);
      return NXML_ERR_PARSER;
    }

  (*buffer)++;
  (*size)--;

  if (!strncmp (*buffer, "!ELEMENT", 8))
    {
      (*buffer) += 8;
      (*size) -= 8;

      return __nxml_dtd_parse_element (doc, buffer, size);
    }

  if (!strncmp (*buffer, "!ATTLIST", 8))
    {
      (*buffer) += 8;
      (*size) -= 8;

      return __nxml_dtd_parse_attribute (doc, buffer, size);
    }

  if (!strncmp (*buffer, "!ENTITY", 7))
    {
      (*buffer) += 7;
      (*size) -= 7;

      return __nxml_dtd_parse_entity (doc, buffer, size);
    }

  if (!strncmp (*buffer, "!NOTATION", 9))
    {
      (*buffer) += 9;
      (*size) -= 9;

      return __nxml_dtd_parse_notation (doc, buffer, size);
    }

  if (**buffer == '?')
    {
      (*buffer)++;
      (*size)--;

      return __nxml_dtd_parse_pi (doc, buffer, size);
    }

  if (!strncmp (*buffer, "!--", 3))
    {
      (*buffer) += 3;
      (*size) -= 3;

      return __nxml_dtd_parse_comment (doc, buffer, size);
    }

  if (!strncmp (*buffer, "![", 2))
    {
      int q = 0;

      /* TODO: parse ignore and include tags */
      while (*size > 0)
	{
	  if (**buffer == 0xa && doc->priv.func)
	    doc->priv.line++;

	  if (**buffer == '<')
	    q++;

	  else if (**buffer == '>' && !q)
	    break;

	  else if (**buffer == '>')
	    q--;

	  (*buffer)++;
	  (*size)--;
	}

      return __nxml_dtd_parse_get_end (doc, buffer, size);
    }

  if (doc->priv.func)
    doc->priv.func ("%s: unexpected element (line %d)\n",
		    doc->file ? doc->file : "", doc->priv.line);

  return NXML_ERR_PARSER;
}

static __nxml_doctype_attribute_t *
__nxml_dtd_find_attribute (__nxml_doctype_attribute_t * attr, char *value)
{
  while (attr)
    {
      if (!strcmp (attr->element, value))
	return attr;

      attr = attr->next;
    }

  return NULL;
}

static nxml_error_t
__nxml_dtd_valid_node_attributes_single (nxml_t * nxml, nxml_data_t * data,
					 nxml_attr_t * attr,
					 __nxml_doctype_attribute_attdef_t *
					 def)
{

  /* for each def of attributes... */
  while (def)
    {
      /* ... find the current... */
      if (!strcmp (def->name, attr->name))
	{
	  /* has this attribute a fixed value ? */
	  if (def->value == NXML_DOCTYPE_ATTRIBUTE_VALUE_FIXED
	      && strcmp (def->fixed_value, attr->value))
	    {
	      if (nxml->priv.func)
		nxml->priv.
		  func
		  ("%s: expected value '%s' for the attribute '%s' of the node '%s'.\n",
		   nxml->file ? nxml->file : "", def->fixed_value, attr->name,
		   data->value);
	      return NXML_ERR_PARSER;
	    }

	  /* has this attribute a value from a list ? */
	  if (def->list)
	    {
	      __nxml_doctype_attribute_attdef_list_t *list = def->list;
	      while (list)
		{
		  if (!strcmp (list->value, attr->value))
		    break;
		  list = list->next;
		}

	      if (!list)
		{
		  if (nxml->priv.func)
		    nxml->priv.
		      func
		      ("%s: the attribute '%s' of the node '%s' requires a value from a list.\n",
		       nxml->file ? nxml->file : "", attr->name, data->value);
		  return NXML_ERR_PARSER;
		}
	    }

	  return NXML_OK;
	}

      def = def->next;
    }

  if (nxml->priv.func)
    nxml->priv.func ("%s: unexpected attribute '%s' for the node '%s'.\n",
		     nxml->file ? nxml->file : "", attr->name, data->value);
  return NXML_ERR_PARSER;
}

static nxml_error_t
__nxml_dtd_valid_node_attributes (nxml_t * nxml, nxml_data_t * data)
{
  __nxml_doctype_attribute_t *attribute;

  /* There are attributes for this node? */
  if ((attribute =
       __nxml_dtd_find_attribute (nxml->priv.attributes, data->value))
      &&& attribute->attdef)
    {
      nxml_attr_t *attr;
      nxml_error_t ret;
      __nxml_doctype_attribute_attdef_t *at;

      /* Check the attribute of the node */
      attr = data->attributes;
      while (attr)
	{
	  if ((ret =
	       __nxml_dtd_valid_node_attributes_single (nxml, data, attr,
							attribute->attdef)) !=
	      NXML_OK)
	    return ret;
	  attr = attr->next;
	}

      /* If ok, check the request node */
      at = attribute->attdef;
      while (at)
	{
	  if (at->value == NXML_DOCTYPE_ATTRIBUTE_VALUE_REQUIRED)
	    {
	      attr = data->attributes;
	      while (attr)
		{
		  if (!strcmp (attr->name, at->name))
		    break;
		  attr = attr->next;
		}

	      if (!attr)
		{
		  if (nxml->priv.func)
		    nxml->priv.
		      func
		      ("%s: expected attribute '%s' for the node '%s'.\n",
		       nxml->file ? nxml->file : "", at->name, data->value);
		  return NXML_ERR_PARSER;
		}
	    }
	  at = at->next;
	}
    }

  /* If no attributes... */
  else
    {
      if (data->attributes)
	{
	  if (nxml->priv.func)
	    nxml->priv.func ("%s: unexpected attributes for the node '%s'.\n",
			     nxml->file ? nxml->file : "", data->value);
	  return NXML_ERR_PARSER;
	}
    }

  return NXML_OK;
}

static __nxml_doctype_element_t *
__nxml_dtd_find_element (__nxml_doctype_element_t * element, char *value)
{
  while (element)
    {
      if (!strcmp (element->name, value))
	return element;

      element = element->next;
    }

  return NULL;
}

static nxml_error_t
__nxml_dtd_valid_node_elements_single (nxml_t * nxml, nxml_data_t * data,
				       __nxml_doctype_element_content_t * c,
				       int exclame)
{
  nxml_data_t *t_data = data;
  int data_no_loop = 0;
  int id = 0, error, choose = 0;

  while (data)
    {
      error = 0;

      while (data->type != NXML_TYPE_TEXT && data->type != NXML_TYPE_ELEMENT)
	data = data->next;

      if (!data)
	break;

      if (c->pcdata)
	{
	  t_data = data->children;
	  while (t_data)
	    {
	      if (t_data->type == NXML_TYPE_ELEMENT)
		{
		  if (c->choose)
		    error = 1;
		  else
		    {
		      if (nxml->priv.func && exclame)
			nxml->priv.
			  func
			  ("%s: expected a #PCDATA string after node '%s'.\n",
			   nxml->file ? nxml->file : "", data->value);
		      return NXML_ERR_PARSER;
		    }

		  break;
		}

	      t_data = t_data->next;
	    }

	  if (!error && (c->choose || choose))
	    while (c && c->choose)
	      c = c->next;

	  if (c->choose)
	    choose = 1;
	  else
	    choose = 0;

	  c = c->next;
	}

      else if (c->name)
	{
	  switch (c->type)
	    {
	    case NXML_DOCTYPE_ELEMENT_CONTENT_ONE:
	      if (strcmp (c->name, data->value))
		{
		  if (c->choose)
		    error = 1;
		  else
		    {
		      if (nxml->priv.func && exclame)
			nxml->priv.func
			  ("%s: expected node '%s' and not '%s'.\n",
			   nxml->file ? nxml->file : "", c->name,
			   data->value);
		      return NXML_ERR_PARSER;
		    }
		}

	      if (!error && (c->choose || choose))
		while (c && c->choose)
		  c = c->next;

	      if (c->choose)
		choose = 1;
	      else
		choose = 0;

	      c = c->next;

	      break;

	    case NXML_DOCTYPE_ELEMENT_CONTENT_0_OR_1:
	      if (strcmp (c->name, data->value))
		data_no_loop = 1;

	      if (c->choose)
		choose = 1;
	      else
		choose = 0;

	      c = c->next;
	      break;

	    case NXML_DOCTYPE_ELEMENT_CONTENT_0_OR_MORE:
	      if (strcmp (c->name, data->value))
		{
		  if (c->choose)
		    choose = 1;
		  else
		    choose = 0;

		  c = c->next;
		  data_no_loop = 1;
		}
	      break;

	    case NXML_DOCTYPE_ELEMENT_CONTENT_1_OR_MORE:
	      if (strcmp (c->name, data->value))
		{
		  if (!id)
		    {
		      if (c->choose)
			error = 1;
		      else
			{
			  if (nxml->priv.func && exclame)
			    nxml->priv.func
			      ("%s: expected node '%s' and not '%s'.\n",
			       nxml->file ? nxml->file : "", c->name,
			       data->value);
			  return NXML_ERR_PARSER;
			}

		      if (!error && (c->choose || choose))
			while (c && c->choose)
			  c = c->next;

		      if (c->choose)
			choose = 1;
		      else
			choose = 0;

		      c = c->next;
		    }
		  else
		    {
		      id = 0;
		      if (c->choose)
			choose = 1;
		      else
			choose = 0;

		      c = c->next;
		    }
		}

	      else
		id = 1;

	      break;
	    }
	}

      else if (__nxml_dtd_valid_node_elements_single (nxml, data, c->list, 0)
	       != NXML_OK)
	{
	  if (c->choose)
	    error = 1;
	  else
	    {
	      if (nxml->priv.func && exclame)
		nxml->priv.func ("%s: no correct value after node '%s'.\n",
				 nxml->file ? nxml->file : "", data->value);
	      return NXML_ERR_PARSER;
	    }

	  if (!error && (c->choose || choose))
	    while (c && c->choose)
	      c = c->next;

	  if (c->choose)
	    choose = 1;
	  else
	    choose = 0;

	  c = c->next;
	}

      if (data_no_loop)
	data_no_loop = 0;
      else
	data = data->next;
    }

  while (c)
    {
      if (c->type == NXML_DOCTYPE_ELEMENT_CONTENT_ONE
	  || c->type == NXML_DOCTYPE_ELEMENT_CONTENT_1_OR_MORE)
	{
	  if (nxml->priv.func && exclame)
	    nxml->priv.func ("%s: no correct document.\n",
			     nxml->file ? nxml->file : "");
	  return NXML_ERR_PARSER;
	}

      c = c->next;
    }

  return NXML_OK;
}

static nxml_error_t
__nxml_dtd_valid_node_children (nxml_t * nxml, nxml_data_t * data)
{
  __nxml_doctype_element_t *element;

  /* There are elements for this node? */
  if ((element = __nxml_dtd_find_element (nxml->priv.elements, data->value))
      && element->type != NXML_DOCTYPE_ELEMENT_EMPTY)
    {
      nxml_error_t ret;

      /* Check the element of the node */
      if ((ret =
	   __nxml_dtd_valid_node_elements_single (nxml, data->children,
						  element->content, 1)) !=
	  NXML_OK)
	return ret;

      /* Recursive... */
      data = data->children;
      while (data)
	{
	  if ((ret = __nxml_dtd_valid_node (nxml, data)) != NXML_OK)
	    return ret;
	  data = data->next;
	}
    }

  /* If no elements... */
  else
    {
      if (data->children)
	{
	  if (nxml->priv.func)
	    nxml->priv.
	      func ("%s: unexpected children elements for the node '%s'.\n",
		    nxml->file ? nxml->file : "", data->value);
	  return NXML_ERR_PARSER;
	}
    }

  return NXML_OK;
}

/* This function checks a single node */
static nxml_error_t
__nxml_dtd_valid_node (nxml_t * nxml, nxml_data_t * data)
{
  nxml_error_t ret;

  if ((ret = __nxml_dtd_valid_node_attributes (nxml, data)) != NXML_OK)
    return ret;

  if ((ret = __nxml_dtd_valid_node_children (nxml, data)) != NXML_OK)
    return ret;

  return NXML_OK;
}

/* This function check the data struct and parse the document... */
static nxml_error_t
__nxml_dtd_valid (nxml_t * nxml)
{
  nxml_doctype_t *doctype = nxml->doctype;
  nxml_data_t *data;
  nxml_error_t err;

  __nxml_entity_parse (nxml);

  if ((err = nxml_root_element (nxml, &data)) != NXML_OK || !data)
    return err;

  if (!strcmp (data->value, doctype->value))
    {
      if (nxml->priv.func)
	nxml->priv.
	  func ("%s: expected '%s' as root element and not '%s' (line %d).\n",
		nxml->file ? nxml->file : "", doctype->value, data->value,
		nxml->priv.line);
      return NXML_ERR_PARSER;
    }

  return __nxml_dtd_valid_node (nxml, data);
}

/* EXTERNAL FUNCTIONS ******************************************************/

nxml_error_t
nxml_dtd_parse_buffer (nxml_t * doc, char *buffer, size_t size, int flag)
{
  int done = 0;
  nxml_error_t err;

  if (!doc || !buffer)
    return NXML_ERR_DATA;

  if (!size)
    size = strlen (buffer);

  doc->priv.line = 1;

  while (!(err = __nxml_dtd_parse_get_tag (doc, &buffer, &size, &done))
	 && done);

  if ((flag & NXML_DOCTYPEFLAG_RECURSIVE))
    {
      __nxml_doctype_notation_t *n = doc->priv.notations;

      while (n)
	{
	  if (n->system_literal)
	    __nxml_dtd_parse_something (doc, n->system_literal, flag);
	  n = n->next;
	}
    }

  if (err == NXML_OK)
    err = __nxml_dtd_valid (doc);

  __nxml_dtd_clear (doc);

  return err;
}

nxml_error_t
nxml_dtd_parse_url (nxml_t * doc, char *url, int flag)
{
  __nxml_download_t *download;
  nxml_error_t err;

  if (!doc || !url)
    return NXML_ERR_DATA;

  if (!(download = __nxml_download_file (doc, url)))
    return NXML_ERR_POSIX;

  err = nxml_dtd_parse_buffer (doc, download->mm, download->size, flag);

  free (download->mm);
  free (download);

  return err;
}

nxml_error_t
nxml_dtd_parse_file (nxml_t * doc, char *file, int flag)
{
  nxml_error_t err;
  char *buffer;
  struct stat st;
  int fd;

  if (!file || !doc)
    return NXML_ERR_DATA;

  if (stat (file, &st))
    return NXML_ERR_POSIX;

  if ((fd = open (file, O_RDONLY)) < 0)
    return NXML_ERR_POSIX;

  if (!(buffer = (char *) malloc (sizeof (char) * st.st_size)))
    return NXML_ERR_POSIX;

  if (read (fd, buffer, st.st_size) != st.st_size)
    {
      free (buffer);
      close (fd);
      return NXML_ERR_POSIX;
    }

  close (fd);

  err = nxml_dtd_parse_buffer (doc, buffer, st.st_size, flag);

  free (buffer);

  return err;
}

nxml_error_t
nxml_valid_dtd (nxml_t * nxml, int flag)
{
  char *buffer;
  size_t size;
  nxml_doctype_t *tmp;
  char *system_literal = NULL;
  char *pubid_literal = NULL;
  nxml_error_t err;

  if (!nxml)
    return NXML_ERR_DATA;

  if (!nxml->doctype)
    {
      if (nxml->priv.func)
	nxml->priv.func ("%s: no doctype founded for this XML document\n",
			 nxml->file ? nxml->file : "");
      return NXML_ERR_PARSER;
    }

  tmp = nxml->doctype;

  while (tmp)
    {
      buffer = tmp->value;
      size = strlen (buffer);

      /*
       * Rule [28] - doctypedecl ::= '<!DOCTYPE' S Name (S ExternalID)? S?
       *                             ('[' intSubset ']' S?)? '>'
       *
       * buffer is: (S ExternalID)? S? ('[' intSubset ']' S?)?
       *
       * Rule [75] - ExternalID ::= 'SYSTEM' S SystemLiteral |
       *                            'PUBLIC' S PubidLiteral S SystemLiteral
       */
      if (!strncmp (buffer, "SYSTEM", 6))
	{
	  buffer += 6;
	  size -= 6;

	  __nxml_escape_spaces (nxml, &buffer, &size);

	  if (!(system_literal = __nxml_get_value (nxml, &buffer, &size)))
	    {
	      if (nxml->priv.func)
		nxml->priv.
		  func
		  ("%s: expected the SystemLiteral element after SYSTEM (line %d).\n",
		   nxml->file ? nxml->file : "", nxml->priv.line);
	      return NXML_ERR_PARSER;
	    }
	}

      else if (!strncmp (buffer, "PUBLIC", 6))
	{
	  buffer += 6;
	  size -= 6;

	  __nxml_escape_spaces (nxml, &buffer, &size);

	  if (!(pubid_literal = __nxml_get_value (nxml, &buffer, &size)))
	    {
	      if (nxml->priv.func)
		nxml->priv.
		  func
		  ("%s: expected the PubidLiteral element after PUBLIC (line %d).\n",
		   nxml->file ? nxml->file : "", nxml->priv.line);
	      return NXML_ERR_PARSER;
	    }

	  __nxml_escape_spaces (nxml, &buffer, &size);

	  if (!(system_literal = __nxml_get_value (nxml, &buffer, &size)))
	    {
	      free (pubid_literal);

	      if (nxml->priv.func)
		nxml->priv.
		  func
		  ("%s: expected the SystemLiteral element after PubidLiteral '%s' (line %d).\n",
		   nxml->file ? nxml->file : "", pubid_literal,
		   nxml->priv.line);
	      return NXML_ERR_PARSER;
	    }
	}

      tmp->system_literal = system_literal;
      tmp->pubid_literal = pubid_literal;

      if (system_literal && (flag & NXML_DOCTYPEFLAG_RECURSIVE))
	{
	  err = __nxml_dtd_parse_something (nxml, system_literal, flag);

	  if (err != NXML_OK)
	    {
	      nxml_empty_doctype (nxml->doctype);
	      return err;
	    }
	}

      __nxml_escape_spaces (nxml, &buffer, &size);

      /* 
       * Rule [28b] - intSubset ::= (markupdecl | DeclSep)*
       */
      if (*buffer == '[')
	{
	  int i = 0;
	  char *value;
	  int q = 0;

	  buffer++;
	  size--;

	  while ((*(buffer + i) != ']' || q) && i < size)
	    {
	      if (*(buffer + i) == '[')
		q++;

	      else if (*(buffer + i) == ']')
		q--;

	      if (*(buffer + i) == 0xa && nxml->priv.func)
		nxml->priv.line++;

	      i++;
	    }

	  if (*(buffer + i) != ']')
	    {
	      if (nxml->priv.func)
		nxml->priv.func ("%s: expected ']' char (line %d)\n",
				 nxml->file ? nxml->file : "",
				 nxml->priv.line);

	      nxml_empty_doctype (nxml->doctype);

	      return NXML_ERR_PARSER;
	    }

	  if (!(value = (char *) malloc (sizeof (char) * (i + 1))))
	    {
	      nxml_empty_doctype (nxml->doctype);
	      return NXML_ERR_POSIX;
	    }

	  strncpy (value, buffer, i);
	  value[i] = 0;

	  err = nxml_dtd_parse_buffer (nxml, value, i, flag);

	  if (err != NXML_OK)
	    {
	      nxml_empty_doctype (nxml->doctype);
	      free (value);
	      return err;
	    }

	  free (value);
	}

      else if (*buffer)
	{
	  if (nxml->priv.func)
	    nxml->priv.
	      func
	      ("%s: expected PUBLIC or SYSTEM or a intSubset declaration but not this char '%c' (line %d)\n",
	       nxml->file ? nxml->file : "", *buffer, nxml->priv.line);

	  nxml_empty_doctype (nxml->doctype);

	  return NXML_ERR_PARSER;
	}

      tmp = tmp->next;
    }

  return NXML_OK;
}

/* EOF */
