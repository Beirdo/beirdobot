/* mRss - Copyright (C) 2005-2006 bakunin - Andrea Marchesini 
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

#ifndef __M_RSS_H__
#define __M_RSS_H__

#include <sys/types.h>

typedef struct mrss_t mrss_t;
typedef struct mrss_item_t mrss_item_t;
typedef struct mrss_category_t mrss_category_t;
typedef struct mrss_hour_t mrss_hour_t;
typedef struct mrss_day_t mrss_day_t;
typedef void * mrss_generic_t;

/** This enum describes the error type of libmrss */
typedef enum {
  MRSS_OK = 0,			/**< No error */
  MRSS_ERR_POSIX,		/**< For the correct error, use errno */
  MRSS_ERR_PARSER,		/**< Parser error */
  MRSS_ERR_VERSION,		/**< The RSS has a no compatible VERSION */
  MRSS_ERR_DATA			/**< The parameters are incorrect */
} mrss_error_t;

typedef enum {
  MRSS_VERSION_0_91,		/**< 0.91 RSS version */
  MRSS_VERSION_0_92,		/**< 0.92 RSS version */
  MRSS_VERSION_1_0,		/**< 1.0 RSS version */
  MRSS_VERSION_2_0,		/**< 2.0 RSS version */
} mrss_version_t;

