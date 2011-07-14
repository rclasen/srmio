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
 * copy existing marker
 *
 * on success pointer is returned
 * returns NULL on error and sets errno.
 */
srm_marker_t srm_marker_clone( srm_marker_t marker )
{
	srm_marker_t tmp;

	if( NULL == (tmp = srm_marker_new()))
		return NULL;

	tmp->first = marker->first;
	tmp->last = marker->last;

	if( marker->notes && NULL == (tmp->notes = strdup( marker->notes ) ))
		goto clean1;

	return tmp;

clean1:
	srm_marker_free( tmp );
	return NULL;
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
 * add chunk to data.
 * chunk's timestamp is adjusted to fit recint
 * if there's a gap, it's filled with averaged data
 *
 * on success 0 is returned
 * returns -1 and sets errno on error
 */
int srm_data_add_fillp( srm_data_t data, srm_chunk_t chunk )
{
	srm_chunk_t last = data->chunks[data->cused-1];
	srm_time_t recint = chunk->dur;
	srm_time_t lnext = last->time + recint;
	unsigned miss;
	unsigned i;

	if( chunk->time < lnext ){
		ERRMSG( "srm_data_add_fillp: data is overlapping, can't add" );
		errno = EINVAL;
		return -1;
	}

	miss = 0.4 + (chunk->time - lnext) / recint;
	lnext += recint * miss;


	/* ... adjust current time to fit n*recint */
	if( chunk->time != lnext ){
		DPRINTF("srm_data_add_fill: adjusting gap %.1f -> %.1f",
			(double)chunk->time / 10,
			(double)lnext / 10);
		chunk->time = lnext;
	}

	if( ! miss )
		return 0;

	DPRINTF( "srm_data_add_fill: synthesizing %d chunks @%.1f",
		miss, (double)lnext / 10 );

	/* ... adjust marker indices */
	for( i=0; i < data->mused; ++i ){
		srm_marker_t mark = data->marker[i];

		if( mark->first >= data->cused )
			mark->first += miss;

		if( mark->last >= data->cused )
			mark->last += miss;
	}

	/* ... insert averaged data */
	for( i = 1; i <= miss; ++i ){
		srm_chunk_t fill;
		double part = (double)i / (miss +1 );

		if( NULL == (fill = srm_chunk_new() ))
			return -1;

		fill->time = last->time + (i * recint);
		fill->dur = recint;
		fill->temp = part * (chunk->temp
			- last->temp) + last->temp;
		fill->pwr = part * (int)( chunk->pwr - last->pwr )
			+ last->pwr + 0.5;
		fill->speed = part * (chunk->speed
			- last->speed) + last->speed;
		fill->cad = part * (int)( chunk->cad - last->cad )
			+ last->cad + 0.5;
		fill->hr = part * (int)( chunk->hr
			- last->hr ) + last->hr;
		fill->ele = part * (chunk->ele - last->ele)
			+ last->ele + 0.5;

		if( 0 > srm_data_add_chunkp( data, fill ) ){
			srm_chunk_free(fill);
			return -1;
		}
	}

	if( 0 > srm_data_add_chunkp( data, chunk ) )
		return -1;

	return 0;
}


/*
 * fix small time leaps at block boundaries
 * fix timestamps of overlapping chunks
 * fill small gaps with averaged data
 * fixed data is copied to a new srm_data_t handle
 *
 * returns pointer to newly allocated srm_data
 * returns NULL on failure
 */
srm_data_t srm_data_fixup( srm_data_t data )
{
	srm_data_t fixed;
	srm_time_t delta = 0;
	unsigned c, m;

	if( data->cused < 1 )
		return NULL;

	if( NULL == (fixed = srm_data_new() ))
		return NULL;

	/* copy global data */
	fixed->slope = data->slope;
	fixed->zeropos = data->zeropos;
	fixed->circum = data->circum;
	if( data->notes && NULL == (fixed->notes = strdup( data->notes ) ))
		goto clean1;

	/* copy marker */
	for( m=0; m < data->mused; ++m ){
		srm_marker_t mark;

		if( NULL == ( mark = srm_marker_clone( data->marker[m]) ) )
			goto clean1;

		if( 0 > srm_data_add_markerp( fixed, mark ) )
			goto clean1;
	}

	/* copy chunks + fix smaller gaps/overlaps */

	if( 0 > srm_data_add_chunk( fixed, data->chunks[0] ) )
		goto clean1;

	for( c=1; c < data->cused; ++c ){
		srm_chunk_t last = fixed->chunks[fixed->cused-1];
		srm_chunk_t this;
		srm_time_t recint;
		srm_time_t lnext;

		if( NULL == ( this = srm_chunk_clone( data->chunks[c] )))
			goto clean1;

		recint = this->dur;
		lnext = last->time + recint;

		/* overlapping < 1sec, adjust this time */
		if( this->time < lnext
			&& lnext - last->time < 10 ){

			DPRINTF("srm_data_fixup: adjusting overlap %.1f -> %.1f",
				(double)this->time / 10,
				(double)lnext / 10);
			this->time = lnext;

			if( 0 > srm_data_add_chunkp( fixed, this ) )
				goto clean1;

		/* small gap > recint ... fill/shift  */
		} else if( lnext < this->time
			&& this->time - lnext <= 2*recint ){

			if( 0 > srm_data_add_fillp( fixed, this ))
				goto clean1;

		/* nothing to fix (yet) */
		} else {
			if( 0 > srm_data_add_chunkp( fixed, this ) )
				goto clean1;

		}

	}

	/* fix "severe" overlaping */
	for( c = fixed->cused -1; c > 0; --c ){
		srm_chunk_t this = fixed->chunks[c-1];
		srm_chunk_t next = fixed->chunks[c];
		srm_time_t recint = next->dur;
		srm_time_t nprev = next->time - recint - delta;

		if( nprev < this->time ){
			delta += this->time - nprev;
			DPRINTF( "srm_data_fixup overlaping blocks @%d, "
				"new delta: %.1f",
				c,
				(double)delta/10 );
		}
		this->time -= delta;
	}

	return fixed;

clean1:
	srm_data_free( fixed );
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
			ERRMSG("too many chunks");
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
			ERRMSG("too many marker");
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
		ERRMSG("marker out of range");
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
 * return start time of first chunk
 */
srm_time_t srm_data_time_start( srm_data_t data )
{
	if( data->cused < 1 )
		return (srm_time_t) -1;

	return data->chunks[0]->time;
}


/*
 * return common recording interval
 */
srm_time_t srm_data_recint( srm_data_t data )
{
	if( ! data->cused )
		return 0;

	return data->chunks[data->cused-1]->dur;
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
		ERRMSG("no blocks in empty data");
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

		if( prev->time + this->dur != this->time ){

			if( used >= avail ){
				srm_marker_t *tmp;
				unsigned ns = avail + 10;

				if( ns < avail ){
					ERRMSG("too many blocks");
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


