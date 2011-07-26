/*
 * Copyright (c) 2011 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include <ftd2xx.h>
#include <dlfcn.h>

#include "io.h"


/************************************************************
 *
 * dynamic lib loading
 */

static void *d2xx_lib = NULL;
static FT_STATUS (*FP_OpenEx)( PVOID pArg1, DWORD Flags, FT_HANDLE *pHandle) = NULL;
static FT_STATUS (*FP_Close)( FT_HANDLE ftHandle) = NULL;
static FT_STATUS (*FP_Purge)( FT_HANDLE ftHandle, DWORD dwMask ) = NULL;
static FT_STATUS (*FP_SetBaudRate)( FT_HANDLE ftHandle, ULONG BaudRate) = NULL;
static FT_STATUS (*FP_SetDataCharacteristics)( FT_HANDLE ftHandle, UCHAR WordLength, UCHAR StopBits, UCHAR Parity) = NULL;
static FT_STATUS (*FP_SetFlowControl)( FT_HANDLE ftHandle, USHORT FlowControl, UCHAR XonChar, UCHAR XoffChar) = NULL;
//static FT_STATUS (*FP_GetQueueStatus)( FT_HANDLE ftHandle, DWORD *dwRxBytes) = NULL;
static FT_STATUS (*FP_SetTimeouts)( FT_HANDLE ftHandle, ULONG ReadTimeout, ULONG WriteTimeout) = NULL;
static FT_STATUS (*FP_Read)( FT_HANDLE ftHandle, LPVOID lpBuffer, DWORD nBufferSize, LPDWORD lpBytesReturned) = NULL;
static FT_STATUS (*FP_Write)( FT_HANDLE ftHandle, LPVOID lpBuffer, DWORD nBufferSize, LPDWORD lpBytesWritten) = NULL;
static FT_STATUS (*FP_SetBreakOn)( FT_HANDLE ftHandle) = NULL;
static FT_STATUS (*FP_SetBreakOff)( FT_HANDLE ftHandle) = NULL;
//static FT_STATUS (*FP_CreateDeviceInfoList)( LPDWORD lpdwNumDevs) = NULL;
//static FT_STATUS (*FP_GetDeviceInfoList)( FT_DEVICE_LIST_INFO_NODE *pDest, LPDWORD lpdwNumDevs) = NULL;


#define SYM(handle, var, name)	\
	if( ! (var = dlsym(handle, name ) )) goto clean;

static bool d2xx_dlopen( void )
{
	fprintf( stderr, "dlopen %s\n", D2XX_LIBNAME );
	if( NULL == (d2xx_lib = dlopen( D2XX_LIBNAME, RTLD_NOW ) )){
		fprintf( stderr, "dlopen(%s) failed:%s\n", D2XX_LIBNAME,
			dlerror() );
		return false;
	}

	SYM(d2xx_lib, FP_OpenEx, "FT_OpenEx" );
	SYM(d2xx_lib, FP_Close, "FT_Close" );
	SYM(d2xx_lib, FP_Purge, "FT_Purge" );
	SYM(d2xx_lib, FP_SetBaudRate, "FT_SetBaudRate" );
	SYM(d2xx_lib, FP_SetDataCharacteristics, "FT_SetDataCharacteristics" );
	SYM(d2xx_lib, FP_SetFlowControl, "FT_SetFlowControl" );
	//SYM(d2xx_lib, FP_GetQueueStatus, "FT_GetQueueStatus" );
	SYM(d2xx_lib, FP_SetTimeouts, "FT_SetTimeouts" );
	SYM(d2xx_lib, FP_Read, "FT_Read" );
	SYM(d2xx_lib, FP_Write, "FT_Write" );
	SYM(d2xx_lib, FP_SetBreakOn, "FT_SetBreakOn" );
	SYM(d2xx_lib, FP_SetBreakOff, "FT_SetBreakOff" );
//	SYM(d2xx_lib, FP_CreateDeviceInfoList, "FT_CreateDeviceInfoList" );
//	SYM(d2xx_lib, FP_GetDeviceInfoList, "FT_GetDeviceInfoList" );

	return true;
clean:
	dlclose( d2xx_lib );
	d2xx_lib = NULL;
	return false;
}


