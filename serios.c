/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

/* TODO: help identifying interfaces

that's likely a task for hald:

$ hal-find-by-capability --capability serial
/org/freedesktop/Hal/devices/usb_device_403_6001_A2QU4AND_if0_serial_usb_0
/org/freedesktop/Hal/devices/usb_device_67b_2303_noserial_if0_serial_usb_0
/org/freedesktop/Hal/devices/usb_device_403_6001_ftE2HGRM_if0_serial_usb_0
/org/freedesktop/Hal/devices/pnp_PNP0501_serial_platform_0

$ hal-get-property \
	--udi /org/freedesktop/Hal/devices/usb_device_403_6001_A2QU4AND_if0_serial_usb_0 \
	--key linux.device_file
/dev/ttyUSB2

or with libudev

or "solid" from kdelibs

*/

#include "serio.h"

#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif


static const tcflag_t _srmio_ios_baud[srmio_io_baud_max] = {
	B2400,
	B4800,
	B9600,
	B19200,
	B38400,
};


/*
 * handle
 */
struct _srmio_ios_t {
	char			*fname;
	int			fd;
	struct termios		oldios;
};
typedef struct _srmio_ios_t *srmio_ios_t;

#define SELF(x)	((srmio_ios_t)x->child)


static int _srmio_ios_write( srmio_io_t h, const unsigned char *buf,
	size_t len, srmio_error_t *err )
{
	int ret;

	assert( h );
	assert( buf );

	ret = write( SELF(h)->fd, buf, len );

	if( ret < 0 ){
		srmio_error_errno( err, "ios write" );
	} else {
		tcdrain( SELF(h)->fd );
	}

	return ret;
}

static int _srmio_ios_read( srmio_io_t h, unsigned char *buf, size_t len,
	srmio_error_t *err )
{
	size_t got = 0;
	int ret;

	assert( h );
	assert( buf );
	assert( SELF(h)->fd >= 0 );

	/* as the whole read() uses VTIME as timeout, this causes problems
	 * for long reads. Therefore we read bytes individually */

	while( got < len ){
		ret = read( SELF(h)->fd, &buf[got], 1 );
		if( ret < 0 ){ /* error, abort */
			srmio_error_errno( err, "ios read" );
			return -1;
		}

		if( ret < 1 ) /* no further data */
			return got;

		got += ret;
	}

	return got;
}


static bool _srmio_ios_flush( srmio_io_t h, srmio_error_t *err )
{
	assert( h );
	assert( SELF(h)->fd >= 0 );

	if( 0 != tcflush( SELF(h)->fd, TCIOFLUSH ) ){
		srmio_error_errno( err, "ios flush" );
		return false;
	}

	return true;
}

static bool _srmio_ios_send_break( srmio_io_t h, srmio_error_t *err )
{
	assert( h );
	assert( SELF(h)->fd >= 0 );

	if( 0 != tcsendbreak( SELF(h)->fd, 0 ) ){
		srmio_error_errno( err, "ios break" );
		return false;
	}

	return true;
}

static bool _srmio_ios_update( srmio_io_t h, srmio_error_t *err )
{
	struct termios ios;

	memset(&ios, 0, sizeof(struct termios));
#ifdef HAVE_CFMAKERAW
	/* not sure, if this is needed right after memset ...: */
	cfmakeraw( &ios );
#endif

	/* TODO: make other termios parameters configurable */

	ios.c_iflag = IGNPAR;
	ios.c_cflag = CS8 | CLOCAL | CREAD;

	switch( h->flow ){
	  case srmio_io_flow_none:
		/* do nothing */
		break;

	  case srmio_io_flow_xonoff:
		ios.c_iflag |= IXON | IXOFF;
		break;

	  case srmio_io_flow_rtscts:
		ios.c_cflag |= CRTSCTS;
		break;

	  default:
		srmio_error_set( err, "ios invalid flow control: %u", h->flow);
		return false;
	}

	switch( h->parity ){
	  case srmio_io_parity_none:
		/* do nothing */
		break;

	  case srmio_io_parity_even:
		ios.c_cflag |= PARENB;
		break;

	  case srmio_io_parity_odd:
		ios.c_cflag |= PARENB | PARODD;
		break;

	  default:
		srmio_error_set( err, "ios invalid parity: %u", h->parity );
		return false;
	}

	if( 0 > cfsetispeed( &ios, _srmio_ios_baud[h->baudrate] ) ){
		srmio_error_errno( err, "ios setispieed" );
		return false;
	}

	if( 0 > cfsetospeed( &ios, _srmio_ios_baud[h->baudrate] ) ){
		srmio_error_errno( err, "ios setospieed" );
		return false;
	}

	/* wait max 1 sec for whole read() */
	ios.c_cc[VMIN] = 0;
	ios.c_cc[VTIME] = 10;

	if( tcsetattr( SELF(h)->fd, TCSANOW, &ios ) ){
		srmio_error_errno( err, "ios settattr" );
		return false;
	}

	return true;
}


static bool _srmio_ios_close( srmio_io_t h, srmio_error_t *err )
{
	(void)err;
	assert( h );

	if( SELF(h)->fd < 0 )
		return true;

	tcsetattr( SELF(h)->fd, TCSANOW, &SELF(h)->oldios );

	close( SELF(h)->fd );
	SELF(h)->fd = -1;

	/* TODO: unlock */

	return true;
}

static bool _srmio_ios_open( srmio_io_t h, srmio_error_t *err )
{
	(void)h;

	/* TODO: lock */

	if( 0 > (SELF(h)->fd = open( SELF(h)->fname, O_RDWR | O_NOCTTY))){
		srmio_error_errno( err, "ios open" );
		return false;
	}

	/* get serial comm parameter for restore on close*/
	if( tcgetattr( SELF(h)->fd, &SELF(h)->oldios ) ){
		srmio_error_errno( err, "ios getattr" );
		goto clean;
	}

	if( ! srmio_io_update( h, err ) )
		goto clean;

	return true;

clean:
	close(SELF(h)->fd);
	SELF(h)->fd = -1;
	return false;
}

static void _srmio_ios_free( srmio_io_t h )
{
	free(SELF(h)->fname);
	free(SELF(h));
}

static const srmio_io_methods_t _ios_methods = {
	.free		= _srmio_ios_free,
	.open		= _srmio_ios_open,
	.close		= _srmio_ios_close,
	.update		= _srmio_ios_update,
	.read		= _srmio_ios_read,
	.write		= _srmio_ios_write,
	.flush		= _srmio_ios_flush,
	.send_break	= _srmio_ios_send_break,
};

srmio_io_t srmio_ios_new( const char *fname, srmio_error_t *err )
{
	srmio_ios_t self;
	srmio_io_t h;

	assert( fname );
	if( NULL == ( self = malloc( sizeof(struct _srmio_ios_t)))){
		srmio_error_errno( err, "ios new" );
		return NULL;
	}

	self->fd = -1;

	if( NULL == ( self->fname = strdup( fname ) )){
		srmio_error_errno( err, "ios new fname" );
		goto clean1;
	}

	if( NULL == ( h = srmio_io_new( &_ios_methods, (void*)self, err ) ))
		goto clean2;

	return h;
clean2:
	free( self->fname );
clean1:
	free( self );
	return NULL;
}

