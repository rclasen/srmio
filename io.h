/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _IO_H
#define _IO_H


#include "srmio.h"

typedef void (*srmio_io_method_void)( srmio_io_t h );
typedef bool (*srmio_io_method_bool)( srmio_io_t h, srmio_error_t *err );
typedef int (*srmio_io_method_buf)( srmio_io_t h, unsigned char *buf, size_t len, srmio_error_t *err );
typedef int (*srmio_io_method_constbuf)( srmio_io_t h, const unsigned char *buf, size_t len, srmio_error_t *err );

typedef struct {
	srmio_io_method_void		free;
	srmio_io_method_bool		open;
	srmio_io_method_bool		close;
	srmio_io_method_bool		update;
	srmio_io_method_buf		read;
	srmio_io_method_constbuf	write;
	srmio_io_method_bool		flush;
	srmio_io_method_bool		send_break;
} srmio_io_methods_t;

struct _srmio_io_t {
	srmio_io_baudrate_t	baudrate;
	srmio_io_parity_t	parity;
	srmio_io_flow_t		flow;
	const srmio_io_methods_t	*methods;
	bool			is_open;
	void			*child;
};

srmio_io_t srmio_io_new( const srmio_io_methods_t *methods, void *child, srmio_error_t *err );

#include "common.h"

#endif