static int d2xx_errno( FT_STATUS status )
{
	fprintf(stderr,"status: %u\n", status );
	switch( status ){
          case FT_OK:
		return 0;

          case FT_INVALID_HANDLE:
          case FT_INVALID_PARAMETER:
          case FT_INVALID_BAUD_RATE:
          case FT_INVALID_ARGS:
		return EINVAL;

          case FT_IO_ERROR:
		return EIO;

          case FT_NOT_SUPPORTED:
		return ENOTSUP;

          case FT_DEVICE_NOT_FOUND:
		return ENODEV;

          case FT_DEVICE_NOT_OPENED:
          case FT_DEVICE_NOT_OPENED_FOR_ERASE:
          case FT_DEVICE_NOT_OPENED_FOR_WRITE:
		return EBADF;

          case FT_INSUFFICIENT_RESOURCES:
		return ENOMEM;

          case FT_FAILED_TO_WRITE_DEVICE:
          case FT_EEPROM_READ_FAILED:
          case FT_EEPROM_WRITE_FAILED:
          case FT_EEPROM_ERASE_FAILED:
          case FT_EEPROM_NOT_PRESENT:
          case FT_EEPROM_NOT_PROGRAMMED:

          case FT_OTHER_ERROR:
		return EIO;
	}

	return EIO;
}


/************************************************************
 *
 * srmio
 */


struct _srmio_d2xx_t {
	char			*identifier;
	DWORD			id_flag;
	FT_HANDLE		ft;
	bool			is_open;
};
typedef struct _srmio_d2xx_t *srmio_d2xx_t;

#define SELF(x)	((srmio_d2xx_t)x->child)


static int _srmio_d2xx_write( srmio_io_t h, const unsigned char *buf, size_t len )
{
	FT_STATUS ret;
	DWORD written;

	assert( h );
	assert( SELF(h)->is_open );
	assert( buf );

	ret = (*FP_Write)( SELF(h)->ft, (unsigned char *)buf, len, &written );
	if( ret == FT_OK )
		return written;

	errno = d2xx_errno( ret );
	return -1;
}

static int _srmio_d2xx_read( srmio_io_t h, unsigned char *buf, size_t len )
{
	DWORD total = 0;

	assert( h );
	assert( buf );
	assert( SELF(h)->is_open );

	while( total < len ){
		FT_STATUS ret;
		DWORD got;

		ret = (*FP_Read)( SELF(h)->ft, &buf[total], 1, &got );
		if( ret != FT_OK ){
			errno = d2xx_errno( ret );
			return -1;
		}

		if( got < 1 ){
			errno = ETIMEDOUT;
			return -1;
		}

		total += got;
	}

	return total;
}


static bool _srmio_d2xx_flush( srmio_io_t h )
{
	FT_STATUS ret;

	assert( h );
	assert( SELF(h)->is_open );

	ret = (*FP_Purge)( SELF(h)->ft, FT_PURGE_RX | FT_PURGE_TX );
	if( ret != FT_OK ){
		errno = d2xx_errno( ret );
		return false;
	}

	return true;
}

static bool _srmio_d2xx_send_break( srmio_io_t h )
{
	struct timespec tspec = {
		.tv_sec = 0,
		.tv_nsec = 250000000,
	}; // 0.25 sec
	FT_STATUS ret;

	assert( h );
	assert( SELF(h)->is_open );

	ret = (*FP_SetBreakOn)( SELF(h)->ft );
	if( ret != FT_OK ){
		errno = d2xx_errno( ret );
		return false;
	}

	nanosleep( &tspec, NULL );

	ret = (*FP_SetBreakOff)( SELF(h)->ft );
	if( ret != FT_OK ){
		errno = d2xx_errno( ret );
		return false;
	}

	return true;
}

