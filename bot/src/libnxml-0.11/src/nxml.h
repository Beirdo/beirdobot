/* nXml - Copyright (C) 2005-2006 bakunin - Andrea Marchesini 
 *                                    <bakunin@autistici.org>
 *
 * This source code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Public License as published 
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This source code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Please refer to the GNU Public License for more details.
 *
 * You should have received a copy of the GNU Public License along with
 * this source code; if not, write to:
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __N_XML_H__
#define __N_XML_H__

#include <curl/curl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef  __cplusplus
extern "C" {
#endif

typedef struct nxml_t nxml_t;
typedef struct nxml_data_t nxml_data_t;
typedef struct nxml_attr_t nxml_attr_t;
typedef struct nxml_doctype_t nxml_doctype_t;
typedef struct nxml_namespace_t nxml_namespace_t;

typedef struct __nxml_doctype_notation_t __nxml_doctype_notation_t;
typedef struct __nxml_doctype_element_t __nxml_doctype_element_t;
typedef struct __nxml_doctype_element_content_t __nxml_doctype_element_content_t;
typedef struct __nxml_doctype_attribute_t __nxml_doctype_attribute_t;
typedef struct __nxml_doctype_attribute_attdef_t __nxml_doctype_attribute_attdef_t;
typedef struct __nxml_doctype_attribute_attdef_list_t __nxml_doctype_attribute_attdef_list_t;
typedef struct __nxml_doctype_entity_t __nxml_doctype_entity_t;
typedef struct __nxml_private_t __nxml_private_t;

/** This enum describes the error type of libnxml */
typedef enum
{
  NXML_OK = 0,			/**< No error */
  NXML_ERR_POSIX,		/**< For the correct error, use errno */
  NXML_ERR_PARSER,		/**< Parser error */
  NXML_ERR_DATA			/**< The parameters are incorrect */
} nxml_error_t;

/** This enum describes the type of data element of libnxml */
typedef enum
{
  NXML_TYPE_TEXT,		/**< Text element */
  NXML_TYPE_COMMENT,		/**< Comment element */
  NXML_TYPE_ELEMENT,		/**< Data element */
  NXML_TYPE_PI,			/**< PI element */
  NXML_TYPE_ELEMENT_CLOSE,	/**< Data element - For internal use only */
} nxml_type_t;

/** This enum describes the supported XML version */
typedef enum
{
  NXML_VERSION_1_1,		/**< XML 1.1 */
  NXML_VERSION_1_0		/**< XML 1.0 */
} nxml_version_t;

/** This enum describes the CharSet of XML document */
typedef enum 
{
  NXML_CHARSET_UTF8,		/**< UTF8 chatset detected */
  NXML_CHARSET_UTF16LE,		/**< UTF 16 Little Endian detected */
  NXML_CHARSET_UTF16BE,		/**< UTF 16 Big Endian detected */
  NXML_CHARSET_UCS4_1234,	/**< UCS 4byte order 1234 detected */
  NXML_CHARSET_UCS4_4321,	/**< UCS 3byte order 4321 detected */
  NXML_CHARSET_UCS4_2143,	/**< UCS 3byte order 2143 detected */
  NXML_CHARSET_UCS4_3412,	/**< UCS 3byte order 3412 detected */
  NXML_CHARSET_UNKNOWN		/**< Unknown format */
} nxml_charset_t;

#define NXML_DOCTYPEFLAG_NOFLAG		0x0 /**< No flag */
#define NXML_DOCTYPEFLAG_DOWNLOAD	0x1 /**< Download the document */
#define NXML_DOCTYPEFLAG_RECURSIVE	0x2 /**< Do it recursive */

/** This enum describes the type of element for the doctype parsing.
 * It is for internal use only */
typedef enum {
  NXML_DOCTYPE_ELEMENT_EMPTY,
  NXML_DOCTYPE_ELEMENT_ANY,
  NXML_DOCTYPE_ELEMENT_MIXED_OR_CHILDREN,
} __nxml_doctype_element_type_t;

