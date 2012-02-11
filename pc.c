/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */


#include "pc.h"

#include <stdarg.h>

void srmio_pc_logv( srmio_pc_t pch, const char *fmt, va_list ap )
{
	char buf[SRMIO_ERROR_MSG_SIZE];

	if( ! pch->lfunc )
		return;

	if( 0 > vsnprintf( buf, SRMIO_ERROR_MSG_SIZE, fmt, ap ) )
		return;

	(*pch->lfunc)( buf, pch->ldata );
}

void srmio_pc_log( srmio_pc_t pch, const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	srmio_pc_logv( pch, fmt, ap );
	va_end( ap );
}

srmio_pc_t srmio_pc_new( const srmio_pc_methods_t *methods, void *child,
	srmio_error_t *err )
{
	srmio_pc_t pch;

	assert( methods );
	assert( child );

	if( NULL == (pch = malloc( sizeof(struct _srmio_pc_t)))){
		srmio_error_errno( err, "pc new" );
		return NULL;
	}

	memset( pch, 0, sizeof(struct _srmio_pc_t) );

	pch->methods = methods;
	pch->child = child;
	pch->xfer_state = srmio_pc_xfer_state_new;
	pch->xfer_type = srmio_pc_xfer_type_new;

	return pch;
}

void srmio_pc_free( srmio_pc_t pch )
{
	assert(pch->methods->free);

	if( pch->is_open )
		srmio_pc_close( pch, NULL );

	(*pch->methods->free)( pch );
	free(pch);
}

bool srmio_pc_open( srmio_pc_t pch, srmio_error_t *err )
{
	assert( pch );
	assert( pch->io );
	assert(pch->methods->open);

	SRMIO_PC_DEBUG(pch, "");

	if( pch->is_open ){
		SRMIO_PC_ERROR( pch, err, "device is already open" );
		return false;
	}

	if( ! srmio_io_is_open( pch->io ) ){
		SRMIO_PC_ERROR( pch, err, "io device is already open" );
		return false;
	}

	srmio_io_set_baudrate( pch->io, pch->baudrate, err );
	srmio_io_set_parity( pch->io, pch->parity, err );

	if( ! (*pch->methods->open)( pch, err ) )
		return false;

	pch->is_open = true;
	return true;
}

bool srmio_pc_close( srmio_pc_t pch, srmio_error_t *err )
{
	assert( pch );
	assert( pch->io );
	assert(pch->methods->close);

	SRMIO_PC_DEBUG(pch, "");

	if( pch->xfer_state != srmio_pc_xfer_state_new )
		srmio_pc_xfer_finish( pch, err );

	if( ! (*pch->methods->close)( pch, err ) )
		return false;

	pch->is_open = false;
	return true;
}

bool srmio_pc_set_debugfunc( srmio_pc_t pch,
	srmio_logfunc_t func, void *data )
{
	assert( pch );

	pch->dfunc = func;
	pch->ddata = data;
	return true;
}

bool srmio_pc_set_logfunc( srmio_pc_t pch,
	srmio_logfunc_t func, void *data )
{
	assert( pch );

	pch->lfunc = func;
	pch->ldata = data;
	return true;
}

/*
 * sets the device filename, that's len for communicating.
 * does *not* take ownership of io handle!!! You need to free it yourself!
 */
bool srmio_pc_set_device( srmio_pc_t pch, srmio_io_t h, srmio_error_t *err )
{
	assert( pch );
	assert( h );

	if( pch->is_open ){
		SRMIO_PC_ERROR( pch, err, "device is open, can't change");
		return false;
	}

	pch->io = h;

	return true;
}

/*
 * retrieve the IO handle
 */
bool srmio_pc_get_device( srmio_pc_t pch, srmio_io_t *h, srmio_error_t *err )
{
	(void)err;
	assert( pch );
	assert( h );

	*h = pch->io;
	return true;
}

/*
 * sets the baudrate to use for communication with the PowerControl.
 * with srmio_io_baud_max, all supported baud rates are probed.
 */
bool srmio_pc_set_baudrate( srmio_pc_t pch, srmio_io_baudrate_t rate,
	srmio_error_t *err )
{
	assert( pch );
	assert( rate <= srmio_io_baud_max );

	if( pch->is_open ){
		SRMIO_PC_ERROR( pch, err, "device is open, can't change");
		return false;
	}

	pch->baudrate = rate;
	return true;
}


