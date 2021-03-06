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
 * add chunk to data.
 * chunk's timestamp is adjusted to fit recint
 * if there's a gap, it's filled with averaged data
 *
 * on success 0 is returned
 * returns -1 and sets errno on error
 */
static bool srmio_data_add_fillp( srmio_data_t data, srmio_chunk_t chunk,
	srmio_error_t *err )
{
	srmio_chunk_t last = data->chunks[data->cused-1];
	srmio_time_t recint = chunk->dur;
	srmio_time_t lnext = last->time + last->dur;
	unsigned miss;
	unsigned i;

	assert(data);
	assert(data->cused);
	assert(chunk);
	assert(chunk->dur);

	if( chunk->time < lnext ){
		srmio_error_set( err, "data is overlapping, can't add" );
		return false;
	}

	if( lnext < last->time ){
		srmio_error_set( err, "time overflow error" );
		return false;
	}

	miss = 0.4 + (chunk->time - lnext) / recint;
	lnext += recint * miss;


	/* ... adjust current time to fit n*recint */
	if( chunk->time != lnext ){
		DPRINTF("adjusting gap %.1f -> %.1f",
			(double)chunk->time / 10,
			(double)lnext / 10);
		chunk->time = lnext;
	}

	if( ! miss )
		return true;

	DPRINTF( "synthesizing %d chunks @%.1f",
		miss, (double)lnext / 10 );

	/* ... adjust marker indices */
	for( i=0; i < data->mused; ++i ){
		srmio_marker_t mark = data->marker[i];

		if( mark->first >= data->cused )
			mark->first += miss;

		if( mark->last >= data->cused )
			mark->last += miss;
	}

	/* ... insert averaged data */
	for( i = 1; i <= miss; ++i ){
		srmio_chunk_t fill;
		double part = (double)i / (miss +1 );

		if( NULL == (fill = srmio_chunk_new(err) ))
			return false;

		fill->time = last->time + last->dur + ((i-1) * recint);
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

		if( ! srmio_data_add_chunkp( data, fill, err ) ){
			srmio_chunk_free(fill);
			return false;
		}
	}

	if( ! srmio_data_add_chunkp( data, chunk, err ) )
		return false;

	return true;
}


/* TODO: skip n equal chunks or set pwr+cad = 0 before first chunk with
 * cad=0. These n chunks were recorded while PC was waiting for next
 * signal ... timeout is for cad=20 -> thats 3 chunks @ 1sec recint */

/*
 * fix small time leaps at block boundaries
 * fix timestamps of overlapping chunks
 * fill small gaps with averaged data
 * fixed data is copied to a new srmio_data_t handle
 *
 * returns pointer to newly allocated srmio_data
 * returns NULL on failure
 */
srmio_data_t srmio_data_fixup( srmio_data_t data, srmio_error_t *err )
{
	srmio_data_t fixed;
	srmio_time_t delta = 0;
	unsigned c, m;

	assert( data );

	DPRINTF("start %d", data->cused );

	if( data->cused < 1 ){
		srmio_error_set( err, "no data" );
		return NULL;
	}

	/* copy global data */
	if( NULL == (fixed = srmio_data_header( data, err ) ))
		return NULL;

	/* copy marker */
	for( m=0; m < data->mused; ++m ){
		srmio_marker_t mark;

		if( NULL == ( mark = srmio_marker_clone( data->marker[m], err ) ) )
			goto clean1;

		if( ! srmio_data_add_markerp( fixed, mark, err ) )
			goto clean1;
	}

	/* copy chunks + fix smaller gaps/overlaps */

	if( ! srmio_data_add_chunk( fixed, data->chunks[0], err ) )
		goto clean1;

	for( c=1; c < data->cused; ++c ){
		srmio_chunk_t last = fixed->chunks[fixed->cused-1];
		srmio_chunk_t this;
		srmio_time_t recint;
		srmio_time_t lnext;

		if( NULL == ( this = srmio_chunk_clone( data->chunks[c], err )))
			goto clean1;

		recint = this->dur;
		lnext = last->time + last->dur;

		/* overlapping < 1sec, adjust this time */
		if( this->time < lnext
			&& lnext - last->time < 10 ){

			DPRINTF("adjusting overlap %.1f -> %.1f",
				(double)this->time / 10,
				(double)lnext / 10);
			this->time = lnext;

			if( ! srmio_data_add_chunkp( fixed, this, err ) )
				goto clean1;

		/* small gap > recint ... fill/shift  */
		} else if( lnext < this->time
			&& this->time - lnext <= 2*recint ){

			if( ! srmio_data_add_fillp( fixed, this, err ))
				goto clean1;

		/* nothing to fix (yet) */
		} else {
			if( ! srmio_data_add_chunkp( fixed, this, err ) )
				goto clean1;

		}

	}

	/* fix "severe" overlaping */
	for( c = fixed->cused -1; c > 0; --c ){
		srmio_chunk_t this = fixed->chunks[c-1];
		srmio_chunk_t next = fixed->chunks[c];
		srmio_time_t recint = this->dur;
		srmio_time_t nprev = next->time - recint - delta;

		if( nprev < this->time ){
			delta += this->time - nprev;
			DPRINTF( "overlaping blocks #%d "
				"@.%.1lf - %.1lf, "
				"new delta: %.1f",
				c,
				0.1 * this->time,
				0.1 * next->time,
				(double)delta/10 );
		}
		this->time -= delta;
	}

	return fixed;

clean1:
	srmio_data_free( fixed );
	return NULL;
}



