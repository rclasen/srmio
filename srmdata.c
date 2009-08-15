/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include "common.h"

#ifdef DEBUG
void DUMPHEX( const char *prefix, const unsigned char *buf, size_t blen )
{
	size_t i;

	if( blen < 1 )
		return;

	fprintf( stderr, "%s:", prefix );
	for( i=0; i < blen; ++i ){
		unsigned char c = buf[i];
		unsigned char p = c;

		if( ! isprint(c) )
			p=' ';

		fprintf( stderr, " 0x%02x/%c", c, p );
	}
	fprintf( stderr, "\n" );
}
#endif

/*
 * allocate and initialize new chunk
 *
 * on success pointer is returned
 * returns NULL on error and sets errno.
 */
srm_chunk_t srm_chunk_new( void )
{
	srm_chunk_t tmp;

	if( NULL == (tmp = malloc( sizeof(struct _srm_chunk_t)) ))
		return NULL;

	memset( tmp, 0, sizeof(struct _srm_chunk_t));
	return tmp;
}

/*
 * copy chunk to a newly allocated one.
 *
 * on success pointer is returned
 * returns NULL on error and sets errno.
 */
srm_chunk_t srm_chunk_clone( srm_chunk_t chunk )
{
	srm_chunk_t tmp;

	if( NULL == (tmp = malloc( sizeof(struct _srm_chunk_t)) ))
		return NULL;

	memcpy( tmp, chunk, sizeof(struct _srm_chunk_t));
	return tmp;
}

/*
 * free chunk memory
 */
void srm_chunk_free( srm_chunk_t chunk )
{
	free( chunk );
}





/*
 * allocate and initialize new marker
 *
 * on success pointer is returned
 * returns NULL on error and sets errno.
 */
srm_marker_t srm_marker_new( void )
{
	srm_marker_t tmp;

	if( NULL == (tmp = malloc( sizeof(struct _srm_marker_t)) ))
		return NULL;

	memset( tmp, 0, sizeof(struct _srm_marker_t));
	return tmp;
}

/*
 * free marker memory
 */
void srm_marker_free( srm_marker_t block )
{
	if( ! block )
		return;

	free( block->notes );
	free( block );
}




/*
 * allocate and initialize new data structure to hold data retrieved
 * from PCV or file
 *
 * on success pointer is returned
 * returns NULL on error and sets errno.
 */
srm_data_t srm_data_new( void )
{
	srm_data_t data;

	if( NULL == (data = malloc( sizeof( struct _srm_data_t) )) )
		return NULL;
	memset( data, 0, sizeof(struct _srm_data_t));

	if( NULL == (data->chunks = malloc(sizeof(srm_chunk_t *))))
		goto clean1;
	*data->chunks = NULL;

	if( NULL == (data->marker = malloc(sizeof(srm_marker_t *))))
		goto clean2;
	*data->marker = NULL;

	return data;

clean2:
	free( data->chunks );

clean1:
	free( data );
	return NULL;
}

/*
 * add chunk to end of data's chunk list. Extends list when necessary.
 * Chunk is not copied.
 *
 * on success 0 is returned
 * returns -1 and sets errno on error
 */
int srm_data_add_chunkp( srm_data_t data, srm_chunk_t chunk )
{
	DPRINTF( "srm_data_add_chunk %u:"
		"time=%.1f, "
		"temp=%.1f, "
		"pwr=%u, "
		"spd=%.3f, "
		"cad=%u, "
		"hr=%u ",
		data->cused,
		(double)chunk->time/10,
		chunk->temp,
		chunk->pwr,
		chunk->speed,
		chunk->cad,
		chunk->hr );

	if( data->cused >= data->cavail ){
		srm_chunk_t *tmp;

		if( data->cavail > UINT_MAX - 1001 ){
			errno = EOVERFLOW;
			return -1;
		}

		if( NULL == (tmp = realloc( data->chunks, 
			(data->cavail + 1001) * sizeof(srm_chunk_t))))
			return -1;

		data->cavail += 1000;
		data->chunks = tmp;
	}

	data->chunks[data->cused] = chunk;
	data->chunks[++ data->cused] = NULL;

	return 0;
}

