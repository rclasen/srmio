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
 * split data
 * - when recint changes
 * - on negative time steps
 * - on gaps
 * - when data overlaps significantly 
 *
 */
srmio_data_t *srmio_data_split( srmio_data_t src, srmio_time_t gap,
	srmio_time_t overlap, srmio_error_t *err )
{
	srmio_data_t *list = NULL;
	unsigned alloc = 0;
	unsigned used = 0;
	unsigned c, delta;

	assert( src );

	srmio_error_set( err, "no data" );

	for( c = 0; c < src->cused; ++c ){
		srmio_time_t start = src->chunks[c]->time
			- src->chunks[c]->dur;
		int split = 0;

		if( c == 0 ){
			++split;

		} else if( src->chunks[c]->dur != src->chunks[c-1]->dur ){
			DPRINTF( "found recint change" );
			++split;

		} else if( start > src->chunks[c-1]->time ){
			if( start - src->chunks[c-1]->time > gap ){
				DPRINTF( "found gap" );
				++split;
			}

		} else if( start < src->chunks[c-1]-> time ){
			if( src->chunks[c-1]->time - start > overlap ){
				DPRINTF( "found overlap" );
				++split;
			}

		}

		if( split ){
			DPRINTF( "starting new data %u", used );
			if( used +1 >= alloc ){
				srmio_data_t *new;

				if( NULL == (new = realloc( list,
					sizeof(srmio_data_t) * ( alloc +1 +10)  ) )){
					srmio_error_errno( err, "realloc");
					goto clean;
				}

				alloc += 10;
				list = new;
			}

			if( NULL == (list[used] = srmio_data_header( src, err)))
				goto clean;

			list[++used] = NULL;
		}

		srmio_data_add_chunk( list[used-1], src->chunks[c], err );
	}

	delta = 0;
	for( c = 0; c < used; ++c ){
		unsigned last = delta + list[c]->cused -1;
		unsigned m;

		DPRINTF( "file %d: %d - %d", c, delta, last );

		for( m = 0; m < src->mused; ++m ){
			srmio_marker_t mk = src->marker[m];
			srmio_marker_t nm;

			if( mk->last < delta || mk->first > last )
				continue;

			DPRINTF( "found marker: %d - %d", mk->first,
				mk->last );

			if( NULL == (nm = srmio_marker_clone( mk, err )))
				goto clean;

			if( nm->first < delta )
				nm->first = 0;
			else
				nm->first -= delta;

			if( nm->last > last )
				nm->last = last;
			else
				nm->last -= delta;

			srmio_data_add_markerp( list[c], nm, err );
		}

		delta += list[c]->cused;
	}

	return list;

clean:
	for( c = 0; c < used;  ++c )
		srmio_data_free( list[c] );
	free( list );

	return NULL;
}