/** Flag list for mrss_set and mrss_get functions */
typedef enum {
  /* Generic */

  /** Set the ersion to a mrss_t element - the value is a mrss_version_t enum */
  MRSS_FLAG_VERSION = 1,

  /** Set the title to a mrss_t element - the value is a string */
  MRSS_FLAG_TITLE,
  /** Set the description to a mrss_t element - the value is a string */
  MRSS_FLAG_DESCRIPTION,
  /** Set the link to a mrss_t element - the value is a string */
  MRSS_FLAG_LINK,
  /** Set the language to a mrss_t element - the value is a string */
  MRSS_FLAG_LANGUAGE,
  /** Set the rating to a mrss_t element - the value is a string */
  MRSS_FLAG_RATING,
  /** Set the copyright to a mrss_t element - the value is a string */
  MRSS_FLAG_COPYRIGHT,
  /** Set the pubDate to a mrss_t element - the value is a string */
  MRSS_FLAG_PUBDATE,
  /** Set the lastBuildDate to a mrss_t element - the value is a string */
  MRSS_FLAG_LASTBUILDDATE,
  /** Set the docs to a mrss_t element - the value is a string */
  MRSS_FLAG_DOCS,
  /** Set the managingeditor to a mrss_t element - the value is a string */
  MRSS_FLAG_MANAGINGEDITOR,
  /** Set the webMaster to a mrss_t element - the value is a string */
  MRSS_FLAG_WEBMASTER,
  /** Set the generator to a mrss_t element - the value is a string */
  MRSS_FLAG_GENERATOR,
  /** Set the ttl to a mrss_t element - the value is a integer */
  MRSS_FLAG_TTL,
  /** Set the about to a mrss_t element - the value is a string */
  MRSS_FLAG_ABOUT,

  /* Image */

  /** Set the image_title to a mrss_t element - the value is a string */
  MRSS_FLAG_IMAGE_TITLE,
  /** Set the image_url to a mrss_t element - the value is a string */
  MRSS_FLAG_IMAGE_URL,
  /** Set the image_link to a mrss_t element - the value is a string */
  MRSS_FLAG_IMAGE_LINK,
  /** Set the image_width to a mrss_t element - the value is a integer */
  MRSS_FLAG_IMAGE_WIDTH,
  /** Set the image_height to a mrss_t element - the value is a integer */
  MRSS_FLAG_IMAGE_HEIGHT,
  /** Set the image_description to a mrss_t element - the value is a string */
  MRSS_FLAG_IMAGE_DESCRIPTION,

  /* TextInput */

  /** Set the textinput_title to a mrss_t element - the value is a string */
  MRSS_FLAG_TEXTINPUT_TITLE,
  /** Set the textinput_description to a mrss_t element - the value is a string */
  MRSS_FLAG_TEXTINPUT_DESCRIPTION,
  /** Set the textinput_name to a mrss_t element - the value is a string */
  MRSS_FLAG_TEXTINPUT_NAME,
  /** Set the textinput_link to a mrss_t element - the value is a string */
  MRSS_FLAG_TEXTINPUT_LINK,

  /* Cloud */

  /** Set the cloud to a mrss_t element - the value is a string */
  MRSS_FLAG_CLOUD,
  /** Set the cloud_domain to a mrss_t element - the value is a string */
  MRSS_FLAG_CLOUD_DOMAIN,
  /** Set the cloud_port to a mrss_t element - the value is a string */
  MRSS_FLAG_CLOUD_PORT,
  /** Set the cloud_path to a mrss_t element - the value is a integer */
  MRSS_FLAG_CLOUD_PATH,
  /** Set the cloud_registerProcedure to a mrss_t element - 
   * the value is a string */
  MRSS_FLAG_CLOUD_REGISTERPROCEDURE,
  /** Set the cloud_protocol to a mrss_t element - the value is a string */
  MRSS_FLAG_CLOUD_PROTOCOL,

  /* SkipHours */

  /** Set the hour to a mrss_hour_t element - the value is a string */
  MRSS_FLAG_HOUR,

  /* SkipDays */

  /** Set the day to a mrss_day_t element - the value is a string */
  MRSS_FLAG_DAY,

  /* Category or Item/Category */

  /** Set the category to a mrss_category_t element - the value is a string */
  MRSS_FLAG_CATEGORY,
  /** Set the domain to a mrss_category_t element - the value is a string */
  MRSS_FLAG_CATEGORY_DOMAIN,

  /* Item */

  /** Set the title to a mrss_item_t element - the value is a string */
  MRSS_FLAG_ITEM_TITLE,
  /** Set the link to a mrss_item_t element - the value is a string */
  MRSS_FLAG_ITEM_LINK,
  /** Set the description to a mrss_item_t element - the value is a string */
  MRSS_FLAG_ITEM_DESCRIPTION,
  /** Set the author to a mrss_item_t element - the value is a string */
  MRSS_FLAG_ITEM_AUTHOR,
  /** Set the comments to a mrss_item_t element - the value is a string */
  MRSS_FLAG_ITEM_COMMENTS,
  /** Set the pubDate to a mrss_item_t element - the value is a string */
  MRSS_FLAG_ITEM_PUBDATE,
  /** Set the guid to a mrss_item_t element - the value is a string */
  MRSS_FLAG_ITEM_GUID,
  /** Set the guid_isPermaLink to a mrss_item_t element - 
   * the value is a integer */
  MRSS_FLAG_ITEM_GUID_ISPERMALINK,
  /** Set the source to a mrss_item_t element - the value is a string */
  MRSS_FLAG_ITEM_SOURCE,
  /** Set the source_url to a mrss_item_t element - the value is a string */
  MRSS_FLAG_ITEM_SOURCE_URL,
  /** Set the enclosure to a mrss_item_t element - the value is a string */
  MRSS_FLAG_ITEM_ENCLOSURE,
  /** Set the enclosure_url to a mrss_item_t element - the value is a string */
  MRSS_FLAG_ITEM_ENCLOSURE_URL,
  /** Set the enclosure_length to a mrss_item_t element - 
   * the value is a integer */
  MRSS_FLAG_ITEM_ENCLOSURE_LENGTH,
  /** Set the enclosure_type to a mrss_item_t element - the value is a string */
  MRSS_FLAG_ITEM_ENCLOSURE_TYPE,

  /** Set the terminetor flag */
  MRSS_FLAG_END = 0

} mrss_flag_t;

/** Enum for the casting of the libmrss data struct */
typedef enum {
  /** The data struct is a mrss_t */
  MRSS_ELEMENT_CHANNEL,
  /** The data struct is a mrss_item_t */
  MRSS_ELEMENT_ITEM,
  /** The data struct is a mrss_hour_t */
  MRSS_ELEMENT_SKIPHOURS,
  /** The data struct is a mrss_day_t */
  MRSS_ELEMENT_SKIPDAYS,
  /** The data struct is a mrss_category_t */
  MRSS_ELEMENT_CATEGORY
} mrss_element_t;

/** Data struct for any items of RSS. It contains a pointer to the list
 * of categories. 
 *
 * \brief 
 * Struct data for item elements */
struct mrss_item_t {

  /** For internal use only: */
  mrss_element_t element;
  int allocated;

