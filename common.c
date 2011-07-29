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

const char *srmio_version = PACKAGE_STRING;

void srmio_dumphexv(FILE *fh, const unsigned char *buf, size_t blen,
	const char *fmt, va_list ap )
{
	size_t i;

	assert( fh );
	assert( ! blen || buf );
	assert( fmt );

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
