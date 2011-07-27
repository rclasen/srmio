/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _SRMIO_H
#define _SRMIO_H

#include <stdio.h>
#include "srmio_config.h"

# ifdef __cplusplus
extern "C"
{
# endif

/* please check the actual source for descriptions of the individual
 * functions */

/************************************************************
 *
 * from err.c
 *
 ************************************************************/

#define SRMIO_ERROR_MSG_SIZE	512

struct _srmio_error_t {
	char	message[SRMIO_ERROR_MSG_SIZE];
};
typedef struct _srmio_error_t srmio_error_t;

/************************************************************
 *
 * from chunk.c
 *
 ************************************************************/

typedef uint64_t srmio_time_t;	/* seconds since epoch * 10 */

/* actual data tuple as retrieved from PowerControl or file */
struct _srmio_chunk_t {
	srmio_time_t	time;	/* chunk end time */
	srmio_time_t	dur;	/* chunk duration */
	double		temp;	/* temperature °C */
	unsigned	pwr;	/* avg power W */
	double		speed;	/* avg speed km/h */
	unsigned	cad;	/* avg cadence 1/min */
	unsigned	hr;	/* avg hr 1/min */
	long		ele;	/* elevation in meter */
};
typedef struct _srmio_chunk_t *srmio_chunk_t;

srmio_chunk_t srmio_chunk_new( srmio_error_t *err );
srmio_chunk_t srmio_chunk_clone( srmio_chunk_t chunk, srmio_error_t *err );
void srmio_chunk_free( srmio_chunk_t chunk );


/************************************************************
 *
 * from marker.c
 *
 ************************************************************/

struct _srmio_marker_t {
	unsigned	first;	/* num. of first marked chunk */
	unsigned	last;	/* num. of last marked chunk */
	char		*notes;	/* user's notes for this marker */
};
typedef struct _srmio_marker_t *srmio_marker_t;

srmio_marker_t srmio_marker_new( srmio_error_t *err );
srmio_marker_t srmio_marker_clone( srmio_marker_t marker, srmio_error_t *err );
void srmio_marker_free( srmio_marker_t marker );


/************************************************************
 *
 * from data.c
 *
 ************************************************************/

/* data structure to hold all information retrieved
 * from PowerControl or file */
struct _srmio_data_t {
	double		slope;
	unsigned	zeropos;
	unsigned	circum;
	char		*notes;
	char		*athlete;	/* athlete name */

	/* array of chunks */
	srmio_chunk_t	 *chunks;
	unsigned	cused;
	unsigned	cavail;

	/* array of marker */
	srmio_marker_t	 *marker;
	unsigned	mused;
	unsigned	mavail;
};
typedef struct _srmio_data_t *srmio_data_t;

srmio_data_t srmio_data_new( srmio_error_t *err );
srmio_data_t srmio_data_header( srmio_data_t src, srmio_error_t *err );
void srmio_data_free( srmio_data_t data );

bool srmio_data_add_chunkp( srmio_data_t data, srmio_chunk_t chunk, srmio_error_t *err );
bool srmio_data_add_chunk( srmio_data_t data, srmio_chunk_t chunk, srmio_error_t *err );
bool srmio_data_add_markerp( srmio_data_t data, srmio_marker_t mark, srmio_error_t *err );
bool srmio_data_add_marker( srmio_data_t data, unsigned first, unsigned last, srmio_error_t *err );

bool srmio_data_time_start( srmio_data_t data, srmio_time_t *start, srmio_error_t *err );
bool srmio_data_recint( srmio_data_t data, srmio_time_t *recint, srmio_error_t *err );
srmio_marker_t *srmio_data_blocks( srmio_data_t data, srmio_error_t *err );



/************************************************************
 *
 * from fixup.c
 *
 ************************************************************/

srmio_data_t srmio_data_fixup( srmio_data_t data, srmio_error_t *err );

/************************************************************
 *
 * from split.c
 *
 ************************************************************/

srmio_data_t *srmio_data_split( srmio_data_t src,
	srmio_time_t gap, srmio_time_t overlap, srmio_error_t *err );


/************************************************************
 *
 * from file_srm.c
 *
 ************************************************************/

srmio_data_t srmio_file_srm_read( FILE *fh, srmio_error_t *err );
bool srmio_file_srm7_write( srmio_data_t data, FILE *fh, srmio_error_t *err );



/************************************************************
 *
 * from file_wkt.c
 *
 ************************************************************/

bool srmio_file_wkt_write( srmio_data_t data, FILE *fh, srmio_error_t *err );


/************************************************************
 *
 * from ftypes.c
 *
 ************************************************************/

typedef enum {
	srmio_ftype_unknown,
	srmio_ftype_srm5,
	srmio_ftype_srm6,
	srmio_ftype_srm7,
	srmio_ftype_wkt,
	srmio_ftype_max,
} srmio_ftype_t;

srmio_ftype_t srmio_ftype_from_string( const char *type );

srmio_data_t srmio_file_ftype_read( srmio_ftype_t ftype, FILE *fh, srmio_error_t *err );
bool srmio_file_ftype_write( srmio_data_t data, srmio_ftype_t ftype, FILE *fh, srmio_error_t *err );

/************************************************************
 *
 * from io.c
 *
 ************************************************************/

typedef enum {
	srmio_io_baud_2400,
	srmio_io_baud_4800,
	srmio_io_baud_9600,
	srmio_io_baud_19200,
	srmio_io_baud_38400,
	srmio_io_baud_max
} srmio_io_baudrate_t;

bool srmio_io_baud2name( srmio_io_baudrate_t rate, unsigned *name );
bool srmio_io_name2baud( unsigned name, srmio_io_baudrate_t *rate );

typedef enum {
	srmio_io_parity_none,
	srmio_io_parity_even,
	srmio_io_parity_odd,
	srmio_io_parity_max,
} srmio_io_parity_t;

bool srmio_io_parity2name( srmio_io_parity_t parity, char *name );
bool srmio_io_name2parity( char name, srmio_io_parity_t *parity );

typedef enum {
	srmio_io_flow_none,
	srmio_io_flow_xonoff,
	srmio_io_flow_rtscts,
	srmio_io_flow_max,
} srmio_io_flow_t;

bool srmio_io_flow2name( srmio_io_flow_t flow, const char **name );
bool srmio_io_name2flow( const char *name, srmio_io_flow_t *parity );

/* IO handle for serial device abstraction */
typedef struct _srmio_io_t *srmio_io_t;

void srmio_io_free( srmio_io_t h );

bool srmio_io_set_baudrate( srmio_io_t h, srmio_io_baudrate_t, srmio_error_t *err );
bool srmio_io_set_parity( srmio_io_t h, srmio_io_parity_t, srmio_error_t *err );
bool srmio_io_set_flow( srmio_io_t h, srmio_io_flow_t, srmio_error_t *err );

bool srmio_io_open( srmio_io_t h, srmio_error_t *err );
bool srmio_io_close( srmio_io_t h, srmio_error_t *err );
bool srmio_io_update( srmio_io_t h, srmio_error_t *err );
bool srmio_io_is_open( srmio_io_t h );

int srmio_io_write( srmio_io_t h, const unsigned char *buf, size_t len, srmio_error_t *err );
int srmio_io_read( srmio_io_t h, unsigned char *buf, size_t len, srmio_error_t *err );
bool srmio_io_flush( srmio_io_t h, srmio_error_t *err );
bool srmio_io_send_break( srmio_io_t h, srmio_error_t *err );


/************************************************************
 *
 * from ios.c
 *
 ************************************************************/

#ifdef SRMIO_HAVE_TERMIOS

srmio_io_t srmio_ios_new( const char *fname, srmio_error_t *err );

#endif

/************************************************************
 *
 * from d2xx.c
 *
 ************************************************************/

#ifdef SRMIO_HAVE_D2XX

srmio_io_t srmio_d2xx_serial_new( const char *serial, srmio_error_t *err );
srmio_io_t srmio_d2xx_description_new( const char *desc, srmio_error_t *err );

#endif

/************************************************************
 *
 * from pc.c
 *
 ************************************************************/

typedef struct _srmio_pc_t *srmio_pc_t;

typedef enum {
	srmio_pc_xfer_state_new,
	srmio_pc_xfer_state_running,
	srmio_pc_xfer_state_success,
	srmio_pc_xfer_state_failed,
	srmio_pc_xfer_state_abort,
	srmio_pc_xfer_state_max,
} srmio_pc_xfer_state_t;


typedef enum {
	srmio_pc_xfer_type_new,
	srmio_pc_xfer_type_all, /* xfer deleted data, as well */
	srmio_pc_xfer_type_max,
} srmio_pc_xfer_type_t;

struct _srmio_pc_xfer_block_t {
	double		slope;
	unsigned	zeropos;
	unsigned	circum;
	char		*athlete;
	size_t		total;	/* $items in this block - for progress indication */
	/* informational only, chunks may disagree!!!:
	 * 0 = unset/unknown
	 */
	srmio_time_t	recint;
	srmio_time_t	start;
	srmio_time_t	end;
};
typedef struct _srmio_pc_xfer_block_t *srmio_pc_xfer_block_t;

void srmio_pc_free( srmio_pc_t conn );

bool srmio_pc_set_device( srmio_pc_t conn, srmio_io_t h, srmio_error_t *err );
bool srmio_pc_get_device( srmio_pc_t conn, srmio_io_t *h, srmio_error_t *err );
bool srmio_pc_set_baudrate( srmio_pc_t pch, srmio_io_baudrate_t rate, srmio_error_t *err );
bool srmio_pc_set_parity( srmio_pc_t pch, srmio_io_parity_t parity, srmio_error_t *err );

bool srmio_pc_open( srmio_pc_t conn, srmio_error_t *err );
bool srmio_pc_close( srmio_pc_t conn, srmio_error_t *err );
bool srmio_pc_get_baudrate( srmio_pc_t pch, srmio_io_baudrate_t *rate, srmio_error_t *err );
bool srmio_pc_get_parity( srmio_pc_t pch, srmio_io_parity_t *parity, srmio_error_t *err );
bool srmio_pc_get_version( srmio_pc_t conn, unsigned *version, srmio_error_t *err );

bool srmio_pc_cmd_get_athlete( srmio_pc_t conn, char **athlete, srmio_error_t *err );

bool srmio_pc_cmd_set_time( srmio_pc_t conn, struct tm *timep, srmio_error_t *err );
bool srmio_pc_cmd_set_recint( srmio_pc_t conn, srmio_time_t recint, srmio_error_t *err );

bool srmio_pc_cmd_clear( srmio_pc_t conn, srmio_error_t *err );

bool srmio_pc_set_xfer( srmio_pc_t conn, srmio_pc_xfer_type_t type, srmio_error_t *err );
bool srmio_pc_can_preview( srmio_pc_t conn );


typedef void (*srmio_progress_t)( size_t total, size_t done,
	void *user_data );

bool srmio_pc_xfer_start( srmio_pc_t conn, srmio_error_t *err );
bool srmio_pc_xfer_get_blocks( srmio_pc_t conn, size_t *blocks, srmio_error_t *err );
bool srmio_pc_xfer_block_next( srmio_pc_t conn, srmio_pc_xfer_block_t block );
bool srmio_pc_xfer_chunk_next( srmio_pc_t conn, srmio_chunk_t chunk,
	bool *is_intervall, bool *start_intervall );
bool srmio_pc_xfer_finish( srmio_pc_t conn, srmio_error_t *err );
srmio_pc_xfer_state_t srmio_pc_xfer_status( srmio_pc_t conn, srmio_error_t *err);
bool srmio_pc_xfer_block_progress( srmio_pc_t conn, size_t *block_done );

bool srmio_pc_xfer_all( srmio_pc_t conn,
	srmio_data_t data,
	srmio_progress_t pfunc, void *user_data, srmio_error_t *err );


/************************************************************
 *
 * from pc5.c
 *
 ************************************************************/


srmio_pc_t srmio_pc5_new( srmio_error_t *err );

// TODO: obsolete, need migration
bool srmio_pc5_cmd_get_time( srmio_pc_t conn, struct tm *timep, srmio_error_t *err );
bool srmio_pc5_cmd_get_circum( srmio_pc_t conn, unsigned *circum, srmio_error_t *err );
bool srmio_pc5_cmd_get_slope( srmio_pc_t conn, double *slope, srmio_error_t *err );
bool srmio_pc5_cmd_get_zeropos( srmio_pc_t conn, unsigned *zeropos, srmio_error_t *err );
bool srmio_pc5_cmd_get_recint( srmio_pc_t conn, srmio_time_t *recint, srmio_error_t *err );


/************************************************************
 *
 * from pc7.c
 *
 ************************************************************/

srmio_pc_t srmio_pc7_new( srmio_error_t *err );

// TODO: obsolete, need migration
bool srmio_pc7_cmd_get_time( srmio_pc_t conn, struct tm *timep, srmio_error_t *err );
bool srmio_pc7_cmd_get_circum( srmio_pc_t conn, unsigned *circum, srmio_error_t *err );
bool srmio_pc7_cmd_get_slope( srmio_pc_t conn, double *slope, srmio_error_t *err );
bool srmio_pc7_cmd_get_zeropos( srmio_pc_t conn, unsigned *zeropos, srmio_error_t *err );
bool srmio_pc7_cmd_get_recint( srmio_pc_t conn, srmio_time_t *recint, srmio_error_t *err );

# ifdef __cplusplus
}
# endif


#endif /* _SRMIO_H */