  /* Data: */

  				/*	0.91	0.92	1.0	2.0	*/
  char *title;			/*	R	O	O	O	*/
  char *link;			/*	R	O	O	O	*/
  char *description;		/*	R	O	-	O	*/

  char *author;			/*	-	-	-	O	*/
  char *comments;		/*	-	-	-	O	*/
  char *pubDate;		/*	-	-	-	O	*/
  char *guid;			/*	-	-	-	O	*/
  int guid_isPermaLink;		/*	-	-	-	O	*/

  char *source;			/*	-	O	-	O	*/
  char *source_url;		/*	-	R	-	R	*/

  char *enclosure;		/*	-	O	-	O	*/
  char *enclosure_url;		/*	-	R	-	R	*/
  int enclosure_length;		/*	-	R	-	R	*/
  char *enclosure_type;		/*	-	R	-	R	*/

  mrss_category_t *category;	/*	-	O	-	O	*/

  mrss_item_t *next;
};

/** Data struct for skipHours elements. 
 *
 * \brief 
 * Struct data for skipHours elements */
struct mrss_hour_t {
  /** For internal use only: */
  mrss_element_t element;
  int allocated;

  /* Data: */
  				/*	0.91	0.92	1.0	2.0	*/
  char *hour;			/*	R	R	-	R	*/
  mrss_hour_t *next;
};

/** Data struct for skipDays elements. 
 *
 * \brief 
 * Struct data for skipDays elements */
struct mrss_day_t {
  /** For internal use only: */
  mrss_element_t element;
  int allocated;

  /* Data: */
  				/*	0.91	0.92	1.0	2.0	*/
  char *day;			/*	R	R	-	R	*/
  mrss_day_t *next;
};

/** Data struct for category elements
 *
 * \brief 
 * Struct data for category elements */
struct mrss_category_t {
  /** For internal use only: */
  mrss_element_t element;
  int allocated;

  /* Data: */
  				/*	0.91	0.92	1.0	2.0	*/
  char *category;		/*	-	R	-	R	*/
  char *domain;			/*	-	O	-	O	*/
  mrss_category_t *next;
};

/** Principal data struct. It contains pointers to any other structures.
 *
 * \brief 
 * Principal data struct. It contains pointers to any other structures */
struct mrss_t {
  /** For internal use only: */
  mrss_element_t element;
  int allocated;

  /* Data: */

  char *file;
  size_t size;
  char *encoding;

  mrss_version_t version;	/*	0.91	0.92	1.0	2.0	*/

  char *title;			/*	R	R	R	R	*/
  char *description;		/*	R	R	R	R	*/
  char *link;			/*	R	R	R	R	*/
  char *language;		/*	R	O	-	O	*/
  char *rating;			/*	O	O	-	O	*/
  char *copyright;		/*	O	O	-	O	*/
  char *pubDate;		/*	O	O	-	O	*/
  char *lastBuildDate;		/*	O	O	-	O	*/
  char *docs;			/*	O	O	-	O	*/
  char *managingeditor;		/*	O	O	-	O	*/
  char *webMaster;		/*	O	O	-	O	*/
  char *generator;		/*	-	-	-	O	*/
  int ttl;			/*	-	-	-	O	*/
  char *about;			/*	-	-	R	-	*/
  
  /* Tag Image: */		/*	O	O	O	O	*/
  char *image_title;		/*	R	R	R	R	*/
  char *image_url;		/*	R	R	R	R	*/
  char *image_link;		/*	R	R	R	R	*/
  unsigned int image_width;	/*	O	O	-	O	*/
  unsigned int image_height;	/*	O	O	-	O	*/
  char *image_description;	/*	O	O	-	O	*/

  /* TextInput: */		/*	O	O	O	O	*/
  char *textinput_title;	/*	R	R	R	R	*/
  char *textinput_description;	/*	R	R	R	R	*/
  char *textinput_name;		/*	R	R	R	R	*/
  char *textinput_link;		/*	R	R	R	R	*/

  /* Cloud */
  char *cloud;			/*	-	O	-	O	*/
  char *cloud_domain;		/*	-	R	-	R	*/
  int cloud_port;		/*	-	R	-	R	*/
  char *cloud_path;		/*	-	R	-	R	*/
  char *cloud_registerProcedure;/*	-	R	-	R	*/
  char *cloud_protocol;		/*	-	R	-	R	*/

