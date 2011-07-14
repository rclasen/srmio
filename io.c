/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */


#include "io.h"

static const unsigned _srmio_io_baudnames[srmio_io_baud_max] = {
	2400,
	4800,
	9600,
	19200,
	38400,
};

static const char _srmio_io_paritynames[srmio_io_parity_max] = {
	'n',
	'e',
	'o',
};

static const char* _srmio_io_flownames[srmio_io_flow_max] = {
	"none",
	"xonoxff",
	"rtscts",
};

bool srmio_io_baud2name( srmio_io_baudrate_t rate, unsigned *name )
{
	assert( name );

	if( rate >= srmio_io_baud_max )
		return false;

	*name = _srmio_io_baudnames[rate];
	return true;
}

bool srmio_io_name2baud( unsigned name, srmio_io_baudrate_t *rate )
{
	srmio_io_baudrate_t i;

	assert( rate );

	for( i = 0; i < srmio_io_baud_max; ++i ){
		if( _srmio_io_baudnames[i] == name ){
			*rate = i;
			return true;
		}
	}

	return false;
}

bool srmio_io_parity2name( srmio_io_parity_t rate, char *name )
{
	assert( name );

	if( rate >= srmio_io_parity_max )
		return false;

	*name = _srmio_io_paritynames[rate];
	return true;
}

bool srmio_io_name2parity( char name, srmio_io_parity_t *parity )
{
	assert( parity );

	switch( name ){
	  case 'n':
		*parity = srmio_io_parity_none;
		return true;

	  case 'e':
		*parity = srmio_io_parity_even;
		return true;

	  case 'o':
		*parity = srmio_io_parity_odd;
		return true;

	  default:
		break;

	}

	return false;
}

bool srmio_io_flow2name( srmio_io_flow_t flow, const char **name )
{
	assert( name );

	if( flow >= srmio_io_flow_max )
		return false;

	*name = _srmio_io_flownames[flow];
	return true;
}

bool srmio_io_name2flow( const char *name, srmio_io_flow_t *flow )
{
	srmio_io_flow_t i;

	assert( flow );

	for( i = 0; i < srmio_io_flow_max; ++i ){
		if( 0 == strcmp( _srmio_io_flownames[i], name) ){
			*flow = i;
			return true;
		}
	}

	return false;
}

srmio_io_t srmio_io_new( const srmio_io_methods_t *methods, void *child )
{
	srmio_io_t h;

	assert( methods );
	assert( child );

	if( NULL == ( h = malloc(sizeof(struct _srmio_io_t))))
		return NULL;
	memset(h, 0, sizeof(struct _srmio_io_t) );

	h->methods = methods;
	h->child = child;

	return h;
}

void srmio_io_free( srmio_io_t h )
{
	assert( h );

	if( srmio_io_is_open( h ) )
		srmio_io_close( h );

	(*h->methods->free)( h );
	free( h );
}

bool srmio_io_is_open( srmio_io_t h )
{
	assert(h);

	return h->is_open;
}

bool srmio_io_set_baudrate( srmio_io_t h, srmio_io_baudrate_t rate )
{
	assert( h );

	h->baudrate = rate;

	return true;
}

bool srmio_io_set_parity( srmio_io_t h, srmio_io_parity_t parity )
{
	assert( h );

	h->parity = parity;

	return true;
}

bool srmio_io_set_flow( srmio_io_t h, srmio_io_flow_t flow )
{
	assert( h );

	h->flow = flow;

	return true;
}

bool srmio_io_update( srmio_io_t h )
{
	assert( h );

	return (*h->methods->update)( h );
}


bool srmio_io_open( srmio_io_t h )
{
	assert(h);

	if( h->is_open ){
		errno = EBUSY;
		return false;
	}

	h->is_open = (*h->methods->open)( h );

	return h->is_open;
}

bool srmio_io_close( srmio_io_t h )
{
	bool ret;

	assert(h);

	ret = (*h->methods->close)( h );
	if( ret )
		h->is_open = false;

	return ret;
}

int srmio_io_write( srmio_io_t h, const unsigned char *buf, size_t len )
{
	assert(h);

	return (*h->methods->write)( h, buf, len );
}

int srmio_io_read( srmio_io_t h, unsigned char *buf, size_t len )
{
	assert(h);

	return (*h->methods->read)( h, buf, len );
}

bool srmio_io_flush( srmio_io_t h )
{
	assert(h);

	return (*h->methods->flush)( h );
}

bool srmio_io_send_break( srmio_io_t h )
{
	assert(h);

	return (*h->methods->send_break)( h );
}



