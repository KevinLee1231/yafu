/*--------------------------------------------------------------------
This source distribution is placed in the public domain by its author,
Jason Papadopoulos. You may use it for any purpose, free of charge,
without having to notify anyone. I disclaim any responsibility for any
errors.

Optionally, please be nice and tell me if you find this source to be
useful. Again optionally, if you add to the functionality present here
please consider making those additions public too, so that others may 
benefit from your work.	

$Id: savefile.c 964 2014-05-03 03:30:03Z jasonp_sf $
--------------------------------------------------------------------*/

#include "savefile.h"
#include "util.h"
#include <stdlib.h>
#include <stdint.h>

/* we need a generic interface for reading and writing lines
   of data to the savefile while a factorization is in progress.
   This is necessary for two reasons: first, early msieve 
   versions would sometimes clobber their savefiles, and some
   users have several machines all write to the same savefile
   in a network directory. When output is manually buffered and 
   then explicitly flushed after writing to disk, most of the
   relations in the savefile will survive under these circumstances.

   Output is line-buffered through s->buf and explicitly flushed, and a
   .gz sibling is preferred over the plain file when zlib is available, so
   that savefiles stay usable even when a run is killed mid-factorization. */

#define SAVEFILE_BUF_SIZE 65536

/*--------------------------------------------------------------------*/
void savefile_init(savefile_t *s, char *savefile_name) {
	
	memset(s, 0, sizeof(savefile_t));
	
	s->name = "msieve.dat";
	if (savefile_name)
		s->name = savefile_name;

	s->buf = (char*)malloc((size_t)SAVEFILE_BUF_SIZE);
	if (s->buf == NULL) {
		printf("failed to allocate %u bytes\n", (size_t)SAVEFILE_BUF_SIZE);
		exit(-1);
	}
}

/*--------------------------------------------------------------------*/
void savefile_free(savefile_t* s) {

	free(s->buf);
	memset(s, 0, sizeof(savefile_t));
}

/*--------------------------------------------------------------------*/
void savefile_open(savefile_t* s, uint32_t flags) {

	char* open_string;
#ifndef NO_ZLIB
	char name_gz[256];
	struct stat dummy;
#endif

	if (flags & SAVEFILE_APPEND)
		open_string = "a";
	else if ((flags & SAVEFILE_READ) && (flags & SAVEFILE_WRITE))
		open_string = "r+w";
	else if (flags & SAVEFILE_READ)
		open_string = "r";
	else
		open_string = "w";

	s->is_a_FILE = s->isCompressed = 0;

#ifndef NO_ZLIB
	sprintf(name_gz, "%s.gz", s->name);
	if (stat(name_gz, &dummy) == 0) {
		if (stat(s->name, &dummy) == 0) {
			printf("error: both '%s' and '%s' exist. "
				"Remove the wrong one and restart\n",
				s->name, name_gz);
			exit(-1);
		}
		s->isCompressed = 1;
		s->fp = gzopen(name_gz, open_string);
		if (s->fp == NULL) {
			printf("error: cannot open '%s'\n", name_gz);
			exit(-1);
		}
		/* fprintf(stderr, "using compressed '%s'\n", name_gz); */
	}
	else if (flags & SAVEFILE_APPEND) {
		/* Unfortunately, append is not intuitive in zlib */
		/* Note: the .dat file may be a compressed file   */
		/*       we are using UNIX philosophy here:       */
		/*       it is the content, not filename, that matters */
		uint8 header[4];
		FILE* fp;
		int n;

		if ((fp = fopen(s->name, "r"))) {
			if ((n = fread(header, sizeof(uint8), 3, fp)) &&
				(n != 3 || header[0] != 31 || header[1] != 139 || header[2] != 8))
				s->is_a_FILE = 1;
			/* exists, non-empty and not gzipped,
			   so we will fopen a FILE to append plainly */
			fclose(fp);
		}
		if (s->is_a_FILE) {
			s->fp = (gzFile*)fopen(s->name, "a");
		}
		else {
			s->fp = gzopen(s->name, "a");
			s->isCompressed = 1;
		}
	}
	else
#endif
	{
		s->fp = gzopen(s->name, open_string);
	}
	if (s->fp == NULL) {
		printf("error: cannot open '%s'\n", s->name);
		exit(-1);
	}

	s->buf_off = 0;
	s->buf[0] = 0;
		}

/*--------------------------------------------------------------------*/
void savefile_close(savefile_t * s) {

	s->is_a_FILE ? fclose((FILE*)s->fp) : gzclose(s->fp);
	s->fp = NULL;
}

/*--------------------------------------------------------------------*/
uint32_t savefile_eof(savefile_t * s) {

	return (s->is_a_FILE ? feof((FILE*)s->fp) : gzeof(s->fp));
}

/*--------------------------------------------------------------------*/
uint32_t savefile_exists(savefile_t * s) {

	struct stat dummy;
	return (stat(s->name, &dummy) == 0);
}

/*--------------------------------------------------------------------*/
void savefile_read_line(char* buf, size_t max_len, savefile_t * s) {
	if (max_len == 0)
		return;

	gzgets(s->fp, buf, (int)max_len);
}

/*--------------------------------------------------------------------*/
void savefile_write_line(savefile_t * s, char* buf) {
	size_t bytes_left = strlen(buf);

	while (bytes_left > 0) {
		size_t available = (SAVEFILE_BUF_SIZE - 1) - s->buf_off;
		size_t chunk;

		if (available == 0) {
			savefile_flush(s);
			available = SAVEFILE_BUF_SIZE - 1;
		}

		chunk = bytes_left < available ? bytes_left : available;
		memcpy(s->buf + s->buf_off, buf, chunk);
		s->buf_off += (uint32_t)chunk;
		s->buf[s->buf_off] = 0;
		buf += chunk;
		bytes_left -= chunk;
	}
}

/*--------------------------------------------------------------------*/
void savefile_flush(savefile_t * s) {

	if (s->is_a_FILE) {
		fprintf((FILE*)s->fp, "%s", s->buf);
		fflush((FILE*)s->fp);
	}
	else {
		gzputs(s->fp, s->buf);
	}

	s->buf_off = 0;
	s->buf[0] = 0;
}

/*--------------------------------------------------------------------*/
void savefile_rewind(savefile_t * s) {

	s->is_a_FILE ? rewind((FILE*)s->fp) : gzrewind(s->fp);
}