  mrss_hour_t *skipHours;	/*	O	O	-	O	*/
  mrss_day_t *skipDays;		/*	O	O	-	O	*/

  mrss_category_t *category;	/*	-	O	-	O	*/

  mrss_item_t *item;		/*	R	R	R	R	*/
};

/** PARSE FUNCTIONS *********************************************************/

/**
 * Parses a url and creates the data struct of the feed RSS url.
 * This function downloads your request if this is http or ftp.
 * \param url The url to be parsed
 * \param mrss the pointer to your data struct
 * \return the error code
 */
mrss_error_t	mrss_parse_url		(char *		url,
					 mrss_t **	mrss);

/** 
 * Parses a file and creates the data struct of the feed RSS url
 * \param file The file to be parsed
 * \param mrss the pointer to your data struct
 * \return the error code
 */
mrss_error_t	mrss_parse_file		(char *		file,
					 mrss_t **	mrss);

/** 
 * Parses a buffer and creates the data struct of the feed RSS url
 * \param buffer Pointer to the xml memory stream to be parsed
 * \param size_buffer The size of the array of char
 * \param mrss the pointer to your data struct
 * \return the error code
 */
mrss_error_t	mrss_parse_buffer	(char *		buffer,
					 size_t		size_buffer,
					 mrss_t **	mrss);

/** WRITE FUNCTIONS *********************************************************/

/** 
 * Writes a RSS struct data in a local file
 * \param mrss the rss struct data
 * \param file the local file
 * \return the error code
 */
mrss_error_t	mrss_write_file		(mrss_t *	mrss,
					 char *		file);

/**
 * Write a RSS struct data in a buffer.
 *
 * \code
 * char *buffer;
 * buffer=NULL; //<--- This is important!!
 * mrss_write_buffer (mrss, &buffer);
 * \endcode
 *
 * The buffer must be NULL.
 * \param mrss the rss struct data
 * \param buffer the buffer
 * \return the error code
 */
mrss_error_t	mrss_write_buffer	(mrss_t *	mrss,
					 char **	buffer);

/** FREE FUNCTION ***********************************************************/

/** 
 * This function frees any type of data struct of libmrss. If the element
 * is alloced by libmrss, it will be freed, else this function frees
 * only the internal data.
 *
 * \code
 * mrss_t *t=....;
 * mrss_item_t *item=...;
 *
 * mrss_free(t);
 * mrss_free(item);
 * \endcode
 *
 * \param element the data struct
 * \return the error code
 */
mrss_error_t	mrss_free		(mrss_generic_t	element);

/** GENERIC FUNCTION ********************************************************/

/** 
 * This function returns a static string with the description of error code
 * \param err the error code that you need as string
 * \return a string. Don't free this string!
 */
char *		mrss_strerror		(mrss_error_t	err);

/**
 * This function returns the mrss_element_t of a mrss data struct.
 * \param element it is the element that you want check
 * \param ret it is a pointer to a mrss_element_t. It will be sets.
 * \return the error code
 */
mrss_error_t	mrss_element		(mrss_generic_t	element,
					 mrss_element_t *ret);

/**
 * This function set the timeout for the download of URI.
 *
 * \param timeout if timeout is 0 libmrss doesn't use timeout. If timeout is
 * -1, the timeout will be set to 10 seconds, else timeout will be set to
 *  the value of this variable.
 * \return the error code
 */
mrss_error_t	mrss_set_timeout	(int		timeout);

/**
 * This function gets the timeout for the download of URI.
 *
 * \param timeout pointer to a int.
 * \return the error code
 */
mrss_error_t	mrss_get_timeout	(int *		timeout);

/**
 * This function returns the number of seconds sinze Jennuary 1st 1970 in the
 * UTC time zone, for the url that the urlstring parameter specifies.
 *
 * \param urlstring the url
 * \param lastmodified is a pointer to a time_t struct. The return value can
 * be 0 if the HEAD request does not return a Last-Modified value.
 * \return the error code
 */
mrss_error_t	mrss_get_last_modified	(char *		urlstring,
					 time_t *	lastmodified);

/** EDIT FUNCTIONS **********************************************************/

