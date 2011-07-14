/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include "common.h"

#ifdef DEBUG
void dumphex( const char *func, const char *prefix, const unsigned char *buf, size_t blen )
{
	size_t i;

	if( blen < 1 )
		return;

	fprintf( stderr, "%s", func );
	if( prefix && strlen(prefix) )
		fprintf( stderr, " %s", prefix );
	fprintf( stderr, ": " );

	for( i=0; i < blen; ++i ){
		unsigned char c = buf[i];
		unsigned char p = c;

		if( ! isprint(c) )
			p=' ';

		if( i )
			fprintf( stderr, " " );
		fprintf( stderr, "0x%02x/%c", c, p );
	}
	fprintf( stderr, "\n" );
}
#endif