typedef enum {
  NXML_DOCTYPE_ELEMENT_CONTENT_ONE = 0,
  NXML_DOCTYPE_ELEMENT_CONTENT_0_OR_1,
  NXML_DOCTYPE_ELEMENT_CONTENT_0_OR_MORE,
  NXML_DOCTYPE_ELEMENT_CONTENT_1_OR_MORE
} __nxml_doctype_element_content_type_t;

typedef enum {
  NXML_DOCTYPE_ATTRIBUTE_TYPE_ENUMERATION,
  NXML_DOCTYPE_ATTRIBUTE_TYPE_NOTATION,
  NXML_DOCTYPE_ATTRIBUTE_TYPE_CDATA,
  NXML_DOCTYPE_ATTRIBUTE_TYPE_ID,
  NXML_DOCTYPE_ATTRIBUTE_TYPE_IDREF,
  NXML_DOCTYPE_ATTRIBUTE_TYPE_IDREFS,
  NXML_DOCTYPE_ATTRIBUTE_TYPE_ENTITY,
  NXML_DOCTYPE_ATTRIBUTE_TYPE_ENTITIES,
  NXML_DOCTYPE_ATTRIBUTE_TYPE_NMTOKEN,
  NXML_DOCTYPE_ATTRIBUTE_TYPE_NMTOKENS,
} __nxml_doctype_attribute_attdef_type_t;

typedef enum {
  NXML_DOCTYPE_ATTRIBUTE_VALUE_ANY,
  NXML_DOCTYPE_ATTRIBUTE_VALUE_REQUIRED,
  NXML_DOCTYPE_ATTRIBUTE_VALUE_IMPLIED,
  NXML_DOCTYPE_ATTRIBUTE_VALUE_FIXED
} __nxml_doctype_attribute_attdef_value_t;

/** Data struct for any element of XML stream 
 *
 * \brief
 * Data struct for any element of XML streams/files
 */
struct nxml_data_t
{
  nxml_type_t type;		/**< type of this nxml_data_t struct */

  char *value;			/**< The value of this data struct */

  nxml_attr_t *attributes;	/**< List of attributes of this struct. 
	  			 This list exists only if 
				 type == NXML_TYPE_ELEMENT */

  nxml_namespace_t *ns;         /**< Pointer to the correct namespace */
  nxml_namespace_t *ns_list;    /**< The namespaces in this element */

  nxml_data_t *children;	/**< The children of this data struct */
  nxml_data_t *next;		/**< The next element */

  nxml_data_t *parent;		/**< The parent */
  nxml_t *doc;			/**< The nxml_t */
};

/** Data struct for any element of attribute of xml element 
 *
 * \brief
 * Data struct for any element of attribute of xml element
 */
struct nxml_attr_t
{
  char *name;
  char *value;

  nxml_namespace_t *ns;

  nxml_attr_t *next;
};

/** Data struct for doctype elements
 *
 * \brief
 * Data struct for doctype elements
 */
struct nxml_doctype_t
{
  char *value;			/**< The string no parsers */
  char *name;			/**< The name of current doctype */

  nxml_t *doc;			/**< The nxml_t */
  nxml_doctype_t *next;

  /** This information will be set only after a nxml_valid_dtd(...) */

  char *system_literal;		/**< If the DTD has a system_leteral */
  char *pubid_literal;		/**< If the DTD has a system_leteral */
};

/** Data struct private for doctype parsing of contents of a element
 *
 * \brief
 * Data struct private for doctype parsing of contents of a element
 */
struct __nxml_doctype_element_content_t {
  char *name;
  int pcdata;
  int choose;

  __nxml_doctype_element_content_type_t type;
  __nxml_doctype_element_content_t *list;

  __nxml_doctype_element_content_t *next;
};

/** Data struct private for doctype parsing of elements 
 *
 * \brief
 * Data struct private for doctype parsing of elements
 */
struct __nxml_doctype_element_t {
  char *name;
  __nxml_doctype_element_content_t *content;

  __nxml_doctype_element_type_t type;
  __nxml_doctype_element_t *next;
};

/** Data struct for any element of attribute of xml element 
 *
 * \brief
 * Data struct for any element of attribute of xml element
 */
struct __nxml_doctype_attribute_attdef_list_t
{
  char *value;
  __nxml_doctype_attribute_attdef_list_t *next;
};

