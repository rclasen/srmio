/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include "common.h"

/************************************************************
 * any endianess
 */

int8_t buf_get_int8( const unsigned char *buf, size_t pos )
{
	return buf[pos];
}

uint8_t buf_get_uint8( const unsigned char *buf, size_t pos )
{
	return buf[pos];
}


char *buf_get_string( const unsigned char *buf, size_t pos, size_t len )
{
	char *res;

	if( NULL == (res = malloc(len +1) ))
		return NULL;

	memcpy( res, (char*)&buf[pos], len );
	res[len] = 0;

	return res;
}


bool buf_set_uint8( unsigned char *buf, size_t pos, uint16_t x )
{
	if( x > UINT8_MAX ){
		errno = ERANGE;
		return false;
	}

	buf[pos] = x;

	return true;
}

bool buf_set_string( unsigned char *buf, size_t pos, const char *string, size_t max )
{
	size_t len;

	if( ! max ){
		errno = EINVAL;
		return false;
	}

	if( ! string ){
		buf[pos] = 0;
		return true;
	}

	len = strlen( string );
	memcpy( &buf[pos], string, len );

	if( len < max )
		memset( &buf[pos + len], 0, max - len );

	return true;
}


/************************************************************
 * little endian
 */

int16_t buf_get_lint16( const unsigned char *buf, size_t pos )
{
	return ((buf[pos+1] << 8) | buf[pos] );
}

int32_t buf_get_lint32( const unsigned char *buf, size_t pos )
{
	return ( (buf[pos+3] << 24)
		| (buf[pos+2] << 16)
		| (buf[pos+1] << 8)
		| buf[pos] );
}



uint16_t buf_get_luint16( const unsigned char *buf, size_t pos )
{
	return ((buf[pos+1] << 8) | buf[pos] );
}

uint32_t buf_get_luint32( const unsigned char *buf, size_t pos )
{
	return ( (buf[pos+3] << 24)
		| (buf[pos+2] << 16)
		| (buf[pos+1] << 8)
		| buf[pos] );
}





bool buf_set_lint16( unsigned char *buf, size_t pos, int32_t x )
{
	if( x > INT16_MAX || x < INT16_MIN ){
		errno = ERANGE;
		return false;
	}

	buf[pos] = x & 0xff;
	buf[pos+1] = (x >> 8) & 0xff;

	return true;
}

bool buf_set_lint32( unsigned char *buf, size_t pos, int64_t x )
{
	if( x > INT32_MAX || x < INT32_MIN ){
		errno = ERANGE;
		return false;
	}

	buf[pos] = x & 0xff;
	buf[pos+1] = (x >> 8) & 0xff;
	buf[pos+2] = (x >> 16) & 0xff;
	buf[pos+3] = (x >> 24) & 0xff;

	return true;
}


bool buf_set_luint16( unsigned char *buf, size_t pos, uint32_t x )
{
	if( x > UINT16_MAX ){
		errno = ERANGE;
		return false;
	}

	buf[pos] = x & 0xff;
	buf[pos+1] = (x >> 8) & 0xff;

	return true;
}

bool buf_set_luint32( unsigned char *buf, size_t pos, uint64_t x )
{
	if( x > UINT32_MAX ){
		errno = ERANGE;
		return false;
	}

	buf[pos] = x & 0xff;
	buf[pos+1] = (x >> 8) & 0xff;
	buf[pos+2] = (x >> 16) & 0xff;
	buf[pos+3] = (x >> 24) & 0xff;

	return true;
}


/************************************************************
 * big endian
 */

int16_t buf_get_bint16( const unsigned char *buf, size_t pos )
{
	return ((buf[pos] << 8) | buf[pos+1] );
}

uint16_t buf_get_buint16( const unsigned char *buf, size_t pos )
{
	return ((buf[pos] << 8) | buf[pos+1] );
}

uint32_t buf_get_buint24( const unsigned char *buf, size_t pos )
{
	return ((buf[pos] << 16)
		| (buf[pos+1] << 8)
		| buf[pos+2] );
}

uint32_t buf_get_buint32( const unsigned char *buf, size_t pos )
{
	return ((buf[pos] << 24)
		| (buf[pos+1] << 16)
		| (buf[pos+2] << 8)
		| buf[pos+3] );
}

bool buf_set_buint16( unsigned char *buf, size_t pos, uint32_t x )
{
	if( x > UINT16_MAX ){
		errno = ERANGE;
		return false;
	}

	buf[pos+1] = x & 0xff;
	buf[pos] = (x >> 8) & 0xff;

	return true;
}

bool buf_set_buint32( unsigned char *buf, size_t pos, uint64_t x )
{
	if( x > UINT32_MAX ){
		errno = ERANGE;
		return false;
	}

	buf[pos+3] = x & 0xff;
	buf[pos+2] = (x >> 8) & 0xff;
	buf[pos+1] = (x >> 16) & 0xff;
	buf[pos] = (x >> 24) & 0xff;

	return true;
}



