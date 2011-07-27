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

void srmio_error_setv( srmio_error_t *err, const char *fmt, va_list ap )
{
#ifdef DEBUG
	va_list dp;

	fprintf( stderr, "set_error: " );
	va_copy( dp, ap );
	vfprintf( stderr, fmt, dp );
	va_end( dp );
	fprintf( stderr, "\n" );
#endif

	if( ! err )
		return;

	vsnprintf( err->message, SRMIO_ERROR_MSG_SIZE, fmt, ap );
}

void srmio_error_set( srmio_error_t *err, const char *fmt, ... )
{
	va_list ap;

	assert( fmt );

	va_start( ap, fmt );
	srmio_error_setv( err, fmt, ap );
	va_end( ap );
}


void srmio_error_copy( srmio_error_t *dst, srmio_error_t *src )
{
	assert( src );

	if( ! dst )
		return;

	strncpy( dst->message, src->message, SRMIO_ERROR_MSG_SIZE );
	dst->message[SRMIO_ERROR_MSG_SIZE-1] = 0;
}