/** Data struct private for doctype parsing of attdefs of a attribute
 *
 * \brief
 * Data struct private for doctype parsing of attdefs of a attribute
 */
struct __nxml_doctype_attribute_attdef_t {
  char *name;

  __nxml_doctype_attribute_attdef_type_t type;

  __nxml_doctype_attribute_attdef_value_t value;
  char *fixed_value;

  __nxml_doctype_attribute_attdef_list_t *list;

  __nxml_doctype_attribute_attdef_t *next;
};

/** Data struct private for doctype parsing of attributes 
 *
 * \brief
 * Data struct private for doctype parsing of attributes
 */
struct __nxml_doctype_attribute_t {
  char *element;

  __nxml_doctype_attribute_attdef_t *attdef;
  __nxml_doctype_attribute_t *next;
};

/** Data struct private for doctype parsing of entities
 *
 * \brief
 * Data struct private for doctype parsing of entities
 */
struct __nxml_doctype_entity_t {
  int percent;
  char *name;
  char *reference;
  char *system;
  char *pubid;
  char *ndata;
  __nxml_doctype_entity_t *next;
};

/** Data struct private for doctype parsing of notations
 *
 * \brief
 * Data struct private for doctype parsing of notations
 */
struct __nxml_doctype_notation_t {
  char *system_literal;
  char *pubid_literal;

  __nxml_doctype_notation_t *next;
};

/** Data struct for namespace
 *
 * \brief
 * Data struct for namespace
 */
struct nxml_namespace_t {
  char *prefix;
  char *ns;
  nxml_namespace_t *next;
};

/** Data struct private for internal use only
 *
 * \brief
 * Data struct private for internal use only
 */
struct __nxml_private_t
{
  void (*func) (char *, ...);
  int line;
  int timeout;

  __nxml_doctype_element_t *elements;
  __nxml_doctype_attribute_t *attributes;
  __nxml_doctype_entity_t *entities;
  __nxml_doctype_notation_t *notations;
};

/** Principal data struct. It contains pointers to any other structures.
 *
 * \brief 
 * Principal data struct. It contains pointers to any other structures */
struct nxml_t
{

  char *file;	/**< XML document filename or url */
  size_t size;	/**< Size of XML document in byte */

  nxml_version_t version;	/**< XML document version */
  int standalone;		/**< This document is standalone ? */
  char *encoding;		/**< Encoding type */

  nxml_charset_t charset_detected;	/**< charset detected when the a
					  XML document is parsed. The document
					  will be convert to UTF-8 */

  nxml_data_t *data;	/**< The data of XML document */
  nxml_doctype_t *doctype; /**< The doctype of XML document */

  __nxml_private_t priv;  /**< For internal use only */
};

/* INIT FUNCTIONS ************************************************************/

/**
 * This function creates a new nxml_t element.
 *
 * \param nxml Pointer to a nxml_t element. It will be allocted.
 * \return the error code
 */
nxml_error_t	nxml_new		(nxml_t ** nxml);

/** 
 * This function creates a new nxml_data_t child of a parent in the data 
 * struct. If parent is NULL the child will be created in the root level
 * of XML document.
 *
 * \param nxml Pointer to a nxml_t data struct.
 * \param parent The parent of new data struct child. If it is NULL, the
 * child is in the root level.
 * \param child It is the pointer to the new data struct. If *child is NULL,
 * it will be allocated, else it will be insert as it is.
 * \return the error code
 *
 * \code
 * nxml_data_t *data1, *data2;
 * data1=NULL;
 * nxml_add(nxml, NULL, &data1);
 *
 * data2=(nxml_data_t *)malloc(sizeof(nxml_data_t));
 * nxml_add(nxml, NULL, &data2);
 * \endcode
 */
nxml_error_t	nxml_add		(nxml_t * nxml,
					 nxml_data_t *parent,
					 nxml_data_t **child);

/** 
 * This function removes a nxml_data_t child to a parent in the data 
 * struct. If parent is NULL the child will be removed in the root level of
 * XML document. This function doesn't free the child. If you want you can
 * reinsert the child in another parent tree or use the nxml_free_data 
 * function.
 *
 * \param nxml Pointer to a nxml_t data struct.
 * \param parent The parent of data struct child. If it is NULL, the
 * child will be searched in the root level.
 * \param child It is the pointer to the child that you want remove
 * \return the error code
 */
