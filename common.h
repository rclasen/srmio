/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _COMMON_H
#define _COMMON_H

#ifndef DEBUG
/* suppress assert() code generation: */
#define NDEBUG
#endif

#include <assert.h>

#include "srmio.h"

#include <errno.h>
#include <stdio.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif

#ifdef HAVE_STRING_H
# if !defined STDC_HEADERS && defined HAVE_MEMORY_H
#  include <memory.h>
# endif
# include <string.h>
#endif

#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#ifdef VERBOSE
/* TODO: workaround for bad error reporting */
#define ERRMSG(x, ...)	fprintf( stderr, x "\n", ##__VA_ARGS__ );
#define STATMSG(x, ...)	fprintf( stderr, x "\n", ##__VA_ARGS__ );
#else
#define ERRMSG(x, ...)	while(0);
#define STATMSG(x, ...)	while(0);
#endif

/************************************************************
 *
 * from common.c
 *
 ************************************************************/

#ifdef DEBUG

#include <ctype.h>

#define DPRINTF(x, ...)	fprintf( stderr, "%s: " x "\n", __func__, ##__VA_ARGS__ );
#define DUMPHEX(prefix, buf, blen) dumphex(__func__, prefix, buf, blen );
void dumphex(const char *func, const char *prefix, const unsigned char *buf, size_t blen );

#else

#define DPRINTF(x, ...)	while(0);
#define DUMPHEX(x, ...) while(0);

#endif

/************************************************************
 *
 * from buf.c
 *
 ************************************************************/

/* any endianess */

char *buf_get_string( const unsigned char *buf, size_t pos, size_t len );

int8_t buf_get_int8( const unsigned char *buf, size_t pos );
uint8_t buf_get_uint8( const unsigned char *buf, size_t pos );

bool buf_set_string( unsigned char *buf, size_t pos, const char *string, size_t max );

bool buf_set_uint8( unsigned char *buf, size_t pos, uint16_t x );


/* little endian */

int16_t buf_get_lint16( const unsigned char *buf, size_t pos );
int32_t buf_get_lint32( const unsigned char *buf, size_t pos );

uint16_t buf_get_luint16( const unsigned char *buf, size_t pos );
uint32_t buf_get_luint32( const unsigned char *buf, size_t pos );


bool buf_set_lint16( unsigned char *buf, size_t pos, int32_t x );
bool buf_set_lint32( unsigned char *buf, size_t pos, int64_t x );

bool buf_set_luint16( unsigned char *buf, size_t pos, uint32_t x );
bool buf_set_luint32( unsigned char *buf, size_t pos, uint64_t x );

/* big endian */

int16_t buf_get_bint16( const unsigned char *buf, size_t pos );

uint16_t buf_get_buint16( const unsigned char *buf, size_t pos );
uint32_t buf_get_buint24( const unsigned char *buf, size_t pos );
uint32_t buf_get_buint32( const unsigned char *buf, size_t pos );

bool buf_set_buint16( unsigned char *buf, size_t pos, uint32_t x );
bool buf_set_buint32( unsigned char *buf, size_t pos, uint64_t x );

#endif /* _COMMON_H */
