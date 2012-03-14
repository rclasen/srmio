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
 * allocate and initialize new data structure to hold data retrieved
 * from PCV or file
 *
 * on success pointer is returned
 * returns NULL on error and sets errno.
 */
srmio_data_t srmio_data_new( srmio_error_t *err )
{
	srmio_data_t data;

	if( NULL == (data = malloc( sizeof( struct _srmio_data_t) )) ){
		srmio_error_errno( err, "new data" );
		return NULL;
	}
	memset( data, 0, sizeof(struct _srmio_data_t));

	if( NULL == (data->chunks = malloc(sizeof(srmio_chunk_t *)))){
		srmio_error_errno( err, "new data chunks" );
		goto clean1;
	}
	*data->chunks = NULL;

	if( NULL == (data->marker = malloc(sizeof(srmio_marker_t *)))){
		srmio_error_errno( err, "new data marker" );
		goto clean2;
	}
	*data->marker = NULL;

	return data;

clean2:
	free( data->chunks );

clean1:
	free( data );
	return NULL;
}

/*
 * copies "header" to a newly allocated data structure
 */
srmio_data_t srmio_data_header( srmio_data_t src, srmio_error_t *err )
{
	srmio_data_t dst;

	assert(src);

	if( NULL == ( dst = srmio_data_new( err ) ))
		return NULL;

	if( src->notes && NULL == ( dst->notes = strdup( src->notes ) ) ){
		srmio_error_errno( err, "data notes" );
		goto clean1;
	}

	if( src->athlete && NULL == ( dst->athlete = strdup( src->athlete ) ) ){
		srmio_error_errno( err, "data athlete");
		goto clean1;
	}

	dst->slope = src->slope;
	dst->zeropos = src->zeropos;
	dst->circum = src->circum;

	return dst;
clean1:
	srmio_data_free(dst);
	return NULL;
}


/*
 * add chunk to end of data's chunk list. Extends list when necessary.
 * Chunk is not copied.
 *
 * on success 0 is returned
 * returns -1 and sets errno on error
 */
bool srmio_data_add_chunkp( srmio_data_t data, srmio_chunk_t chunk, srmio_error_t *err )
{
	if( data->cused >= data->cavail ){
		srmio_chunk_t *tmp;

		if( data->cavail > UINT_MAX - 1001 ){
			srmio_error_set( err, "too many chunks" );
			return false;
		}

		if( NULL == (tmp = realloc( data->chunks,
			(data->cavail + 1001) * sizeof(srmio_chunk_t)))){
			srmio_error_errno( err, "enlarge data chunks" );
			return false;
		}

		data->cavail += 1000;
		data->chunks = tmp;
	}

	data->chunks[data->cused] = chunk;
	data->chunks[++ data->cused] = NULL;

	return true;
}

/*
 * add copy of chunk to end of data's chunk_list.
 *
 * on success 0 is returned
 * returns -1 and sets errno on error
 */
bool srmio_data_add_chunk( srmio_data_t data, srmio_chunk_t chunk, srmio_error_t *err )
{
	srmio_chunk_t nc;

	if( NULL == (nc = srmio_chunk_clone(chunk, err)))
		return false;
	return srmio_data_add_chunkp( data, nc, err );
}

/*
 * add marker to end of data's marker list. Extends list when necessary.
 * Marker is not copied.
 *
 * on success 0 is returned
 * returns -1 and sets errno on error
 */
bool srmio_data_add_markerp( srmio_data_t data, srmio_marker_t mk, srmio_error_t *err )
{
	if( data->mused >= data->mavail ){
		srmio_marker_t *tmp;

		if( data->mavail > UINT_MAX - 11 ){
			srmio_error_set( err, "too many marker");
			return false;
		}

		if( NULL == (tmp = realloc( data->marker,
			(data->mavail + 11) * sizeof(srmio_marker_t)))){
			srmio_error_errno( err, "enlarge data marker" );
			return false;
		}

		data->mavail += 10;
		data->marker = tmp;
	}

	data->marker[data->mused] = mk;
	data->marker[++ data->mused] = NULL;

	return true;
}

/*
 * create new marker and add it to data's marker list
 *
 * parameters:
 *  data: the data structure to modify
 *  first: number of first marked chunk (counts from 0)
 *  last: number of first marked chunk
 *
 * on success 0 is returned
 * returns -1 and sets errno on error
 */