nxml_error_t	nxml_remove		(nxml_t * nxml,
					 nxml_data_t *parent,
					 nxml_data_t *child);

/** 
 * This function creates a new nxml_attr_t data of a element in the data 
 * struct.
 *
 * \param nxml Pointer to a nxml_t data struct.
 * \param element The element of the new data struct attribute.
 * \param attribute The pointer to the your data struct. If it is NULL it will
 * be allocated, else no.
 * \return the error code
 */
nxml_error_t	nxml_add_attribute	(nxml_t *nxml,
					 nxml_data_t *element,
					 nxml_attr_t **attribute);

/**
 * This function removes a nxml_attr_t data of a element. It does not free it
 * so you can reinsert o free it with nxml_free_attribute.
 *
 * \param nxml Pointer to a nxml_t data struct.
 * \param element The element that contains the attribute
 * \param attribute The attribute that you want remove.
 * \return the error code
 */
nxml_error_t	nxml_remove_attribute	(nxml_t *nxml,
					 nxml_data_t *element,
					 nxml_attr_t *attribute);

/**
 * This function adds a nxml_namespace_t data in a nxml document.
 *
 * \param nxml Pointer to a nxml_t data struct.
 * \param element The element of the new data struct namespace.
 * \param ns The namespace that you want add
 * \return the error code
 */
nxml_error_t	nxml_add_namespace	(nxml_t *nxml,
		                         nxml_data_t *element,
					 nxml_namespace_t **ns);

/**
 * This function removes a nxml_namespace_t data in a nxml document.
 *
 * \param nxml Pointer to a nxml_t data struct.
 * \param element The element of the new data struct namespace.
 * \param ns The namespace that you want remove
 * \return the error code
 */
nxml_error_t	nxml_remove_namespace	(nxml_t *nxml,
		                         nxml_data_t *element,
					 nxml_namespace_t *ns);

/**
 * This function sets the output function. If you set a your function the
 * parser write the error with this function. As default there is not a
 * function. If you want tou can set 'nxml_print_general' function that
 * print to stderr.
 *
 * \param nxml The struct create with nxml_new.
 * \param func Your function. If you don't want the function, set it to NULL.
 * As default a nxml_t element has not a output function.
 * \return the error code
 */
nxml_error_t	nxml_set_func		(nxml_t * nxml,
					 void (*func) (char *, ...));

void		nxml_print_generic	(char *, ...);

/**
 * This function sets the timeout in seconds for the download of a remote
 *  XML document. Default is 0 and 0 is no timeout.
 */
nxml_error_t	nxml_set_timeout	(nxml_t * nxml,
					 int seconds);

/* PARSER FUNCTIONS *********************************************************/

/**
 * This function parses a url. It downloads a url with curl library and
 * parses it.
 *
 * \param nxml the struct create with nxml_new.
 * \param url the url that you want parse.
 * \return the error code
 */
nxml_error_t	nxml_parse_url		(nxml_t * nxml,
					 char *url);

/** 
 * This function parses a file.
 *
 * \param nxml the struct create with nxml_new.
 * \param file the file that you want parse.
 * \return the error code
 */
nxml_error_t	nxml_parse_file		(nxml_t * nxml,
					 char *file);

/** 
 * This function parses a buffer in memory.
 *
 * \param nxml the struct create with nxml_new.
 * \param buffer the buffer that you want parse.
 * \param size the size of buffer. If size is 0, the function checks the 
 * length of your buffer searching a '\\0'.
 * \return the error code
 */
nxml_error_t	nxml_parse_buffer	(nxml_t * nxml,
					 char *buffer,
					 size_t size);

/* DTD FUNCTIONS ************************************************************/

/**
 * This function valids a XML document with its DTD.
 *
 * \param nxml the struct create with nxml_new.
 * \param flag is a attribute for this function. Use some enum like
 * NXML_DOCTYPEFLAG_DOWNLOAD | NXML_DOCTYPEFLAG_RECURSIVE
 * \return the error code
 */