static bool _srmio_d2xx_update( srmio_io_t h )
{
	FT_STATUS ret;
	unsigned rate;
	UCHAR parity;
	USHORT flow;

	if( ! srmio_io_baud2name( h->baudrate, &rate ) ){
		errno = EINVAL;
		return false;
	}

	ret = (*FP_SetBaudRate)( SELF(h)->ft, rate );
	if( ret != FT_OK ){
		errno = d2xx_errno( ret );
		return false;
	}

	switch( h->parity ){
	  case srmio_io_parity_none:
		parity = FT_PARITY_NONE;
		break;

	  case srmio_io_parity_even:
		parity = FT_PARITY_EVEN;
		break;

	  case srmio_io_parity_odd:
		parity = FT_PARITY_ODD;
		break;

	  default:
		errno = EINVAL;
		return false;
	}
	ret = (*FP_SetDataCharacteristics)( SELF(h)->ft, FT_BITS_8,
		FT_STOP_BITS_1, parity );
	if( ret != FT_OK ){
		errno = d2xx_errno( ret );
		return false;
	}

	switch( h->flow ){
	  case srmio_io_flow_none:
		flow = FT_FLOW_NONE;
		break;

	  case srmio_io_flow_xonoff:
		flow = FT_FLOW_XON_XOFF;
		break;

	  case srmio_io_flow_rtscts:
		flow = FT_FLOW_RTS_CTS;
		break;

	  default:
		errno = EINVAL;
		return false;
	}
	ret = (*FP_SetFlowControl)( SELF(h)->ft, flow, 0, 0 );
	if( ret != FT_OK ){
		errno = d2xx_errno( ret );
		return false;
	}

	ret = (*FP_SetTimeouts)( SELF(h)->ft, 1000, 1000 );
	if( ret != FT_OK ){
		errno = d2xx_errno( ret );
		return false;
	}

	return true;
}


static bool _srmio_d2xx_close( srmio_io_t h )
{
	FT_STATUS ret;

	assert( h );

	if( ! SELF(h)->is_open )
		return true;

	SELF(h)->is_open = false;

	ret = (*FP_Close)( SELF(h)->ft );
	if( ret != FT_OK ){
		errno = d2xx_errno( ret );
		return false;
	}

	return true;
}

static bool _srmio_d2xx_open( srmio_io_t h )
{
	FT_STATUS ret;

	assert(h);
	assert(SELF(h)->identifier);

	ret = (*FP_OpenEx)( SELF(h)->identifier, SELF(h)->id_flag, &SELF(h)->ft );
	if( ret != FT_OK ){
		errno = d2xx_errno( ret );
		return false;
	}

	if( ! srmio_io_update( h ) )
		goto clean;

	SELF(h)->is_open = true;

	return true;

clean:
	(*FP_Close)( SELF(h)->ft );
	return false;
}

static void _srmio_d2xx_free( srmio_io_t h )
{
	free(SELF(h)->identifier);
	free(SELF(h));
}

static const srmio_io_methods_t _d2xx_methods = {
	.free		= _srmio_d2xx_free,
	.open		= _srmio_d2xx_open,
	.close		= _srmio_d2xx_close,
	.update		= _srmio_d2xx_update,
	.read		= _srmio_d2xx_read,
	.write		= _srmio_d2xx_write,
	.flush		= _srmio_d2xx_flush,
	.send_break	= _srmio_d2xx_send_break,
};

static srmio_io_t _srmio_d2xx_new( const char *identifier, DWORD flag )
{
	srmio_d2xx_t self;
	srmio_io_t h;

	assert( identifier );

	if( NULL == ( self = malloc( sizeof(struct _srmio_d2xx_t))))
		return NULL;

	memset( self, 0, sizeof(struct _srmio_d2xx_t ));
	self->id_flag = flag;

	if( NULL == (self->identifier = strdup( identifier ) ))
		goto clean1;

	if( NULL == ( h = srmio_io_new( &_d2xx_methods, (void*)self ) ))
		goto clean2;

	return h;
clean2:
	free( self->identifier );
clean1:
	free( self );
	return NULL;
}

srmio_io_t srmio_d2xx_serial_new( const char *serial )
{
	if( ! d2xx_lib )
		if( ! d2xx_dlopen() )
			return NULL;

	return _srmio_d2xx_new( serial, FT_OPEN_BY_SERIAL_NUMBER );
}

srmio_io_t srmio_d2xx_description_new( const char *desc )
{
	if( ! d2xx_lib )
		if( ! d2xx_dlopen() )
			return NULL;

	return _srmio_d2xx_new( desc, FT_OPEN_BY_DESCRIPTION );
}

/* TODO: provide some way to list devices */
