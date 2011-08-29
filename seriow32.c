/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include "serio.h"

#ifdef HAVE_WINDOWS_H
# include <windows.h>
#endif


static const DWORD _srmio_iow32_baud[srmio_io_baud_max] = {
	CBR_2400,
	CBR_4800,
	CBR_9600,
	CBR_19200,
	CBR_38400,
};


/*
 * handle
 */
struct _srmio_iow32_t {
	char			*fname;
	HANDLE			fh;
};
typedef struct _srmio_iow32_t *srmio_iow32_t;

#define SELF(x)	((srmio_iow32_t)x->child)


static void iow32_error( srmio_error_t *err, const char *fmt, ... )
{
	char buf[SRMIO_ERROR_MSG_SIZE];
	wchar_t msg[SRMIO_ERROR_MSG_SIZE+1];
	va_list ap;
	DWORD code;
	char converted[SRMIO_ERROR_MSG_SIZE+1];

	if( ! err )
		return;

	code = GetLastError();

	va_start( ap, fmt );
	vsnprintf( buf, SRMIO_ERROR_MSG_SIZE, fmt, ap );
	va_end ( ap );

	if( ! FormatMessage( FORMAT_MESSAGE_FROM_SYSTEM
		| FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&msg, SRMIO_ERROR_MSG_SIZE * sizeof(wchar_t),
		NULL ) ){

		DPRINTF( "FormatMessage failed, using error code" );
		srmio_error_set( err, "%s: %d", code );
		return;
	}

	if( ! WideCharToMultiByte( CP_ACP, 0,
		msg, -1, converted, SRMIO_ERROR_MSG_SIZE,
		NULL, NULL ) ){

		DPRINTF( "String conversion failed, using error code" );
		srmio_error_set( err, "%s: %d", code );
		return;
	}

	srmio_error_set( err, "%s: %s", converted );
}

static int _srmio_iow32_write( srmio_io_t h, const unsigned char *buf,
	size_t len, srmio_error_t *err )
{
	DWORD cBytes = 0;

	assert( h );
	assert( SELF(h)->fh != INVALID_HANDLE_VALUE );
	assert( buf );

	if( ! WriteFile( SELF(h)->fh, buf, len, &cBytes, NULL ) ){
		iow32_error( err, "WriteFile" );
		return -1;
	}

	if( ! FlushFileBuffers( SELF(h)->fh ) ){
		iow32_error( err, "FlushFileBuffers" );
		return -1;
	}

	return cBytes;
}

static int _srmio_iow32_read( srmio_io_t h, unsigned char *buf, size_t len,
	srmio_error_t *err )
{
	DWORD cBytes = 0;

	assert( h );
	assert( SELF(h)->fh != INVALID_HANDLE_VALUE );
	assert( buf );

	if( ! ReadFile( SELF(h)->fh, buf, len, &cBytes, NULL ) ){
		iow32_error( err, "ReadFile" );
		return -1;
	}

	return cBytes;
}


static bool _srmio_iow32_flush( srmio_io_t h, srmio_error_t *err )
{
	assert( h );
	assert( SELF(h)->fh != INVALID_HANDLE_VALUE );

	if( ! PurgeComm( SELF(h)->fh, PURGE_RXABORT | PURGE_RXCLEAR
		| PURGE_TXABORT | PURGE_TXCLEAR ) ){

		iow32_error( err, "PurgeComm" );
		return false;
	}

	return true;
}

static bool _srmio_iow32_send_break( srmio_io_t h, srmio_error_t *err )
{
	assert( h );
	assert( SELF(h)->fh != INVALID_HANDLE_VALUE );

	if( ! SetCommBreak( SELF(h)->fh ) ){
		iow32_error(err, "SetCommBreak" );
		return false;
	}

#ifdef HAVE_MSEC_SLEEP
	Sleep(250);
#else
# error missing Sleep()
#endif

	if( ! ClearCommBreak( SELF(h)->fh ) ){
		iow32_error(err, "ClearCommBreak" );
		return false;
	}

	return true;
}