nxml_error_t	nxml_valid_dtd		(nxml_t * nxml,
					 int flag);

/**
 * This function parses a remote DTD document and checks if the nxml_t
 * document is valid or not.
 *
 * \param nxml the struct of your XML Document
 * \param url the url that you want parse.
 * \param flag is a attribute for this function. Use some enum like
 * NXML_DOCTYPEFLAG_DOWNLOAD | NXML_DOCTYPEFLAG_RECURSIVE
 * \return the error code
 */
nxml_error_t	nxml_dtd_parse_url	(nxml_t * nxml,
					 char *url,
					 int flag);

/** 
 * This function parses a local DTD document and checks if the nxml_t
 * document is valid or not.
 *
 * \param nxml the struct of your XML Document
 * \param file the file that you want parse.
 * \param flag is a attribute for this function. Use some enum like
 * NXML_DOCTYPEFLAG_DOWNLOAD | NXML_DOCTYPEFLAG_RECURSIVE
 * \return the error code
 */
nxml_error_t	nxml_dtd_parse_file	(nxml_t * nxml,
					 char *file,
					 int flag);

/** 
 * This function parses a buffer in memory as a DTD document and checks
 * if the nxml_t document is valid or not.
 *
 * \param nxml the struct of your XML Document
 * \param buffer the buffer that you want parse.
 * \param size the size of buffer. If size is 0, the function checks the 
 * length of your buffer searching a '\\0'.
 * \param flag is a attribute for this function. Use some enum like
 * NXML_DOCTYPEFLAG_DOWNLOAD | NXML_DOCTYPEFLAG_RECURSIVE
 * \return the error code
 */
nxml_error_t	nxml_dtd_parse_buffer	(nxml_t * nxml,
					 char *buffer,
					 size_t size,
					 int flag);

/* WRITE FUNCTIONS **********************************************************/

/**
 * This function writes the data struct in a local file.
 * \param nxml the nxml data strut
 * \param file the local file
 * \return the error code
 */
nxml_error_t	nxml_write_file		(nxml_t *nxml,
					 char *file);

/**
 * This function writes the data struct in a buffer.
 * \code
 * char *buffer;
 * buffer=NULL; // This is important!
 * nxml_write_buffer(nxml, &buffer);
 * \endcode
 *
 * The buffer must be NULL.
 *
 * \param nxml
 * \param buffer the memory buffer
 * \return the error code
 */
nxml_error_t	nxml_write_buffer	(nxml_t *nxml,
					 char **buffer);

/* FREE FUNCTIONS ************************************************************/

/**
 * This function removes the data in a structure nxml_t and makes it clean for
 * another usage.
 * \param nxml the pointer to you data struct.
 * \return the error code.
 */
nxml_error_t	nxml_empty		(nxml_t * nxml);

/** 
 * This function frees the memory of a nxml_t *element. After the free,
 * your data struct is not useful. If you want erase the internal data, use
 * nxml_empty function
 *
 * \param nxml the pointer to your data struct.
 * \return the error code.
 */
nxml_error_t	nxml_free		(nxml_t * nxml);

/**
 * This function removes the data in a structure nxml_doctype_t and makes 
 * it clean for another usage (another parsing action).
 *
 * \param doctype the pointer to you data struct.
 * \return the error code.
 */
nxml_error_t	nxml_empty_doctype	(nxml_doctype_t * doctype);

/** 
 * This function frees the memory of a nxml_doctype_t *element. After the free,
 * your data struct is not useful. If you want erase the internal data, use
 * nxml_empty_doctype function
 *
 * \param doctype the pointer to you data struct.
 * \return the error code.
 */
nxml_error_t	nxml_free_doctype	(nxml_doctype_t *doctype);

/**
 * This function frees the memory of a nxml_data_t *element and any its
 * children and its attributes.
 *
 * \param data the pointer to you data struct.
 * \return the error code
 */
nxml_error_t	nxml_free_data		(nxml_data_t *data);

/**
 * This function frees the memory of a nxml_attr_t *element.
 *
 * \param data the pointer to you data struct.
 * \return the error code
 */
