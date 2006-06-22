#include "nxml.h"
#include <stdarg.h>

int
main (int argc, char **argv)
{
  nxml_t *data;
  nxml_error_t ret;

  if (argc != 2)
    {
      fprintf (stderr,
	       "Usage:\n\t%s url_xml file\n\nExample:\n\t%s [file.xml|http://server/file.xml]\n\n",
	       argv[0], argv[0]);
      return 1;
    }

  printf ("Creating a new data struct...\n");
  if ((ret = nxml_new (&data)) != NXML_OK)
    {
      puts (nxml_strerror (ret));
      return 1;
    }

  printf ("Setting error function...\n");
  if ((ret = nxml_set_func (data, nxml_print_generic)) != NXML_OK)
    {
      puts (nxml_strerror (ret));
      nxml_free (data);
      return 1;
    }

  printf ("Parsing the document...\n");
  if (!strncmp (argv[1], "http://", 7))
    ret = nxml_parse_url (data, argv[1]);
  else
    ret = nxml_parse_file (data, argv[1]);

  if (ret != NXML_OK)
    {
      puts (nxml_strerror (ret));
      nxml_free (data);
      return 1;
    }

  printf ("Validating with the DTD...\n");
  if ((ret =
       nxml_valid_dtd (data,
		       NXML_DOCTYPEFLAG_DOWNLOAD |
		       NXML_DOCTYPEFLAG_RECURSIVE)) != NXML_OK)
    {
      puts (nxml_strerror (ret));
      nxml_free (data);
      return 1;
    }

  printf ("Freeing memory...\n");
  nxml_free (data);

  puts ("Bye!");
  return 0;
}
