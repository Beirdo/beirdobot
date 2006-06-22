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

nxml_error_t
nxml_new (nxml_t ** nxml)
{
  if (!nxml)
    return NXML_ERR_DATA;

  if (!(*nxml = (nxml_t *) malloc (sizeof (nxml_t))))
    return NXML_ERR_POSIX;

  memset (*nxml, 0, sizeof (nxml_t));

  return NXML_OK;
}

static void
nxml_add_rec (nxml_t * nxml, nxml_data_t * data)
{
  while (data)
    {
      data->doc = nxml;
      if (data->children)
	nxml_add_rec (nxml, data->children);

      data = data->next;
    }
}

nxml_error_t
nxml_add (nxml_t * nxml, nxml_data_t * parent, nxml_data_t ** child)
{
  nxml_data_t *tmp;

  if (!nxml || !child)
    return NXML_ERR_DATA;

  if (!*child)
    {
      if (!(*child = (nxml_data_t *) malloc (sizeof (nxml_data_t))))
	return NXML_ERR_POSIX;

      memset (*child, 0, sizeof (nxml_data_t));
    }

  (*child)->doc = nxml;
  (*child)->parent = parent;
  (*child)->next = NULL;


  if (parent)
    {
      if (!parent->children)
	parent->children = *child;

      else
	{
	  tmp = parent->children;

	  while (tmp->next)
	    tmp = tmp->next;

	  tmp->next = *child;
	}
    }
  else
    {
      if (!nxml->data)
	nxml->data = *child;

      else
	{
	  tmp = nxml->data;

	  while (tmp->next)
	    tmp = tmp->next;

	  tmp->next = *child;
	}
    }

  nxml_add_rec (nxml, (*child)->children);

  return NXML_OK;
}

nxml_error_t
nxml_remove (nxml_t * nxml, nxml_data_t * parent, nxml_data_t * child)
{
  nxml_data_t *tmp, *old;

  if (!nxml || !child)
    return NXML_ERR_DATA;

  if (parent)
    tmp = parent->children;
  else
    tmp = nxml->data;

  old = NULL;

  while (tmp)
    {
      if (tmp == child)
	{
	  if (old)
	    old->next = child->next;
	  else if (parent)
	    parent->children = child->next;
	  else
	    nxml->data = child->next;

	  break;
	}

      old = tmp;
      tmp = tmp->next;
    }

  child->next = NULL;

  return NXML_OK;
}

nxml_error_t
nxml_add_attribute (nxml_t * nxml, nxml_data_t * element, nxml_attr_t ** attr)
{
  nxml_attr_t *tmp;

  if (!nxml || !element || !attr)
    return NXML_ERR_DATA;

  if (!*attr)
    {
      if (!(*attr = (nxml_attr_t *) malloc (sizeof (nxml_attr_t))))
	return NXML_ERR_POSIX;

      memset (*attr, 0, sizeof (nxml_attr_t));
    }

  (*attr)->next = NULL;

  if (!element->attributes)
    element->attributes = *attr;

  else
    {
      tmp = element->attributes;

      while (tmp->next)
	tmp = tmp->next;

      tmp->next = *attr;
    }

  return NXML_OK;
}

nxml_error_t
nxml_remove_attribute (nxml_t * nxml, nxml_data_t * element,
		       nxml_attr_t * attr)
{
  nxml_attr_t *tmp, *old;

  if (!nxml || !element || !attr)
    return NXML_ERR_DATA;

  tmp = element->attributes;

  old = NULL;

  while (tmp)
    {
      if (tmp == attr)
	{
	  if (old)
	    old->next = attr->next;
	  else
	    element->attributes = attr->next;

	  break;
	}

      old = tmp;
      tmp = tmp->next;
    }

  attr->next = NULL;

  return NXML_OK;
}

nxml_error_t
nxml_add_namespace (nxml_t * nxml, nxml_data_t * element,
		    nxml_namespace_t ** namespace)
{
  nxml_namespace_t *tmp;

  if (!nxml || !element || !namespace)
    return NXML_ERR_DATA;

  if (!*namespace)
    {
      if (!
	  (*namespace =
	   (nxml_namespace_t *) malloc (sizeof (nxml_namespace_t))))
	return NXML_ERR_POSIX;

      memset (*namespace, 0, sizeof (nxml_namespace_t));
    }

  (*namespace)->next = NULL;

  if (!element->ns_list)
    element->ns_list = *namespace;

  else
    {
      tmp = element->ns_list;

      while (tmp->next)
	tmp = tmp->next;

      tmp->next = *namespace;
    }

  return NXML_OK;
}

nxml_error_t
nxml_remove_namespace (nxml_t * nxml, nxml_data_t * element,
		       nxml_namespace_t * namespace)
{
  nxml_namespace_t *tmp, *old;

  if (!nxml || !element || !namespace)
    return NXML_ERR_DATA;

  tmp = element->ns_list;

  old = NULL;

  while (tmp)
    {
      if (tmp == namespace)
	{
	  if (old)
	    old->next = namespace->next;
	  else
	    element->ns_list = namespace->next;

	  break;
	}

      old = tmp;
      tmp = tmp->next;
    }

  namespace->next = NULL;

  return NXML_OK;
}

nxml_error_t
nxml_set_func (nxml_t * nxml, void (*func) (char *, ...))
{
  if (!nxml)
    return NXML_ERR_DATA;

  nxml->priv.func = func;

  return NXML_OK;
}

nxml_error_t
nxml_set_timeout (nxml_t * nxml, int timeout)
{
  if (!nxml)
    return NXML_ERR_DATA;

  nxml->priv.timeout = timeout;

  return NXML_OK;
}

void
nxml_print_generic (char *str, ...)
{
  va_list va;

  va_start (va, str);
  vfprintf (stderr, str, va);
  va_end (va);
}


/* EOF */