nxml_error_t	nxml_free_attribute	(nxml_attr_t *data);

/**
 * This function frees the memory of a nxml_namespace_t *element.
 *
 * \param data the pointer to you data struct.
 * \return the error code
 */
nxml_error_t	nxml_free_namespace	(nxml_namespace_t *data);

/* EDIT FUNCTIONS ***********************************************************/

/**
 * This function returns the root element of xml data struct.
 * \code
 * nxml_t *nxml;
 * nxml_data_t *root;
 *
 * nxml_new(&nxml);
 * nxml_parser_file(nxml, "file.xml");
 * nxml_root_element(nxml, &root);
 * printf("%p\n",root);
 * nxml_free(nxml);
 * \endcode
 *
 * \param nxml the data struct
 * \param element the pointer to your nxml_data_t struct
 * \return the error code
 */
nxml_error_t	nxml_root_element	(nxml_t *nxml,
					 nxml_data_t **element);

/**
 * This function searchs the request element in the children of the data struct.
 * \code
 * nxml_t *nxml;
 * nxml_data_t *root;
 *
 * nxml_new(&nxml);
 * nxml_parser_file(nxml, "file.xml");
 * nxml_find_element(nxml, NULL, "hello_world", &root);
 * printf("%p\n",root);
 * nxml_free(nxml);
 * \endcode
 *
 * \param nxml the data struct
 * \param parent the data struct nxml_data_t of parent. If it is NULL, this 
 * function searchs in the root element level.
 * \param name the name of the node that you want.
 * \param element the pointer to your nxml_data_t struct. If element will be
 * NULL, the item that you want does not exist.
 * \return the error code
 */
nxml_error_t	nxml_find_element	(nxml_t *nxml,
					 nxml_data_t *parent,
					 char *name, 
					 nxml_data_t **element);

/**
 * This function searchs the first doctype element in the nxml_t document.
 *
 * \param nxml the data struct
 * \param doctype the pointer to your nxml_doctype_t struct. If element will be
 * NULL, the item that you want does not exist.
 * \return the error code
 */
nxml_error_t	nxml_doctype_element	(nxml_t *nxml,
					 nxml_doctype_t **doctype);

/**
 * This function searchs the request attribute and returns its values.
 * \code
 * nxml_t *nxml;
 * nxml_data_t *root;
 *
 * nxml_new(&nxml);
 * nxml_parser_file(nxml, "file.xml");
 * nxml_find_element(nxml, NULL, "hello_world", &root);
 * if(root) {
 *   char *str;
 *   nxml_find_attribute(root, "attribute", &str);
 *   printf("%s\n",attribute);
 *   free(str);
 * }
 * nxml_free(nxml);
 * \endcode
 *
 * \param data the data struct
 * \param name the attribute that you want search
 * \param attribute the pointer to your nxml_attr_t struct. If attribute will
 * be NULL, the attribute that you want does not exist.
 * does not exist.
 * \return the error code
 */
nxml_error_t	nxml_find_attribute	(nxml_data_t *data,
					 char *name, 
					 nxml_attr_t **attribute);

/**
 * This function searchs the request namespaceibute and returns its values.
 *
 * \param data the data struct
 * \param name the namespace that you want search
 * \param ns the pointer to your nxml_attr_t struct. If namespace will
 * be NULL, the namespace that you want does not exist.
 * does not exist.
 * \return the error code
 */
nxml_error_t	nxml_find_namespace	(nxml_data_t *data,
					 char *name, 
					 nxml_namespace_t **ns);

/**
 * This function returns the string of a XML element.
 * \code
 * nxml_t *nxml;
 * nxml_data_t *root;
 * char *str;
 *
 * nxml_new(&nxml);
 * nxml_parser_file(nxml, "file.xml");
 * nxml_find_element(nxml, NULL, "hello_world", &root);
 * if(root) {
 *   nxml_get_string(root, &str);
 *   if(str) {
 *     printf("Hello_world item contains: %s\n",str);
 *     free(str);
 *   }
 * }
 * nxml_free(nxml);
 * \endcode
 *
 * \param element the xnml_data_t pointer
 * \param string the pointer to you char *. You must free it after usage.
 * \return the error code
 */