/*
 * add copy of chunk to end of data's chunk_list.
 *
 * on success 0 is returned
 * returns -1 and sets errno on error
 */
int srm_data_add_chunk( srm_data_t data, srm_chunk_t chunk )
{
	srm_chunk_t nc;

	if( NULL == (nc = srm_chunk_clone(chunk)))
		return -1;
	return srm_data_add_chunkp( data, nc );
}

/*
 * add marker to end of data's marker list. Extends list when necessary.
 * Marker is not copied.
 *
 * on success 0 is returned
 * returns -1 and sets errno on error
 */
int srm_data_add_markerp( srm_data_t data, srm_marker_t mk )
{
	if( data->mused >= data->mavail ){
		srm_marker_t *tmp;

		if( data->mavail > UINT_MAX - 11 ){
			errno = EOVERFLOW;
			return -1;
		}

		if( NULL == (tmp = realloc( data->marker, 
			(data->mavail + 11) * sizeof(srm_marker_t))))
			return -1;

		data->mavail += 10;
		data->marker = tmp;
	}

	data->marker[data->mused] = mk;
	data->marker[++ data->mused] = NULL;

	return 0;
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
int srm_data_add_marker( srm_data_t data, unsigned first, unsigned last )
{
	srm_marker_t mk;

	DPRINTF( "srm_data_add_marker: %u to %u", 
		first, 
		last );

	if( first >= data->cused || first > last ){
		errno = EINVAL;
		return -1;
	}

	if( NULL == (mk = srm_marker_new() ))
		return -1;
	mk->first = first;
	mk->last = last;

	if( 0 > srm_data_add_markerp( data, mk ) )
		goto clean1;

	return 0;

clean1:
	free(mk);
	return -1;
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
srm_marker_t *srm_data_blocks( srm_data_t data )
{
	srm_marker_t *blocks;
	unsigned avail = 10;
	unsigned used = 1;
	unsigned i;

	if( data->cused < 1 ){
		errno = EINVAL;
		return NULL;
	}

	if( NULL == (blocks = malloc( (1+ avail) * (sizeof(srm_marker_t)))))
		return NULL;

	if( NULL == (*blocks = srm_marker_new()))
		goto clean1;
	blocks[0]->first = 0;
	blocks[0]->last = data->cused -1;
	blocks[1] = NULL;

	for( i=1; i < data->cused; ++i ){
		srm_chunk_t prev = data->chunks[i-1];
		srm_chunk_t this = data->chunks[i];

		if( prev->time + data->recint != this->time ){

			if( used >= avail ){
				srm_marker_t *tmp;
				unsigned ns = avail + 10;

				if( ns < avail ){
					errno = EOVERFLOW;
					goto clean2;
				}

				if( NULL == (tmp = realloc(blocks, (1+ ns) 
					* (sizeof(srm_marker_t)) )))

					goto clean2;

				blocks = tmp;
				avail = ns;
			}

			DPRINTF("srm_data_blocks found gap @%u "
				"%.1f - %.1f = %.1f", 
				i,
				(double)prev->time/10, 
				(double)this->time/10,
				(double)(this->time - prev->time)/10);

			if( NULL == (blocks[used] = srm_marker_new() ) )
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
		srm_marker_free( blocks[i] );

clean1:
	free(blocks);
	return NULL;
}

/*
 * free all memory held in data structure
 */
void srm_data_free( srm_data_t data )
{
	unsigned i;

	if( data == NULL )
		return;
	
	for( i=0; i < data->cused; ++i )
		srm_chunk_free(data->chunks[i]);
	free(data->chunks);

	for( i=0; i < data->mused; ++i )
		srm_marker_free(data->marker[i]);
	free(data->marker);

	free(data->notes);
	free(data);
}


