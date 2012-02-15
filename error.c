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
#ifdef HAVE_WINDOWS_H
# include <windows.h>
#endif


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

#ifdef HAVE_WINDOWS_H
void srmio_error_win( srmio_error_t *err, const char *fmt, ... )
{
	char buf[SRMIO_ERROR_MSG_SIZE];
	wchar_t msg[SRMIO_ERROR_MSG_SIZE+1];
	va_list ap;
	DWORD code;
	char converted[SRMIO_ERROR_MSG_SIZE+1];

	if( ! err )
		return;

	code = GetLastError();

	va_start( ap, fmt );
	vsnprintf( buf, SRMIO_ERROR_MSG_SIZE, fmt, ap );
	va_end ( ap );

	if( ! FormatMessage( FORMAT_MESSAGE_FROM_SYSTEM
		| FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&msg, SRMIO_ERROR_MSG_SIZE * sizeof(wchar_t),
		NULL ) ){

		DPRINTF( "FormatMessage failed, using error code" );
		srmio_error_set( err, "%s: %d", code );
		return;
	}

	if( ! WideCharToMultiByte( CP_ACP, 0,
		msg, -1, converted, SRMIO_ERROR_MSG_SIZE,
		NULL, NULL ) ){

		DPRINTF( "String conversion failed, using error code" );
		srmio_error_set( err, "%s: %d", code );
		return;
	}

	srmio_error_set( err, "%s: %s", buf, converted );
}
#endif /* HAVE_WINDOWS_H */