nxml_error_t	nxml_get_string		(nxml_data_t *element,
					 char **string);

/* ERROR FUNCTIONS **********************************************************/

/**
 * This function returns a static string with the description of error code
 * \param err the error code that you need as string
 * \return a string. Don't free this string!
 */
char *		nxml_strerror		(nxml_error_t err);

/**
 * This function return the line of a error of parse.
 * \param nxml the pointer to data struct
 * \param line pointer to your integer. In this pointer will be set the line.
 * \return the error code
 */
nxml_error_t	nxml_line_error		(nxml_t * nxml,
					 int *line);

/* EASY FUNCTIONS ***********************************************************/

/**
 * This function returns a new nxml_t data.
 *
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the pointer to a new nxml_t data. If it returns NULL, read the err
 * code. This function use nxml_set_func with nxml_print_generic so the
 * error will be write in the standard output.
 */
nxml_t *	nxmle_new_data		(nxml_error_t *err);

/**
 * This function returns a new nxml_t data and parses a remote url document
 * from http or ftp protocol. This function use nxml_set_func with 
 * nxml_print_generic so the error will be write in the standard output.
 *   
 *
 * \param url the url that you want parse.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the pointer to a new nxml_t data.
 */
nxml_t *	nxmle_new_data_from_url	(char *url,
					 nxml_error_t *err);

/**
 * This function returns a new nxml_t data and parses a local file. This 
 * function use nxml_set_func with nxml_print_generic so the error will be
 * write in the standard output.
 *
 * \param file the file that you want parse.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the pointer to a new nxml_t data.
 */
nxml_t *	nxmle_new_data_from_file
					(char *file,
					 nxml_error_t *err);

/**
 * This function returns a new nxml_t data and parses a buffer. This
 * function use nxml_set_func with nxml_print_generic so the error will be
 * write in the standard output.
 *
 * \param buffer the buffer that you want parse.
 * \param size the size of buffer. If size is 0, the function checks the 
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the pointer to a new nxml_t data.
 */
nxml_t *	nxmle_new_data_from_buffer
					(char *buffer,
					 size_t size,
					 nxml_error_t *err);

/**
 * This function creates and adds a child nxml_data_t to a parent in your 
 * nxml data struct.
 *
 * \param nxml Pointer to your nxml data.
 * \param parent The parent of new data struct child. If it is NULL, the
 * child is in the root level.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the pointer to a new nxml_data_t data child.
 */
nxml_data_t *	nxmle_add_new		(nxml_t * nxml,
					 nxml_data_t *parent,
					 nxml_error_t *err);

/**
 * This function adds a your nxml_data_t to a parent in your nxml
 * data struct.
 *
 * \param nxml Pointer to your nxml data.
 * \param parent The parent of new data struct child. If it is NULL, the
 * child is in the root level.
 * \param child The you child nxml_data_t struct that you want insert.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the pointer to a new nxml_data_t data child.
 */
nxml_data_t *	nxmle_add_data		(nxml_t * nxml,
					 nxml_data_t *parent,
					 nxml_data_t *child,
					 nxml_error_t *err);

/**
 * This function creates and adds an attribute nxml_attr_t data to a
 * nxml_data_t struct in your nxml data struct.
 *
 * \param nxml Pointer to your nxml data.
 * \param element The parent of new nxml_attr_t struct.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the pointer to a new nxml_data_t data child.
 */
nxml_attr_t *	nxmle_add_attribute_new	(nxml_t *nxml,
					 nxml_data_t *element,
					 nxml_error_t *err);

/**
 * This function adds an attribute nxml_attr_t data to a
 * nxml_data_t struct in your nxml data struct.
 *
 * \param nxml Pointer to your nxml data.
 * \param element The parent of your nxml_attr_t struct.
 * \param attribute Your attribute element.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the pointer to a new nxml_data_t data child.
 */
nxml_attr_t *	nxmle_add_attribute_data
					(nxml_t *nxml,
					 nxml_data_t *element,
					 nxml_attr_t *attribute,
					 nxml_error_t *err);

