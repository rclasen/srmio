/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include "common.h"

#include <stdarg.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#include <ctype.h>

const char *srmio_version = PACKAGE_STRING;

void srmio_dumphexv(FILE *fh, const unsigned char *buf, size_t blen,
	const char *fmt, va_list ap )
{
	struct timeval tv;
	size_t i;

	assert( ! blen || buf );
	assert( fmt );

	if( ! fh )
		return;

	if( 0 != gettimeofday( &tv, NULL ))
		return;

	fprintf( fh, "%lu.%03lu ", tv.tv_sec, (unsigned long)tv.tv_usec / 1000 );
	vfprintf( fh, fmt, ap );
	fprintf( fh, " %d: ", blen );

	for( i=0; i < blen; ++i ){
		unsigned char c = buf[i];
		unsigned char p = c;

		if( ! isprint(c) )
			p=' ';

		if( i )
			fprintf( fh, " " );
		fprintf( fh, "0x%02x/%c", c, p );
	}
	fprintf( fh, "\n" );
}

void srmio_dumphex(FILE *fh, const unsigned char *buf, size_t blen,
	const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	srmio_dumphexv( fh, buf, blen, fmt, ap );
	va_end( ap );
}

void srmio_debugv(FILE *fh, const char *fmt, va_list ap )
{
	struct timeval tv;

	assert(fmt);

	if( ! fh )
		return;

	if( 0 != gettimeofday( &tv, NULL ))
		return;

	fprintf( fh, "%lu.%03lu ", tv.tv_sec, (unsigned long)tv.tv_usec / 1000 );
	vfprintf( fh, fmt, ap );
	fprintf( fh, "\n" );
}

void srmio_debug(FILE *fh, const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	srmio_debugv( fh, fmt, ap );
	va_end( ap );
}
