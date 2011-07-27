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
 * allocate and initialize new marker
 *
 * on success pointer is returned
 * returns NULL on error and sets errno.
 */
srmio_marker_t srmio_marker_new( srmio_error_t *err )
{
	srmio_marker_t tmp;

	if( NULL == (tmp = malloc( sizeof(struct _srmio_marker_t)) )){
		srmio_error_errno( err, "marker new" );
		return NULL;
	}

	memset( tmp, 0, sizeof(struct _srmio_marker_t));
	return tmp;
}


/*
 * copy existing marker
 *
 * on success pointer is returned
 * returns NULL on error and sets errno.
 */
srmio_marker_t srmio_marker_clone( srmio_marker_t marker, srmio_error_t *err )
{
	srmio_marker_t tmp;

	if( NULL == (tmp = srmio_marker_new(err)))
		return NULL;

	tmp->first = marker->first;
	tmp->last = marker->last;

	if( marker->notes && NULL == (tmp->notes = strdup( marker->notes ))){
		srmio_error_errno( err, "marker clone notes" );
		goto clean1;
	}

	return tmp;

clean1:
	srmio_marker_free( tmp );
	return NULL;
}


/*
 * free marker memory
 */
void srmio_marker_free( srmio_marker_t block )
{
	if( ! block )
		return;

	free( block->notes );
	free( block );
}




