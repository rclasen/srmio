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

#include "srmio_config.h"

# ifdef __cplusplus
extern "C"
{
# endif

/* please check the actual source for descriptions of the individual
 * functions */

/************************************************************
 *
 * from data.c
 *
 ************************************************************/

typedef uint64_t srmio_time_t;	/* seconds since epoch * 10 */

/* actual data tuple as retrieved from PCV or file */
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

srmio_chunk_t srmio_chunk_new( void );
srmio_chunk_t srmio_chunk_clone( srmio_chunk_t chunk );
void srmio_chunk_free( srmio_chunk_t chunk );


/* marker within srmio_data_t pointing to the relvant chunks */
struct _srmio_marker_t {
	unsigned	first;	/* num. of first marked chunk */
	unsigned	last;	/* num. of last marked chunk */
	char		*notes;	/* user's notes for this marker */
};
typedef struct _srmio_marker_t *srmio_marker_t;

srmio_marker_t srmio_marker_new( void );
srmio_marker_t srmio_marker_clone( srmio_marker_t marker );
void srmio_marker_free( srmio_marker_t marker );



/* data structure to hold all information retrieved
 * from PCV or file */
struct _srmio_data_t {
	double		slope;
	unsigned	zeropos;
	unsigned	circum;
	char		*notes;

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

srmio_data_t srmio_data_new( void );
srmio_data_t srmio_data_fixup( srmio_data_t data );

int srmio_data_add_chunkp( srmio_data_t data, srmio_chunk_t chunk );
int srmio_data_add_chunk( srmio_data_t data, srmio_chunk_t chunk );
int srmio_data_add_markerp( srmio_data_t data, srmio_marker_t mark );
int srmio_data_add_marker( srmio_data_t data, unsigned first, unsigned last );

srmio_time_t srmio_data_time_start( srmio_data_t data );
srmio_time_t srmio_data_recint( srmio_data_t data );
srmio_marker_t *srmio_data_blocks( srmio_data_t data );

void srmio_data_free( srmio_data_t data );



/************************************************************
 *
 * from file_srm.c
 *
 ************************************************************/

srmio_data_t srmio_file_srm_read( const char *fname );
int srmio_file_srm7_write( srmio_data_t data, const char *fname );



/************************************************************
 *
 * from file_wkt.c
 *
 ************************************************************/

int srmio_file_wkt_write( srmio_data_t data, const char *fname );


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

srmio_data_t srmio_file_ftype_read( srmio_ftype_t ftype, const char *fname );
int srmio_file_ftype_write( srmio_data_t data, srmio_ftype_t ftype, const char *fname );

/************************************************************
 *
 * from pc5.c
 *
 ************************************************************/

typedef void (*srmio_log_callback_t)( const char *msg );

/* connection handle */
typedef struct _srmio_pc_t *srmio_pc_t;


srmio_pc_t srmio_pc_open( const char *fname, int force,
	srmio_log_callback_t lfunc );
void srmio_pc_close( srmio_pc_t conn );



int srmio_pc_get_version( srmio_pc_t conn );
char *srmio_pc_get_athlete( srmio_pc_t conn );

int srmio_pc_get_time( srmio_pc_t conn, struct tm *timep );
int srmio_pc_set_time( srmio_pc_t conn, struct tm *timep );

int srmio_pc_get_circum( srmio_pc_t conn );

double srmio_pc_get_slope( srmio_pc_t conn );

int srmio_pc_get_zeropos( srmio_pc_t conn );

int srmio_pc_get_recint( srmio_pc_t conn );
int srmio_pc_set_recint( srmio_pc_t conn, srmio_time_t recint );


/* handle that's used within srmio_pc_get_chunks* */
typedef struct _srmio_pc_get_chunk_t *srmio_pc_get_chunk_t;

srmio_pc_get_chunk_t srmio_pc_get_chunk_start( srmio_pc_t conn, int getall );
srmio_chunk_t srmio_pc_get_chunk_next( srmio_pc_get_chunk_t handle );
void srmio_pc_get_chunk_done( srmio_pc_get_chunk_t handle );


int srmio_pc_clear_chunks( srmio_pc_t conn );

srmio_data_t srmio_pc_get_data( srmio_pc_t conn, int getall, int fixup );

# ifdef __cplusplus
}
# endif


#endif /* _SRMIO_H */
