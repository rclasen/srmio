/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _DEBUG_H
#define _DEBUG_H

#ifdef DEBUG

#define DPRINTF(x, ...)	fprintf( stderr, x "\n", ##__VA_ARGS__ );
void DUMPHEX( const char *prefix, const char *buf, size_t blen );

#else

#define DPRINTF(x, ...)	while(0);
#define DUMPHEX(x, ...) while(0);

#endif

#endif
