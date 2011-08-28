/*
 * Copyright (c) 2011 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include "serio.h"

#include <ltdl.h>

#ifdef HAVE_WINDOWS_H
# include <windows.h>
#endif

#ifdef HAVE_FTD2XX_H
# include <ftd2xx.h>
#endif



/************************************************************
 *
 * dynamic lib loading
 */

static lt_dlhandle d2xx_lib = NULL;
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
	if( ! (var = lt_dlsym(handle, name ) )){ \
		srmio_error_set(err, "d2xx: failed to load symbol %s: %s", \
			 name, lt_dlerror() ); \
		goto clean; \
	}

static bool d2xx_dlopen( srmio_error_t *err )
{
	fprintf( stderr, "lt_dlopen %s\n", D2XX_LIBNAME );

	if( 0 != lt_dlinit() ){
		srmio_error_set( err, "d2xx: lt_dlinit failed: %s",
			lt_dlerror() );
		return false;
	}

	if( NULL == (d2xx_lib = lt_dlopenext( D2XX_LIBNAME ) )){
		srmio_error_set( err, "d2xx: lt_dlopen(%s) failed: %s\n",
			D2XX_LIBNAME, lt_dlerror() );
		goto clean1;
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
	lt_dlclose( d2xx_lib );
	d2xx_lib = NULL;
clean1:
	lt_dlexit();
	return false;
}


static void d2xx_error( srmio_error_t *err, FT_STATUS status,
	const char *fmt, ... )
{
	char buf[SRMIO_ERROR_MSG_SIZE];
	va_list ap;

	va_start( ap, fmt );
	vsnprintf( buf, SRMIO_ERROR_MSG_SIZE, fmt, ap );
	va_end ( ap );

	switch( status ){
          case FT_OK:
		srmio_error_set( err, "%s: Success", buf );
		break;

          case FT_INVALID_HANDLE:
		srmio_error_set( err, "%s: invalid Handle", buf );
		break;

          case FT_INVALID_PARAMETER:
		srmio_error_set( err, "%s: invalid parameter", buf );
		break;

          case FT_INVALID_BAUD_RATE:
		srmio_error_set( err, "%s: invalid baud rate", buf );
		break;

          case FT_INVALID_ARGS:
		srmio_error_set( err, "%s: invalid arguments", buf );
		break;

          case FT_IO_ERROR:
		srmio_error_set( err, "%s: IO error", buf );
		break;

          case FT_NOT_SUPPORTED:
		srmio_error_set( err, "%s: not supported", buf );
		break;

          case FT_DEVICE_NOT_FOUND:
		srmio_error_set( err, "%s: device not found", buf );
		break;

          case FT_DEVICE_NOT_OPENED:
		srmio_error_set( err, "%s: device not opened", buf );
		break;

          case FT_DEVICE_NOT_OPENED_FOR_ERASE:
		srmio_error_set( err, "%s: device not open for erase", buf );
		break;

          case FT_DEVICE_NOT_OPENED_FOR_WRITE:
		srmio_error_set( err, "%s: device not open for write", buf );
		break;

          case FT_INSUFFICIENT_RESOURCES:
		srmio_error_set( err, "%s: insufficient resources", buf );
		break;

          case FT_FAILED_TO_WRITE_DEVICE:
          case FT_EEPROM_READ_FAILED:
          case FT_EEPROM_WRITE_FAILED:
          case FT_EEPROM_ERASE_FAILED:
          case FT_EEPROM_NOT_PRESENT:
          case FT_EEPROM_NOT_PROGRAMMED:
		srmio_error_set( err, "%s: EEprom error %d",
			buf, status );
		break;

          default:
		srmio_error_set( err, "%s: unspecified error: %d",
			buf, status );
		break;
	}
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


static int _srmio_d2xx_write( srmio_io_t h, const unsigned char *buf, size_t len, srmio_error_t *err )
{
	FT_STATUS ret;
	DWORD written;

	assert( h );
	assert( SELF(h)->is_open );
	assert( buf );

	ret = (*FP_Write)( SELF(h)->ft, (unsigned char *)buf, len, &written );
	if( ret == FT_OK )
		return written;

	d2xx_error( err, ret, "d2xx write" );
	return -1;
}

static int _srmio_d2xx_read( srmio_io_t h, unsigned char *buf, size_t len, srmio_error_t *err )
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
			d2xx_error( err, ret, "d2xx read" );
			return -1;
		}

		if( got < 1 ){
			return total;
		}

		total += got;
	}

	return total;
}


static bool _srmio_d2xx_flush( srmio_io_t h, srmio_error_t *err )
{
	FT_STATUS ret;

	assert( h );
	assert( SELF(h)->is_open );

	ret = (*FP_Purge)( SELF(h)->ft, FT_PURGE_RX | FT_PURGE_TX );
	if( ret != FT_OK ){
		d2xx_error( err, ret, "d2xx flush" );
		return false;
	}

	return true;
}

