/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */


#include "pc.h"

srmio_pc_t srmio_pc_new( const srmio_pc_methods_t *methods, void *child )
{
	srmio_pc_t pch;

	assert( methods );
	assert( child );

	if( NULL == (pch = malloc( sizeof(struct _srmio_pc_t))))
		return NULL;

	memset( pch, 0, sizeof(struct _srmio_pc_t) );

	pch->methods = methods;
	pch->child = child;
	pch->xfer_state = srmio_pc_xfer_state_new;
	pch->xfer_type = srmio_pc_xfer_type_new;

	return pch;
}

void srmio_pc_free( srmio_pc_t pch )
{
	if( pch->is_open )
		srmio_pc_close( pch );

	(*pch->methods->free)( pch );
	free(pch);
}

bool srmio_pc_open( srmio_pc_t pch )
{
	assert( pch );
	assert( pch->io );

	DPRINTF("");

	if( pch->is_open ){
		errno = EBUSY;
		return false;
	}

	if( ! srmio_io_is_open( pch->io ) ){
		errno = EINVAL;
		return false;
	}

	srmio_io_set_baudrate( pch->io, pch->baudrate );
	srmio_io_set_parity( pch->io, pch->parity );

	if( ! (pch->methods->open( pch ) ) )
		return false;

	pch->is_open = true;
	return true;
}

bool srmio_pc_close( srmio_pc_t pch )
{
	assert( pch );
	assert( pch->io );

	DPRINTF("");

	if( pch->xfer_state != srmio_pc_xfer_state_new )
		srmio_pc_xfer_finish( pch );

	if( ! (pch->methods->close( pch ) ) )
		return false;

	pch->is_open = false;
	return true;
}

/*
 * sets the device filename, that's len for communicating.
 * does *not* take ownership of io handle!!! You need to free it yourself!
 */
bool srmio_pc_set_device( srmio_pc_t pch, srmio_io_t h )
{
	assert( pch );
	assert( h );

	if( pch->is_open ){
		errno = EBUSY;
		return false;
	}

	pch->io = h;

	return true;
}

/*
 * retrieve the IO handle
 */
bool srmio_pc_get_device( srmio_pc_t pch, srmio_io_t *h )
{
	assert( pch );
	assert( h );

	*h = pch->io;
	return true;
}

/*
 * sets the baudrate to use for communication with the PowerControl.
 * with srmio_io_baud_max, all supported baud rates are probed.
 */
bool srmio_pc_set_baudrate( srmio_pc_t pch, srmio_io_baudrate_t rate )
{
	assert( pch );
	assert( rate <= srmio_io_baud_max );

	if( pch->is_open ){
		errno = EBUSY;
		return false;
	}

	pch->baudrate = rate;
	return true;
}


/*
 * sets the parity to use for communication with the PowerControl.
 * with srmio_io_parity_max, all supported parity settings are probed.
 */
bool srmio_pc_set_parity( srmio_pc_t pch, srmio_io_parity_t parity )
{
	assert( pch );
	assert( parity <= srmio_io_parity_max );

	if( pch->is_open ){
		errno = EBUSY;
		return false;
	}

	pch->parity = parity;
	return true;
}


/*
 * gets the firmware as discovered when opening the device
 */
bool srmio_pc_get_version( srmio_pc_t pch, unsigned *version )
{
	assert( pch );
	assert( version );

	*version = pch->firmware;
	return true;
}

bool srmio_pc_set_xfer( srmio_pc_t pch, srmio_pc_xfer_type_t type )
{
	assert( pch );
	assert( type < srmio_pc_xfer_type_max );

	DPRINTF( "%d", type );

	pch->xfer_type = type;

	return true;
}

bool srmio_pc_can_preview( srmio_pc_t conn )
{
	assert(conn);

	return conn->can_preview;
}

bool srmio_pc_xfer_get_blocks( srmio_pc_t conn, size_t *blocks )
{
	assert( conn );
	assert( blocks );

	*blocks = conn->block_cnt;

	return true;
}


bool srmio_pc_cmd_get_athlete( srmio_pc_t pch, char **athlete )
{
	assert(pch);

	return (*pch->methods->cmd_get_athlete)( pch, athlete );
}

bool srmio_pc_cmd_set_time( srmio_pc_t pch, struct tm *t )
{
	assert(pch);

	return (*pch->methods->cmd_set_time)( pch, t );
}

bool srmio_pc_cmd_set_recint( srmio_pc_t pch, srmio_time_t t )
{
	assert(pch);

	return (*pch->methods->cmd_set_recint)( pch, t );
}

bool srmio_pc_cmd_clear( srmio_pc_t pch )
{
	assert(pch);

	return (*pch->methods->cmd_clear)( pch );
}

bool srmio_pc_xfer_start( srmio_pc_t pch )
{
	assert(pch);

	return (*pch->methods->xfer_start)( pch );
}

