/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */


#include "common.h"

#define LIST_ALLOC	1024


struct _srmio_list_t {
	void **list;
	size_t used;
	size_t alloc;
	srmio_list_closure cfunc;
};


void *srmio_list_new( srmio_list_closure cfunc )
{
	srmio_list_t list;

	if( NULL == (list = malloc( sizeof(struct _srmio_list_t) )))
		return NULL;

	srmio_list_init( list, cfunc );
	return list;
}

void srmio_list_init( srmio_list_t list, srmio_list_closure cfunc )
{
	assert(list);

	list->list = NULL;
	list->used = 0;
	list->alloc = 0;
	list->cfunc = cfunc;
}

void srmio_list_clear( srmio_list_t list )
{
	size_t i;

	assert(list);

	for( i = 0; i < list->used; ++i ){
		if( list->cfunc )
			(*list->cfunc)( list->list[i] );
		list->list[i] = NULL;
	}
	list->used = 0;
}

void srmio_list_free( srmio_list_t list )
{
	assert( list );

	srmio_list_clear( list );
	if( list->list )
		free( list->list );

	free( list );
}


bool srmio_list_add( srmio_list_t list, void *data )
{
	if( list->used >= list->alloc ){
		void **tmp;

		if( list->alloc > UINT_MAX - LIST_ALLOC ){
			errno = ERANGE;
			return false;
		}

		if( NULL == (tmp = realloc(list->list, sizeof( void* )
			* (list->alloc + LIST_ALLOC + 1 ))))

			return false;


		list->alloc += LIST_ALLOC;
		list->list = tmp;
	}

	list->list[list->used] = data;
	list->list[++ list->used] = NULL;
	return true;
}

size_t srmio_list_used( srmio_list_t list )
{
	assert( list );
	return list->used;
}

void **srmio_list( srmio_list_t list )
{
	assert( list );

	return list->list;
}