bool srmio_data_add_marker( srmio_data_t data, unsigned first, unsigned last, srmio_error_t *err )
{
	srmio_marker_t mk;

	DPRINTF( "%u to %u",
		first,
		last );

	if( first >= data->cused ){
		srmio_error_set(err, "marker out of range: first=%u > used=%u",
			first, data->cused );
		return false;
	}

	if( first > last ){
		srmio_error_set(err, "marker out of range: first=%u > last=%u",
			first, last );
		return false;
	}

	if( NULL == (mk = srmio_marker_new( err ) ))
		return false;
	mk->first = first;
	mk->last = last;

	if( ! srmio_data_add_markerp( data, mk, err ) )
		goto clean1;

	return true;

clean1:
	free(mk);
	return false;
}


/*
 * return start time of first chunk
 */
bool srmio_data_time_start( srmio_data_t data, srmio_time_t *start, srmio_error_t *err )
{
	assert( data );
	assert( start );

	if( ! data->cused ){
		srmio_error_set( err, "no data available" );
		return false;
	}

	*start = data->chunks[0]->time;
	return true;
}

bool srmio_data_time_end( srmio_data_t data, srmio_time_t *end, srmio_error_t *err )
{
	assert( data );
	assert( end );

	if( ! data->cused ){
		srmio_error_set( err, "no data available" );
		return false;
	}

	*end = data->chunks[data->cused -1]->time
		+ data->chunks[data->cused -1]->dur;
	return true;
}


/*
 * return common recording interval
 */
bool srmio_data_recint( srmio_data_t data, srmio_time_t *recint, srmio_error_t *err )
{
	assert( data );
	assert( recint );

	if( ! data->cused ){
		srmio_error_set( err, "no data available" );
		return false;
	}

	*recint = data->chunks[data->cused-1]->dur;
	return true;
}


/*
 * find gaps in chunklist (non-continuos time), allocate and build list
 * with marker identifying the continuos blocks.
 *
 * parameters:
 *  data: the data structure to check
 *
 * on success pointer to list is returned
 * returns NULL on error and sets errno.
 */
srmio_marker_t *srmio_data_blocks( srmio_data_t data, srmio_error_t *err )
{
	srmio_marker_t *blocks;
	unsigned avail = 10;
	unsigned used = 1;
	unsigned i;

	if( data->cused < 1 ){
		srmio_error_set( err, "no data available");
		return NULL;
	}

	if( NULL == (blocks = malloc( (1+ avail) * (sizeof(srmio_marker_t))))){
		srmio_error_errno( err, "data blocks" );
		return NULL;
	}

	if( NULL == (*blocks = srmio_marker_new( err )))
		goto clean1;
	blocks[0]->first = 0;
	blocks[0]->last = data->cused -1;
	blocks[1] = NULL;

	for( i=1; i < data->cused; ++i ){
		srmio_chunk_t prev = data->chunks[i-1];
		srmio_chunk_t this = data->chunks[i];

		if( prev->time + prev->dur != this->time ){

			if( used >= avail ){
				srmio_marker_t *tmp;
				unsigned ns = avail + 10;

				if( ns < avail ){
					srmio_error_set( err, "too many blocks");
					goto clean2;
				}

				if( NULL == (tmp = realloc(blocks, (1+ ns)
					* (sizeof(srmio_marker_t)) ))){

					srmio_error_errno( err, "enlage data blocks" );
					goto clean2;
				}

				blocks = tmp;
				avail = ns;
			}

			DPRINTF("found gap @%u "
				"%.1f - %.1f = %.1f",
				i,
				(double)prev->time/10,
				(double)this->time/10,
				(double)(this->time - prev->time)/10);

			if( NULL == (blocks[used] = srmio_marker_new(err) ) )
				goto clean2;

			blocks[used-1]->last = i -1;
			blocks[used]->first = i;
			blocks[used]->last = data->cused -1;
			blocks[++used] = NULL;
		}
	}

	return blocks;

clean2:
	for( i=0; i < used; ++i )
		srmio_marker_free( blocks[i] );

clean1:
	free(blocks);
	return NULL;
}

/*
 * free all memory held in data structure
 */
void srmio_data_free( srmio_data_t data )
{
	unsigned i;

	if( data == NULL )
		return;

	for( i=0; i < data->cused; ++i )
		srmio_chunk_free(data->chunks[i]);
	free(data->chunks);

	for( i=0; i < data->mused; ++i )
		srmio_marker_free(data->marker[i]);
	free(data->marker);

	free(data->notes);
	free(data);
}