/** If you want create a new feed RSS from scratch, you need use
 * this function as the first.
 *
 * \code
 * mrss_t *d;
 * mrss_error_t err;
 * char *string;
 * int integer;
 *
 * d=NULL; // ->this is important! If d!=NULL, mrss_new doesn't alloc memory.
 * mrss_new(&d);
 *
 * err=mrss_set (d,
 * 		 MRSS_FLAG_VERSION, MRSS_VERSION_0_92,
 * 		 MRSS_FLAG_TITLE, "the title!",
 * 		 MRSS_FLAG_TTL, 12,
 * 		 MRSS_FLAG_END);
 *
 * if(err!=MRSS_OK) printf("%s\n",mrss_strerror(err));
 *
 * err=mrss_get (d,
 * 		 MRSS_FLAG_TITLE, &string,
 * 		 MRSS_FLAG_TTL, &integer,
 * 		 MRSS_FLAG_END);
 *
 * if(err!=MRSS_OK) printf("%s\n",mrss_strerror(err));
 * printf("The title is: '%s'\n", string);
 * printf("The ttl is: '%d'\n", integer);
 * free(string);
 * \endcode
 *
 * \param mrss is the pointer to the new data struct
 * \return the error code
 */
mrss_error_t	mrss_new		(mrss_t **	mrss);

/**
 * For insert/replace/remove a flags use this function as this example:
 * \code
 * mrss_set(mrss, MRSS_FLAG_TITLE, "hello world", MRSS_FLAG_END);
 * mrss_set(item, MRSS_FLAG_DESCRIPTION, NULL, MRSS_FLAG_END);
 * \endcode
 *
 * \param element it is the mrss data that you want changes the the next
 * list of elements. The list is composted by KEY - VALUES and as last
 * element MRSS_FLAG_END. The variable of value depends from key.
 * \see mrss_flag_t
 * \return the error code
 */
mrss_error_t	mrss_set		(mrss_generic_t	element,
					 ...);

/**
 * This function returns the request arguments. The syntax is the same of
 * mrss_set but the values of the list are pointer to data element (int *,
 * char **). If the key needs a char **, the value will be allocated.
 * \code
 * mrss_get(category, MRSS_FLAG_CATEGORY_DOMAIN, &string, MRSS_FLAG_END);
 * if(string) free(string);
 * \endcode
 * \param element it is any type of mrss data struct.
 * \return the error code
 */
mrss_error_t	mrss_get		(mrss_generic_t	element,
					 ...);

/**
 * This function adds an element to another element. For example you can
 * add a item to a channel, or a category to a item, and so on. Look this
 * example:
 * \code
 *  mrss_item_t *item = NULL;
 * mrss_hour_t *hour = NULL;
 * mrss_day_t day;              // If the element is no null, the function
 * mrss_category_t category,    // does not alloc it
 *
 * mrss_new_subdata(mrss, MRSS_ELEMENT_ITEM, &item);
 * mrss_new_subdata(mrss, MRSS_ELEMENT_SKIPHOURS, &hour);
 * mrss_new_subdata(mrss, MRSS_ELEMENT_SKIPDAYS, &day);
 * mrss_new_subdata(item, MRSS_ELEMENT_ITEM_CATEGORY, &category);
 * \endcode
 * \param element it is the parent element
 * \param subelement it is the type of the child (MRSS_ELEMENT_ITEM,
 * MRSS_ELEMENT_CATEGORY, ...)
 * \param subdata it is the pointer to the new struct. If the pointer
 * of *subdata exists, it will no alloced, else yes.
 * \return the error code
 * \see mrss_element_t
 */
mrss_error_t	mrss_new_subdata	(mrss_generic_t	element,
					 mrss_element_t	subelement,
					 mrss_generic_t	subdata);

/**
 * This function remove a subdata element. As first argoment you must specify
 * the parent, and second argoment the child.
 * \code
 * mrss_remove_subdata(mrss, item);
 * \endcode
 * \param element it is the parent
 * \param subdata the child that you want remove. Remember: 
 * mrss_remove_subdata does not free the memory. So you can remove a item
 * and reinsert it after.
 * \return the error code
 */
mrss_error_t	mrss_remove_subdata	(mrss_generic_t	element,
					 mrss_generic_t	subdata);

#endif

/* EOF */

