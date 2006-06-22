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


static void
__nxml_entity_doctype (nxml_t * nxml, nxml_namespace_t * ns)
{
  char *str;

  while (ns)
    {
      if (ns->prefix && (str = __nxml_entity_trim (nxml, ns->prefix)))
	{
	  free (ns->prefix);
	  ns->prefix = str;
	}

      if (ns->ns && (str = __nxml_entity_trim (nxml, ns->ns)))
	{
	  free (ns->ns);
	  ns->ns = str;
	}

      ns = ns->next;
    }
}

static void
__nxml_entity_rec (nxml_t * nxml, nxml_data_t * e)
{
  char *str;
  nxml_attr_t *attr;

  if (e->value && (str = __nxml_entity_trim (nxml, e->value)))
    {
      free (e->value);
      e->value = str;
    }

  attr = e->attributes;
  while (attr)
    {
      if (attr->name && (str = __nxml_entity_trim (nxml, attr->name)))
	{
	  free (attr->name);
	  attr->name = str;
	}

      if (attr->value && (str = __nxml_entity_trim (nxml, attr->value)))
	{
	  free (attr->value);
	  attr->value = str;
	}

      attr = attr->next;
    }

  if (e->ns_list)
    __nxml_entity_doctype (nxml, e->ns_list);

  e = e->children;
  while (e)
    {
      __nxml_entity_rec (nxml, e);
      e = e->next;
    }
}

void
__nxml_entity_parse (nxml_t * nxml)
{
  nxml_data_t *e;

  e = nxml->data;
  while (e)
    {
      __nxml_entity_rec (nxml, e);
      e = e->next;
    }
}

/* EOF */