static bool _srmio_d2xx_send_break( srmio_io_t h, srmio_error_t *err )
{
#ifdef HAVE_NANOSLEEP
	struct timespec tspec = {
		.tv_sec = 0,
		.tv_nsec = 250000000,
	}; // 0.25 sec
#endif
	FT_STATUS ret;

	assert( h );
	assert( SELF(h)->is_open );

	ret = (*FP_SetBreakOn)( SELF(h)->ft );
	if( ret != FT_OK ){
		d2xx_error( err, ret, "d2xx break on" );
		return false;
	}

#ifdef HAVE_NANOSLEEP
	nanosleep( &tspec, NULL );
#elif HAVE_MSEC_SLEEP
	Sleep(250);
#else
#error no sufficient sleep() function available to send break
#endif

	ret = (*FP_SetBreakOff)( SELF(h)->ft );
	if( ret != FT_OK ){
		d2xx_error( err, ret, "d2xx break off" );
		return false;
	}

	return true;
}

static bool _srmio_d2xx_update( srmio_io_t h, srmio_error_t *err )
{
	FT_STATUS ret;
	unsigned rate;
	UCHAR parity;
	USHORT flow;

	if( ! srmio_io_baud2name( h->baudrate, &rate ) ){
		srmio_error_set( err, "d2xx: invalid baudrate: %u", h->baudrate );
		return false;
	}

	ret = (*FP_SetBaudRate)( SELF(h)->ft, rate );
	if( ret != FT_OK ){
		d2xx_error( err, ret, "d2xx baud" );
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
		srmio_error_set( err, "d2xx: invalid parity: %u", h->parity );
		return false;
	}
	ret = (*FP_SetDataCharacteristics)( SELF(h)->ft, FT_BITS_8,
		FT_STOP_BITS_1, parity );
	if( ret != FT_OK ){
		d2xx_error( err, ret, "d2xx parity" );
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
		srmio_error_set( err, "d2xx: invalid flow control: %u", h->flow );
		return false;
	}
	ret = (*FP_SetFlowControl)( SELF(h)->ft, flow, 0, 0 );
	if( ret != FT_OK ){
		d2xx_error( err, ret, "d2xx flow" );
		return false;
	}

	ret = (*FP_SetTimeouts)( SELF(h)->ft, 1000, 1000 );
	if( ret != FT_OK ){
		d2xx_error( err, ret, "d2xx timeout" );
		return false;
	}

	return true;
}


static bool _srmio_d2xx_close( srmio_io_t h, srmio_error_t *err )
{
	FT_STATUS ret;

	assert( h );

	if( ! SELF(h)->is_open )
		return true;

	SELF(h)->is_open = false;

	ret = (*FP_Close)( SELF(h)->ft );
	if( ret != FT_OK ){
		d2xx_error( err, ret, "d2xx close" );
		return false;
	}

	return true;
}

static bool _srmio_d2xx_open( srmio_io_t h, srmio_error_t *err )
{
	FT_STATUS ret;

	assert(h);
	assert(SELF(h)->identifier);

	ret = (*FP_OpenEx)( SELF(h)->identifier, SELF(h)->id_flag, &SELF(h)->ft );
	if( ret != FT_OK ){
		d2xx_error( err, ret, "d2xx open" );
		return false;
	}

	if( ! srmio_io_update( h, err ) )
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

static srmio_io_t _srmio_d2xx_new( const char *identifier, DWORD flag, srmio_error_t *err )
{
	srmio_d2xx_t self;
	srmio_io_t h;

	assert( identifier );

	if( NULL == ( self = malloc( sizeof(struct _srmio_d2xx_t)))){
		srmio_error_errno( err, "new d2xx" );
		return NULL;
	}

	memset( self, 0, sizeof(struct _srmio_d2xx_t ));
	self->id_flag = flag;

	if( NULL == (self->identifier = strdup( identifier ) )){
		srmio_error_errno( err, "new d2xx identifier" );
		goto clean1;
	}

	if( NULL == ( h = srmio_io_new( &_d2xx_methods, (void*)self, err ) ))
		goto clean2;

	return h;
clean2:
	free( self->identifier );
clean1:
	free( self );
	return NULL;
}

srmio_io_t srmio_d2xx_serial_new( const char *serial, srmio_error_t *err )
{
	if( ! d2xx_lib )
		if( ! d2xx_dlopen( err ) )
			return NULL;

	return _srmio_d2xx_new( serial, FT_OPEN_BY_SERIAL_NUMBER, err );
}

srmio_io_t srmio_d2xx_description_new( const char *desc, srmio_error_t *err )
{
	if( ! d2xx_lib )
		if( ! d2xx_dlopen( err ) )
			return NULL;

	return _srmio_d2xx_new( desc, FT_OPEN_BY_DESCRIPTION, err );
}

/* TODO: provide some way to list devices */