bool srmio_pc_xfer_block_next( srmio_pc_t pch, srmio_pc_xfer_block_t block )
{
	assert(pch);
	assert( block );

	return (*pch->methods->xfer_block_next)( pch, block );
}

bool srmio_pc_xfer_chunk_next( srmio_pc_t pch, srmio_chunk_t chunk,
	bool *is_intervall, bool *start_intervall )
{
	assert(pch);
	assert( chunk );

	return (*pch->methods->xfer_chunk_next)( pch, chunk,
		is_intervall, start_intervall );
}

bool srmio_pc_xfer_finish( srmio_pc_t pch )
{
	assert(pch);

	return (*pch->methods->xfer_finish)( pch );
}

srmio_pc_xfer_state_t srmio_pc_xfer_status( srmio_pc_t pch )
{
	assert( pch );

	return pch->xfer_state;
}

bool srmio_pc_xfer_block_progress( srmio_pc_t pch, size_t *block_done )
{
	assert( pch );

	return (*pch->methods->xfer_block_progress)( pch, block_done );
}




/************************************************************
 *
 * use srmio_pc_xfer_all() to fill srmio_data_t structure
 * with all chunks.
 *
 * Also serves as example on how to use the download API.
 *
 ************************************************************/

/*
 * retrieve recorded data from PC and build  "friendly" srmio_data_t structure.
 *
 * parameter:
 *  pch: conn handle
 *
 */
bool srmio_pc_xfer_all( srmio_pc_t pch,
	srmio_data_t data,
	srmio_progress_t pfunc, void *user_data  )
{
	int mfirst = -1;
	struct _srmio_pc_xfer_block_t block;
	struct _srmio_chunk_t chunk;
	size_t done_chunks = 0;
	size_t block_cnt, block_num;
	size_t prog_prev = 0, prog_sum=0;
	srmio_pc_xfer_state_t result;

	assert( pch );
	assert( data );

	block.athlete = NULL;

	DPRINTF("");

	if( ! srmio_pc_xfer_start( pch ) )
		goto clean1;

	if( ! srmio_pc_xfer_get_blocks( pch, &block_cnt ) )
		goto clean2;

	if( block_cnt > 1 ){
		if( srmio_pc_can_preview( pch ) ){
			while( srmio_pc_xfer_block_next( pch, &block ) ){
				prog_sum += block.total;
				if( block.athlete )
					free( block.athlete );
				block.athlete = NULL;
			}
			DPRINTF("prog_sum %u", prog_sum );

			/* finalize / restart xfer */
			srmio_pc_xfer_finish( pch );

			if( ! srmio_pc_xfer_start( pch ) )
				goto clean2;
		}
	}

	while( srmio_pc_xfer_block_next( pch, &block ) ){
		bool is_int;
		bool is_first;
		size_t prog_total;

		data->slope = block.slope;
		data->zeropos = block.zeropos;
		data->circum = block.circum;
		if( block.athlete ){
			if( data->athlete )
				free(data->athlete);
			data->athlete = strdup(block.athlete);
		}

		if( prog_sum ){
			prog_total = prog_sum;

		} else if( block_cnt == 1 ){
			prog_total = block.total +1;

		} else {
			prog_total = block_cnt * 1000;
		}

		while( srmio_pc_xfer_chunk_next( pch, &chunk, &is_int, &is_first  ) ){

			if( pfunc && 0 == done_chunks % 16 ){
				size_t block_done = 0;

				srmio_pc_xfer_block_progress( pch, &block_done );
				if( prog_sum ){
					block_done += prog_prev;

				} else if( block_cnt == 1 ){
					/* unchanged */

				} else {
					block_done = (double)block_num * 1000 + 1000 *
						block.total / block_done;
				}

				DPRINTF( "prog_total %d, prog_prev %d, block_done %d",
					prog_total, prog_prev, block_done );
				(*pfunc)( prog_total, block_done, user_data );
			}


			if( ! srmio_data_add_chunk( data, &chunk ) )
				goto clean2;

			++done_chunks;

			/* finish previous marker */
			if( mfirst >= 0 && ( ! is_int || is_first ) )
				srmio_data_add_marker( data, mfirst, data->cused -1 );

			/* start marker */
			if( is_first ){
				mfirst = (int)data->cused;
				DPRINTF( "new marker at %d", mfirst );

			} else if( ! is_int ){
				mfirst = -1;

			}

			++block_num;
		}

		/* finalize marker at block end */
		if( mfirst >= 0 ){
			srmio_data_add_marker( data, mfirst, data->cused -1 );
			mfirst = -1;
		}

		if( prog_sum )
			prog_prev += block.total;
		else
			prog_prev += 1000;

		free( block.athlete );
		block.athlete = NULL;
	}

	if( ! done_chunks ){
		errno = ENODATA;
		goto clean2;
	}

	result = srmio_pc_xfer_status( pch );
	srmio_pc_xfer_finish( pch );

	return result == srmio_pc_xfer_state_success;

clean2:
	srmio_pc_xfer_finish( pch );

clean1:
	return false;
}

