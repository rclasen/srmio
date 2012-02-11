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

#ifdef DEBUG
void srmio_debugfunc( const char *msg, void *data )
{
	fprintf( stderr, "%s\n", msg );
}
#endif

void srmio_dumphexv(srmio_logfunc_t func, void *data,
	const unsigned char *buf, size_t blen,
	const char *fmt, va_list ap )
{
	struct timeval tv;
	size_t i;
	char msg[SRMIO_ERROR_MSG_SIZE];
	int len;
	int r;

	assert( ! blen || buf );
	assert( fmt );

#ifndef DEBUG
	if( ! func )
		return;
#endif

	if( 0 != gettimeofday( &tv, NULL ))
		return;

	len = snprintf( msg, SRMIO_ERROR_MSG_SIZE,
		"%lu.%03lu ", tv.tv_sec, (unsigned long)tv.tv_usec / 1000 );
	if( 0 > len )
		return;

	r = vsnprintf( msg+len, SRMIO_ERROR_MSG_SIZE - len, fmt, ap );
	if( 0 > r || r > SRMIO_ERROR_MSG_SIZE - len )
		return;
	len += r;

	r = snprintf( msg+len, SRMIO_ERROR_MSG_SIZE - len, " %d:", blen );
	if( 0 > r || r > SRMIO_ERROR_MSG_SIZE - len )
		return;
	len +=r;

	for( i=0; i < blen; ++i ){
		unsigned char c = buf[i];
		unsigned char p = c;

		if( ! isprint(c) )
			p=' ';

		r = snprintf( msg+len, SRMIO_ERROR_MSG_SIZE - len,
			" 0x%02x/%c", c, p );
		if( 0 > r || r > SRMIO_ERROR_MSG_SIZE - len )
			return;
		len +=r;
	}

#ifdef DEBUG
	fprintf( stderr, "%s\n", msg );
	if( func )
#endif
		(*func)( msg, data );
}

void srmio_dumphex(srmio_logfunc_t func, void *data,
	const unsigned char *buf, size_t blen,
	const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	srmio_dumphexv( func, data, buf, blen, fmt, ap );
	va_end( ap );
}

void srmio_debugv( srmio_logfunc_t func, void *data, const char *fmt, va_list ap )
{
	struct timeval tv;
	char msg[SRMIO_ERROR_MSG_SIZE];
	int len;

	assert(fmt);

#ifndef DEBUG
	if( ! func )
		return;
#endif

	if( 0 != gettimeofday( &tv, NULL ))
		return;

	len = snprintf( msg, SRMIO_ERROR_MSG_SIZE, "%lu.%03lu ",
		tv.tv_sec, (unsigned long)tv.tv_usec / 1000 );
	if( 0 > len )
		return;

	if( 0 > vsnprintf( msg+len, SRMIO_ERROR_MSG_SIZE - len, fmt, ap ))
		return;

#ifdef DEBUG
	fprintf( stderr, "%s\n", msg );
	if( func )
#endif
		(*func)( msg, data );
}

void srmio_debug( srmio_logfunc_t func, void *data, const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	srmio_debugv( func, data, fmt, ap );
	va_end( ap );
}
