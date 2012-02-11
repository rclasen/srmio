/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _SRMIO_PC_H
#define _SRMIO_PC_H


#include "common.h"

typedef void (*srmio_pc_method_void)( srmio_pc_t h );
typedef bool (*srmio_pc_method_bool)( srmio_pc_t h, srmio_error_t *err );
typedef bool (*srmio_pc_method_athlete)( srmio_pc_t h, char **athlete, srmio_error_t *err );
typedef bool (*srmio_pc_method_tm)( srmio_pc_t h, struct tm *tp, srmio_error_t *err );
typedef bool (*srmio_pc_method_time)( srmio_pc_t h, srmio_time_t t, srmio_error_t *err );
typedef bool (*srmio_pc_method_block)( srmio_pc_t h, srmio_pc_xfer_block_t block );
typedef bool (*srmio_pc_method_chunk)( srmio_pc_t h, srmio_chunk_t chunk,
	bool *is_intervall, bool *start_intervall );
typedef bool (*srmio_pc_method_progress)( srmio_pc_t h, size_t *block_done );

typedef struct {
	srmio_pc_method_void		free;
	srmio_pc_method_bool		open;
	srmio_pc_method_bool		close;
	srmio_pc_method_athlete		cmd_get_athlete;
	srmio_pc_method_tm		cmd_set_time;
	srmio_pc_method_time		cmd_set_recint;
	srmio_pc_method_bool		cmd_clear;
	srmio_pc_method_bool		xfer_start;
	srmio_pc_method_block		xfer_block_next;
	srmio_pc_method_progress	xfer_block_progress;
	srmio_pc_method_chunk		xfer_chunk_next;
	srmio_pc_method_bool		xfer_finish;
} srmio_pc_methods_t;

struct _srmio_pc_t {
	/* debug */
	srmio_logfunc_t			dfunc;
	void *				ddata;

	/* log */
	srmio_logfunc_t			lfunc;
	void *				ldata;

	/* io */
	srmio_io_baudrate_t		baudrate;
	srmio_io_parity_t		parity;
	srmio_io_t			io;

	bool				can_preview;

	/* conection */
	unsigned			firmware;
	bool				is_open;

	/* xfer */
	srmio_error_t			err;
	srmio_pc_xfer_type_t		xfer_type;
	srmio_pc_xfer_state_t		xfer_state;
	size_t				block_cnt;

	/* object model */
	const srmio_pc_methods_t	*methods;
	void				*child;
};

void srmio_pc_log( srmio_pc_t pch, const char *fmt, ... );

srmio_pc_t srmio_pc_new( const srmio_pc_methods_t *methods, void *child, srmio_error_t *err );

#define SRMIO_PC_DEBUG(pch, fmt,...) \
	srmio_debug( pch->dfunc, pch->ddata, \
	"%s: " fmt, __func__, ##__VA_ARGS__ );
#define SRMIO_PC_DUMP(pch, buf, len, fmt,...) \
	srmio_dumphex( pch->dfunc, pch->ddata, buf, len, \
	"%s: " fmt, __func__, ##__VA_ARGS__ );

#define SRMIO_PC_LOG( pch, fmt, ... )\
	SRMIO_PC_DEBUG(pch, fmt, ##__VA_ARGS__ ); \
	srmio_pc_log( pch, fmt, ##__VA_ARGS__ );

#define SRMIO_PC_ERROR(pch, err, fmt,...) { \
	SRMIO_PC_DEBUG(pch, fmt, ##__VA_ARGS__ ); \
	srmio_error_set( err, fmt, ##__VA_ARGS__ ); \
	}
#define SRMIO_PC_ERRNO( pch, err, fmt, ... ) { \
	SRMIO_PC_DEBUG( pch, fmt ":%s", ##__VA_ARGS__, \
		strerror(errno)  ); \
	srmio_error_set( err, fmt ": %s", ##__VA_ARGS__, strerror(errno) ); \
	}

#endif // _SRMIO_PC_H