static bool _srmio_iow32_update( srmio_io_t h, srmio_error_t *err )
{
	COMMTIMEOUTS timeouts = {
		.ReadIntervalTimeout = 1000,
		.ReadTotalTimeoutConstant = 0,
		.ReadTotalTimeoutMultiplier = 1000,
		.WriteTotalTimeoutConstant = 0,
		.WriteTotalTimeoutMultiplier = 500,
	};
	DCB dcb;

	if( ! GetCommState( SELF(h)->fh, &dcb ) ){
		iow32_error( err, "GetCommState" );
		return false;
	}

	dcb.fBinary = TRUE;
	dcb.fAbortOnError = FALSE;

	dcb.ErrorChar = 0;
	dcb.EofChar = 0;
	dcb.EvtChar = 0;

	dcb.ByteSize = 8;
	dcb.StopBits = ONESTOPBIT;

	// TODO: XonChar, XoffChar, XonLim, XoffLim
	switch( h->flow ){
	  case srmio_io_flow_none:
		dcb.fOutX = FALSE;
		dcb.fInX = FALSE;

		dcb.fOutxCtsFlow = FALSE;
		dcb.fRtsControl = RTS_CONTROL_DISABLE;

		dcb.fOutxDsrFlow = FALSE;
		dcb.fDsrSensitivity = FALSE;
		dcb.fDtrControl = DTR_CONTROL_DISABLE;
		break;

	  case srmio_io_flow_xonoff:
		dcb.fOutX = TRUE;
		dcb.fInX = TRUE;

		dcb.fOutxCtsFlow = FALSE;
		dcb.fRtsControl = RTS_CONTROL_DISABLE;

		dcb.fOutxDsrFlow = FALSE;
		dcb.fDsrSensitivity = FALSE;
		dcb.fDtrControl = DTR_CONTROL_DISABLE;
		break;

	  case srmio_io_flow_rtscts:
		dcb.fOutX = FALSE;
		dcb.fInX = FALSE;

		dcb.fOutxCtsFlow = TRUE;
		dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;

		dcb.fOutxDsrFlow = FALSE;
		dcb.fDsrSensitivity = FALSE;
		dcb.fDtrControl = DTR_CONTROL_DISABLE;
		break;

	  default:
		srmio_error_set( err, "ios invalid flow control: %u", h->flow);
		return false;
	}

	switch( h->parity ){
	  case srmio_io_parity_none:
		dcb.Parity = NOPARITY;
		break;

	  case srmio_io_parity_even:
		dcb.Parity = EVENPARITY;
		break;

	  case srmio_io_parity_odd:
		dcb.Parity = ODDPARITY;
		break;

	  default:
		srmio_error_set( err, "ios invalid parity: %u", h->parity );
		return false;
	}

	assert( h->baudrate < srmio_io_baud_max );
	dcb.BaudRate = _srmio_iow32_baud[h->baudrate];

	if( ! SetCommState( SELF(h)->fh, &dcb )){
		iow32_error( err, "SetCommState" );
		return false;
	}

	if( ! SetCommTimeouts(SELF(h)->fh, &timeouts) ){
		iow32_error( err, "SetCommTimeouts" );
		return false;
	}

	return true;
}


static bool _srmio_iow32_close( srmio_io_t h, srmio_error_t *err )
{
	(void)err;

	assert( h );
	assert( SELF(h)->fh != INVALID_HANDLE_VALUE );

	CloseHandle( SELF(h)->fh );
	SELF(h)->fh = INVALID_HANDLE_VALUE;

	return true;
}

static bool _srmio_iow32_open( srmio_io_t h, srmio_error_t *err )
{
	char path[PATH_MAX];
	wchar_t wname[PATH_MAX];

	assert( h );
	assert( SELF(h)->fh == INVALID_HANDLE_VALUE );

	if( PATH_MAX <= snprintf( path, PATH_MAX, "\\\\.\\%s",
		SELF(h)->fname ) ){

		srmio_error_errno( err, "build filname failed" );
		return false;
	}

	if( ! MultiByteToWideChar( CP_ACP, 0,
		path, -1, wname, PATH_MAX ) ){

		srmio_error_set( err, "failed to convert filename" );
		return false;
	}

	SELF(h)->fh = CreateFile( wname,
		GENERIC_READ|GENERIC_WRITE,
		FILE_SHARE_DELETE|FILE_SHARE_WRITE|FILE_SHARE_READ,
		NULL, OPEN_EXISTING, 0, NULL);

	if( SELF(h)->fh == INVALID_HANDLE_VALUE){
		iow32_error( err, "CreateFile" );
		return false;
	}

	if( ! srmio_io_update( h, err ) )
		goto clean;

	return true;

clean:
	CloseHandle( SELF(h)->fh );
	SELF(h)->fh = INVALID_HANDLE_VALUE;
	return false;
}

static void _srmio_iow32_free( srmio_io_t h )
{
	free(SELF(h)->fname);
	free(SELF(h));
}

static const srmio_io_methods_t _iow32_methods = {
	.free		= _srmio_iow32_free,
	.open		= _srmio_iow32_open,
	.close		= _srmio_iow32_close,
	.update		= _srmio_iow32_update,
	.read		= _srmio_iow32_read,
	.write		= _srmio_iow32_write,
	.flush		= _srmio_iow32_flush,
	.send_break	= _srmio_iow32_send_break,
};

srmio_io_t srmio_iow32_new( const char *fname, srmio_error_t *err )
{
	srmio_iow32_t self;
	srmio_io_t h;

	assert( fname );
	if( NULL == ( self = malloc( sizeof(struct _srmio_iow32_t)))){
		srmio_error_errno( err, "ios new" );
		return NULL;
	}

	self->fh = INVALID_HANDLE_VALUE;

	if( NULL == ( self->fname = strdup( fname ) )){
		srmio_error_errno( err, "ios new fname" );
		goto clean1;
	}

	if( NULL == ( h = srmio_io_new( &_iow32_methods, (void*)self, err ) ))
		goto clean2;

	return h;
clean2:
	free( self->fname );
clean1:
	free( self );
	return NULL;
}

