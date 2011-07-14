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

typedef uint64_t srm_time_t;	/* seconds since epoch * 10 */

/* actual data tuple as retrieved from PCV or file */
struct _srm_chunk_t {
	srm_time_t	time;	/* chunk end time */
	srm_time_t	dur;	/* chunk duration */
	double		temp;	/* temperature °C */
	unsigned	pwr;	/* avg power W */
	double		speed;	/* avg speed km/h */
	unsigned	cad;	/* avg cadence 1/min */
	unsigned	hr;	/* avg hr 1/min */
	long		ele;	/* elevation in meter */
};
typedef struct _srm_chunk_t *srm_chunk_t;

srm_chunk_t srm_chunk_new( void );
srm_chunk_t srm_chunk_clone( srm_chunk_t chunk );
void srm_chunk_free( srm_chunk_t chunk );


/* marker within srm_data_t pointing to the relvant chunks */
struct _srm_marker_t {
	unsigned	first;	/* num. of first marked chunk */
	unsigned	last;	/* num. of last marked chunk */
	char		*notes;	/* user's notes for this marker */
};
typedef struct _srm_marker_t *srm_marker_t;

srm_marker_t srm_marker_new( void );
srm_marker_t srm_marker_clone( srm_marker_t marker );
void srm_marker_free( srm_marker_t marker );



/* data structure to hold all information retrieved 
 * from PCV or file */
struct _srm_data_t {
	double		slope;
	unsigned	zeropos;
	unsigned	circum;
	char		*notes;

	/* array of chunks */
	srm_chunk_t	 *chunks;
	unsigned	cused;
	unsigned	cavail;

	/* array of marker */
	srm_marker_t	 *marker;
	unsigned	mused;
	unsigned	mavail;
};
typedef struct _srm_data_t *srm_data_t;

srm_data_t srm_data_new( void );
srm_data_t srm_data_fixup( srm_data_t data );

int srm_data_add_chunkp( srm_data_t data, srm_chunk_t chunk );
int srm_data_add_chunk( srm_data_t data, srm_chunk_t chunk );
int srm_data_add_markerp( srm_data_t data, srm_marker_t mark );
int srm_data_add_marker( srm_data_t data, unsigned first, unsigned last );

srm_time_t srm_data_time_start( srm_data_t data );
srm_time_t srm_data_recint( srm_data_t data );
srm_marker_t *srm_data_blocks( srm_data_t data );

void srm_data_free( srm_data_t data );



/************************************************************
 *
 * from file_srm.c
 *
 ************************************************************/

srm_data_t srm_data_read( const char *fname ); /* TODO: obsolete, will go away */
srm_data_t srm_data_read_srm( const char *fname );
int srm_data_write_srm7( srm_data_t data, const char *fname );



/************************************************************
 *
 * from file_wkt.c
 *
 ************************************************************/

int srm_data_write_wkt( srm_data_t data, const char *fname );


/************************************************************
 *
 * from ftypes.c
 *
 ************************************************************/

typedef enum {
	srm_ftype_unknown,
	srm_ftype_srm5,
	srm_ftype_srm6,
	srm_ftype_srm7,
	srm_ftype_wkt,
	srm_ftype_max,
} srm_ftype_t;

srm_ftype_t srm_ftype_from_string( const char *type );

srm_data_t srm_data_read_ftype( srm_ftype_t ftype, const char *fname );
int srm_data_write_ftype( srm_data_t data, srm_ftype_t ftype, const char *fname );

/************************************************************
 *
 * from pc5.c
 *
 ************************************************************/

#define SRM_BUFSIZE	1024
#define SRM_BLOCKCHUNKS	11u

typedef void (*srmpc_log_callback_t)( const char *msg );

/* connection handle */
struct _srmpc_conn_t {
	int		fd;
	struct termios	oldios;
	int		stxetx;	/* use stx/etx headers + encoding? */
	time_t		nready;	/* PC accepts next command */
	srmpc_log_callback_t	lfunc;	/* logging callback */
	int		cmd_running; /* command is going on */
};
typedef struct _srmpc_conn_t *srmpc_conn_t;


srmpc_conn_t srmpc_open( const char *fname, int force,
	srmpc_log_callback_t lfunc );
void srmpc_close( srmpc_conn_t conn );



int srmpc_get_version( srmpc_conn_t conn );
char *srmpc_get_athlete( srmpc_conn_t conn );

int srmpc_get_time( srmpc_conn_t conn, struct tm *timep ); 
int srmpc_set_time( srmpc_conn_t conn, struct tm *timep ); 

int srmpc_get_circum( srmpc_conn_t conn );

double srmpc_get_slope( srmpc_conn_t conn );

int srmpc_get_zeropos( srmpc_conn_t conn );

int srmpc_get_recint( srmpc_conn_t conn );
int srmpc_set_recint( srmpc_conn_t conn, srm_time_t recint );


/* handle that's used within srmpc_get_chunks* */
struct _srmpc_get_chunk_t {
	/* whole downlad */
	srmpc_conn_t		conn;
	struct tm		pctime;
	unsigned		blocks;
	unsigned char		buf[SRM_BUFSIZE];
	int			finished;

	/* current block */
	unsigned		blocknum;	/* 0..blocks-1 */
	srm_time_t		bstart;
	unsigned long		dist;
	int			temp;	
	srm_time_t		recint;

	/* current chunk */
	unsigned		chunknum;	/* within block 0..10 */
	int			isfirst;	/* ... of marker */
	int			iscont;		/* ... of marker */
};
typedef struct _srmpc_get_chunk_t *srmpc_get_chunk_t;

srmpc_get_chunk_t srmpc_get_chunk_start( srmpc_conn_t conn, int getall );
srm_chunk_t srmpc_get_chunk_next( srmpc_get_chunk_t handle );
void srmpc_get_chunk_done( srmpc_get_chunk_t handle );

/* alternative / obsolete callback-based interface: */
typedef int (*srmpc_chunk_callback_t)( srmpc_get_chunk_t gh,
	void *cbdata, srm_chunk_t chunk );
int srmpc_get_chunks( srmpc_conn_t conn, int getall,
	srmpc_chunk_callback_t cfunc, void *cbdata );

int srmpc_clear_chunks( srmpc_conn_t conn );

srm_data_t srmpc_get_data( srmpc_conn_t conn, int getall, int fixup );

# ifdef __cplusplus
}
# endif


#endif /* _SRMIO_H */