/*
 * sets the parity to use for communication with the PowerControl.
 * with srmio_io_parity_max, all supported parity settings are probed.
 */
bool srmio_pc_set_parity( srmio_pc_t pch, srmio_io_parity_t parity,
	srmio_error_t *err )
{
	assert( pch );
	assert( parity <= srmio_io_parity_max );

	if( pch->is_open ){
		SRMIO_PC_ERROR( pch, err, "device is open, can't change");
		return false;
	}

	pch->parity = parity;
	return true;
}

bool srmio_pc_get_baudrate( srmio_pc_t pch, srmio_io_baudrate_t *rate,
	srmio_error_t *err )
{
	assert(pch);
	assert(rate);

	if( pch->baudrate >= srmio_io_baud_max ){
		SRMIO_PC_ERROR( pch, err, "baudrate is unset" );
		return false;
	}

	*rate = pch->baudrate;
	return true;
}

bool srmio_pc_get_parity( srmio_pc_t pch, srmio_io_parity_t *parity,
	srmio_error_t *err )
{
	assert(pch);
	assert(parity);

	if( pch->parity >= srmio_io_parity_max ){
		SRMIO_PC_ERROR( pch, err, "parity is unset" );
		return false;
	}

	*parity = pch->parity;
	return true;
}

/*
 * gets the firmware as discovered when opening the device
 */
bool srmio_pc_get_version( srmio_pc_t pch, unsigned *version,
	srmio_error_t *err )
{
	(void)err;
	assert( pch );
	assert( version );

	*version = pch->firmware;
	return true;
}

bool srmio_pc_set_xfer( srmio_pc_t pch, srmio_pc_xfer_type_t type,
	srmio_error_t *err )
{
	(void)err;
	assert( pch );
	assert( type < srmio_pc_xfer_type_max );

	SRMIO_PC_DEBUG(pch,  "%d", type );

	pch->xfer_type = type;

	return true;
}

bool srmio_pc_can_preview( srmio_pc_t conn )
{
	assert(conn);

	return conn->can_preview;
}

bool srmio_pc_xfer_get_blocks( srmio_pc_t conn, size_t *blocks,
	srmio_error_t *err )
{
	(void)err;
	assert( conn );
	assert( blocks );

	*blocks = conn->block_cnt;

	return true;
}


bool srmio_pc_cmd_get_athlete( srmio_pc_t pch, char **athlete,
	srmio_error_t *err )
{
	assert(pch);
	assert(pch->methods->cmd_get_athlete);

	return (*pch->methods->cmd_get_athlete)( pch, athlete, err );
}

bool srmio_pc_cmd_set_time( srmio_pc_t pch, struct tm *t,
	srmio_error_t *err )
{
	assert(pch);
	assert(pch->methods->cmd_set_time);

	return (*pch->methods->cmd_set_time)( pch, t, err );
}

bool srmio_pc_cmd_set_recint( srmio_pc_t pch, srmio_time_t t,
	srmio_error_t *err )
{
	assert(pch);
	assert(pch->methods->cmd_set_recint);

	return (*pch->methods->cmd_set_recint)( pch, t, err );
}

bool srmio_pc_cmd_clear( srmio_pc_t pch, srmio_error_t *err )
{
	assert(pch);
	assert(pch->methods->cmd_clear);

	return (*pch->methods->cmd_clear)( pch, err );
}

bool srmio_pc_xfer_start( srmio_pc_t pch, srmio_error_t *err )
{
	assert(pch);
	assert(pch->methods->xfer_start);

	return (*pch->methods->xfer_start)( pch, err );
}

bool srmio_pc_xfer_block_next( srmio_pc_t pch, srmio_pc_xfer_block_t block )
{
	assert(pch);
	assert( block );
	assert(pch->methods->xfer_block_next);

	return (*pch->methods->xfer_block_next)( pch, block );
}

