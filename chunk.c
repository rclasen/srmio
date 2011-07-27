/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include "common.h"

/*
 * allocate and initialize new chunk
 *
 * on success pointer is returned
 * returns NULL on error and sets errno.
 */
srmio_chunk_t srmio_chunk_new( srmio_error_t *err )
{
	srmio_chunk_t tmp;

	if( NULL == (tmp = malloc( sizeof(struct _srmio_chunk_t)) )){
		srmio_error_errno( err, "new chunk" );
		return NULL;
	}

	memset( tmp, 0, sizeof(struct _srmio_chunk_t));
	return tmp;
}

/*
 * copy chunk to a newly allocated one.
 *
 * on success pointer is returned
 * returns NULL on error and sets errno.
 */
srmio_chunk_t srmio_chunk_clone( srmio_chunk_t chunk, srmio_error_t *err )
{
	srmio_chunk_t tmp;

	if( NULL == (tmp = malloc( sizeof(struct _srmio_chunk_t)) )){
		srmio_error_errno( err, "clone chunk" );
		return NULL;
	}

	memcpy( tmp, chunk, sizeof(struct _srmio_chunk_t));
	return tmp;
}

/*
 * free chunk memory
 */
void srmio_chunk_free( srmio_chunk_t chunk )
{
	free( chunk );
}