/**
 * This function creates and adds a namespace nxml_namespace_t data to a
 * nxml data struct.
 *
 * \param nxml Pointer to your nxml data.
 * \param element The element of in witch you want add the namespace.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the pointer to a new nxml_data_t data child.
 */
nxml_namespace_t * nxmle_add_namespace_new
					(nxml_t *nxml,
					 nxml_data_t *element,
	       				 nxml_error_t *err);

/**
 * This function adds an namespace nxml_namespace-t data to a nxml data struct.
 *
 * \param nxml Pointer to your nxml data.
 * \param element The element of in witch you want add the namespace.
 * \param ns Your namespace element.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the pointer to a new nxml_data_t data child.
 */
nxml_namespace_t * nxmle_add_namespace_data
					(nxml_t *nxml,
					 nxml_data_t *element,
					 nxml_namespace_t *ns,
					 nxml_error_t *err);

/**
 * This function returns the root element of a nxml_t.
 *
 * \param nxml Pointer to your nxml data.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the pointer to the root element. If NULL the element does not
 * exist.
 */
nxml_data_t *	nxmle_root_element	(nxml_t *nxml,
					 nxml_error_t *err);

/**
 * This function returns the first doctype element of a nxml_t.
 *
 * \param nxml Pointer to your nxml data.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the pointer to the doctype element. If NULL the element does not
 * exist.
 */
nxml_doctype_t *nxmle_doctype_element	(nxml_t *nxml,
					 nxml_error_t *err);

/**
 * This function returns the nxml_data_t pointer to a element by
 * a name.
 *
 * \param nxml Pointer to your nxml data.
 * \param parent Pointer to your nxml_data_t parent. If it is NULL, this
 * function searchs in a root element level.
 * \param name The name of element that you want.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the pointer to the root element. If NULL the element does not
 * exist.
 */
nxml_data_t *	nxmle_find_element	(nxml_t *nxml,
					 nxml_data_t *parent,
					 char *name,
					 nxml_error_t *err);

/**
 * This function returns the value of a attribute by a name.
 *
 * \param element Pointer to your nxml_data_t.
 * \param name The name of attribute that you want.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return a pointer to a char allocated so you must free it after usage. If
 * it is NULL, the attribute does not exist.
 */
char *		nxmle_find_attribute	(nxml_data_t *element,
					 char *name,
					 nxml_error_t *err);

/**
 * This function returns the value of a namespace by a name.
 *
 * \param element Pointer to your nxml_data_t.
 * \param name The name of namespace that you want.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return a pointer to a char allocated so you must free it after usage. If
 * it is NULL, the namespace does not exist.
 */
char *		nxmle_find_namespace	(nxml_data_t *element,
					 char *name,
					 nxml_error_t *err);

/**
 * This function returns the contain of a element.
 *
 * \param element Pointer to your nxml_data_t.
 * \param err If err is not NULL, err will be set to the error flag.
 * \return a pointer to a char allocated so you must free it after usage. If
 * it is NULL, the attribute does not exist.
 */
char *		nxmle_get_string	(nxml_data_t *element,
					 nxml_error_t *err);

/**
 * This function writes the data struct in a buffer.
 *
 * \param nxml
 * \param err If err is not NULL, err will be set to the error flag.
 * \return a pointer to a char allocated so you must free it after usage.
 */
char *		nxmle_write_buffer	(nxml_t *nxml, nxml_error_t *err);

/**
 * This function return the line of a error of parse.
 * \param nxml the pointer to data struct
 * \param err If err is not NULL, err will be set to the error flag.
 * \return the line with the error.
 */
int		nxmle_line_error	(nxml_t * nxml, nxml_error_t *err);

/* Easy functions defined: */
#define		nxmle_remove		nxml_remove
#define		nxmle_remove_attribute	nxml_remove_attribute
#define		nxmle_remove_namespace	nxml_remove_namespace
#define		nxmle_write_file	nxml_write_file

#define		nxmle_empty		nxml_empty
#define		nxmle_free		nxml_free
#define		nxmle_free_data		nxml_free_data
#define		nxmle_free_attribute	nxml_free_attribute

#define		nxmle_strerror		nxml_strerror

#ifdef  __cplusplus
}
#endif

#endif

/* EOF */