bool srmio_pc_xfer_chunk_next( srmio_pc_t pch, srmio_chunk_t chunk,
	bool *is_intervall, bool *start_intervall )
{
	assert(pch);
	assert( chunk );
	assert(pch->methods->xfer_chunk_next);

	return (*pch->methods->xfer_chunk_next)( pch, chunk,
		is_intervall, start_intervall );
}

bool srmio_pc_xfer_finish( srmio_pc_t pch, srmio_error_t *err )
{
	assert(pch);
	assert(pch->methods->xfer_finish);

	return (*pch->methods->xfer_finish)( pch, err );
}

srmio_pc_xfer_state_t srmio_pc_xfer_status( srmio_pc_t pch,
	srmio_error_t *err)
{
	assert( pch );

	if( pch->xfer_state == srmio_pc_xfer_state_failed )
		srmio_error_copy( err, &pch->err );

	return pch->xfer_state;
}

bool srmio_pc_xfer_block_progress( srmio_pc_t pch, size_t *block_done )
{
	assert( pch );
	assert(pch->methods->xfer_block_progress);

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
	srmio_progress_t pfunc, void *prog_data,
	srmio_error_t *err  )
{
	int mfirst = -1;
	struct _srmio_pc_xfer_block_t block;
	struct _srmio_chunk_t chunk;
	size_t done_chunks = 0;
	size_t block_cnt, block_num = 0;
	size_t prog_prev = 0, prog_sum=0;

	assert( pch );
	assert( data );

	block.athlete = NULL;

	SRMIO_PC_DEBUG(pch, "");

	if( ! srmio_pc_xfer_start( pch, err ) )
		return false;

	if( ! srmio_pc_xfer_get_blocks( pch, &block_cnt, err ) )
		goto clean;

	srmio_pc_log( pch, "found %d ride blocks", block_cnt );

	if( block_cnt > 1 ){
		if( srmio_pc_can_preview( pch ) ){
			while( srmio_pc_xfer_block_next( pch, &block ) ){
				prog_sum += block.total;
				if( block.athlete )
					free( block.athlete );
				block.athlete = NULL;
			}
			SRMIO_PC_DEBUG(pch, "prog_sum %u", prog_sum );

			/* finalize / restart xfer */
			if( srmio_pc_xfer_state_success !=
				srmio_pc_xfer_status( pch, err ) )

				goto clean;

			srmio_pc_xfer_finish( pch, NULL );

			if( ! srmio_pc_xfer_start( pch, err ) )
				goto clean;
		}
	}

	while( srmio_pc_xfer_block_next( pch, &block ) ){
		bool is_int;
		bool is_first;
		size_t prog_total;

		srmio_pc_log( pch, "downloading ride block %d/%d",
			block_num, block_cnt );

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

				SRMIO_PC_DEBUG( pch,
					"prog_total %d, prog_prev %d, block_done %d",
					prog_total, prog_prev, block_done );
				(*pfunc)( prog_total, block_done, prog_data );
			}


			if( ! srmio_data_add_chunk( data, &chunk, err ) )
				goto clean;

			++done_chunks;

			/* finish previous marker */
			if( mfirst >= 0 && ( ! is_int || is_first ) )
				if( ! srmio_data_add_marker( data, mfirst,
					data->cused -1, err ) )

					goto clean;

			/* start marker */
			if( is_first ){
				mfirst = (int)data->cused;
				SRMIO_PC_DEBUG(pch,  "new marker at %d", mfirst );

			} else if( ! is_int ){
				mfirst = -1;

			}
		}

		/* finalize marker at block end */
		if( mfirst >= 0 ){
			if( ! srmio_data_add_marker( data, mfirst,
				data->cused -1, err ) )

				goto clean;

			mfirst = -1;
		}

		if( prog_sum )
			prog_prev += block.total;
		else
			prog_prev += 1000;

		free( block.athlete );
		block.athlete = NULL;

		++block_num;
	}

	if( ! done_chunks ){
		SRMIO_PC_ERROR( pch, err, "no data");
		goto clean;
	}

	if( srmio_pc_xfer_state_success != srmio_pc_xfer_status( pch, err) )
		goto clean;

	srmio_pc_xfer_finish( pch, NULL  );

	srmio_pc_log( pch, "got %d records", data->cused );

	return true;

clean:
	srmio_pc_xfer_finish( pch, NULL );
	return false;
}

