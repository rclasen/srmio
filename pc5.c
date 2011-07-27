/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */


#include "pc.h"


#define STX	((char)0x2)
#define ETX	((char)0x3)

#define BLOCK_ACK	((const unsigned char *)"\x06")
#define BLOCK_NAK	((const unsigned char *)"\x15")
#define BLOCK_ABRT	((const unsigned char *)"\xaa")

#define PC5_BUFSIZE	1024

#define PC5_PKT_SIZE	64
#define PC5_PKT_CHUNKS	11u


/*
 * connection handle
 */
struct _srmio_pc5_t {
	/* connnection */
	int			stxetx;	/* use stx/etx headers + encoding? */
	time_t			nready;	/* PCV accepts next command */

	/* xfer */
	struct tm		pctime;
	struct _srmio_pc_xfer_block_t block;
	size_t			block_num;
	unsigned char		pkt_data[PC5_BUFSIZE];
	unsigned		pkt_cnt;

	/* current pkt */
	unsigned		pkt_num;	/* 0..pkts-1 */
	srmio_time_t		pkt_time;
	unsigned long		pkt_dist;
	int			pkt_temp;
	srmio_time_t		pkt_recint;

	/* current chunk */
	unsigned		chunk_num;	/* within pkt 0..10 */
};
typedef struct _srmio_pc5_t *srmio_pc5_t;

#define SELF(x) ((srmio_pc5_t)x->child)

/*
 * supported baudrates in order to try
 * default is 9600 8n1
 * srmwin tries:
 *  9600,4800,19200,2400 Baud
 *  each baudrate with none, then with even parity
 */
static const srmio_io_baudrate_t _srmio_pc5_baud_probe[] = {
	srmio_io_baud_9600,
	srmio_io_baud_19200,
	srmio_io_baud_4800,
	srmio_io_baud_2400,
	srmio_io_baud_max,
};

static const srmio_io_parity_t _srmio_pc5_parity_probe[] = {
	srmio_io_parity_none,
	srmio_io_parity_even,
	srmio_io_parity_max,
};

static int _srmio_pc5_msg_decode(
	unsigned char *out, size_t olen,
	const unsigned char *in, size_t ilen,
	srmio_error_t *err );

static bool _srmio_pc5_msg_send( srmio_pc_t conn,
	char cmd, const unsigned char *arg, size_t alen,
	srmio_error_t *err );

/************************************************************
 *
 * low level IO (open/read/write/close)
 *
 ************************************************************/

/*
 * send data towards SRM,
 *
 * returns true on success
 */
static bool _srmio_pc5_write( srmio_pc_t conn, const unsigned char *buf,
	size_t blen, srmio_error_t *err )
{
	int ret;

	DUMPHEX( "", buf, blen );

	ret = srmio_io_write( conn->io, buf, blen, err );
	if( ret < 0 )
		return false;

	if( (size_t)ret < blen ){
		srmio_error_set(err, "incomplete write: %d/%d",
			ret, blen);
		return false;
	}

	return true;
}

/*
 * read up to specified number of bytes
 *
 * returns number of chars read
 * returns -1 on error
 */
static int _srmio_pc5_read( srmio_pc_t conn, unsigned char *buf,
	size_t want, srmio_error_t *err )
{
	return srmio_io_read( conn->io, buf, want, err );
}


/*
 * set serial parameter
 * wake up PCV
 * send "hello"
 * check returned version
 * decode version
 *
 * returns true on success
 */
static bool _srmio_pc5_init(
	srmio_pc_t conn,
	const srmio_io_baudrate_t baudrate,
	const srmio_io_parity_t parity,
	srmio_error_t *err )
{
	int		ret;
	unsigned char	buf[20];
	unsigned char	verbuf[2];
	unsigned	baudname;
	char		parityname;

	assert( conn );
	assert( baudrate < srmio_io_baud_max );
	assert( parity < srmio_io_parity_max );

	SELF(conn)->stxetx = 1;

	srmio_io_baud2name( baudrate, &baudname );
	srmio_io_parity2name( parity, &parityname );

	STATMSG( "trying comm %d/8%c1",
		baudname,
		parityname );

	if( ! srmio_io_set_baudrate( conn->io, baudrate, err ) )
		return false;

	if( ! srmio_io_set_parity( conn->io, parity, err ) )
		return false;

	if( ! srmio_io_update( conn->io, err ))
		return false;

	if( ! srmio_io_send_break( conn->io, err ) )
		return false;

	if( ! srmio_io_flush( conn->io, err ) )
		return false;

	/* send opening 'P' to verify communication works */
	if( ! _srmio_pc5_msg_send( conn, 'P', NULL, 0, err ) )
		return false;

	ret = _srmio_pc5_read( conn, buf, 20, err );
	if( ret < 0 ){
		return false;
	}
	if( ret == 0 ){
		srmio_error_set( err, "got no opening response" );
		return false;
	}

	DUMPHEX( "got", buf, ret );

	/* autodetect communcitation type */
	if( *buf == STX ){
		if( ret != 7 || buf[1] != 'P' ){
			srmio_error_set( err, "opening response is garbled" );
			return false;
		}

		if( 0 >  _srmio_pc5_msg_decode( verbuf, 2, &buf[2], 4, err ) ){
			return false;
		}

	} else {
		if( ret != 3 ||  buf[0] != 'P' ){
			srmio_error_set( err, "opening response is garbled" );
			return false;
		}

		SELF(conn)->stxetx = 0;
		DPRINTF( "disabling stx/etx" );

		memcpy( verbuf, &buf[1], 2 );
	}

	conn->baudrate = baudrate;
	conn->parity = parity;
	conn->firmware = buf_get_buint16( verbuf, 0 );

	DPRINTF("found PCV version 0x%x at %d/8%c1",
		conn->firmware,
		baudname,
		parityname );

	return true;
}

static bool _srmio_pc5_probe_parity( srmio_pc_t conn,
	srmio_io_baudrate_t rate, srmio_error_t *err )
{
	srmio_error_t lerr;
	const srmio_io_parity_t *parity;
	unsigned baud;

	if( ! srmio_io_baud2name( rate, &baud ) ){
		srmio_error_set( err, "invalid baudrate" );
		return false;
	}

	for( parity = _srmio_pc5_parity_probe;
		*parity < srmio_io_parity_max; ++parity ){

		if( _srmio_pc5_init( conn, rate, *parity, &lerr ) )
			return true;

		STATMSG( "probe %u baud, parityID %u failed: %s",
			baud, *parity, lerr.message );
	}

	srmio_error_set( err, "no PCV found at %u baud", baud );
	return false;
}

static bool _srmio_pc5_probe_baud( srmio_pc_t conn,
	srmio_error_t *err )
{
	srmio_error_t lerr;
	const srmio_io_baudrate_t *rate;

	for( rate = _srmio_pc5_baud_probe; *rate < srmio_io_baud_max; ++rate ){
		if( conn->parity == srmio_io_parity_max ){
			if( _srmio_pc5_probe_parity( conn, *rate, &lerr ) )
				return true;

			STATMSG( "probe baudID %u failed: %s",
				*rate, lerr.message );

		} else {
			if( _srmio_pc5_init( conn, *rate, conn->parity, &lerr ) )
				return true;

			SRMIO_PC_STATUS( conn, "probe baudID %u, parityID %u failed: %s",
				*rate, conn->parity, lerr.message );
		}
	}

	srmio_error_set( err, "no PCV found at any baudrate" );
	return false;
}

/*
 * serial handle must be opened before this
 * set serial device properties
 * get version from initial greeting
 *
 */
static bool _srmio_pc5_open( srmio_pc_t conn, srmio_error_t *err )
{
	assert( conn );
	assert( conn->io );

	DPRINTF( "" );

	if( !  srmio_io_set_flow( conn->io, srmio_io_flow_none, err ) )
		return false;

	/* set serial parameter and get PCV version */
	if( conn->baudrate == srmio_io_baud_max ){
		if( ! _srmio_pc5_probe_baud( conn, err ) )
			return false;

	} else if( conn->parity == srmio_io_parity_max ){
		if( ! _srmio_pc5_probe_parity( conn, conn->baudrate, err ) )
			return false;

	} else if( ! _srmio_pc5_init( conn, conn->baudrate, conn->parity, err)) {
		return false;

	}

	return true;
}

/*
 * close serial device
 *
 * parameter:
 *  conn: connection handle
 *
 * returns true on success
 */
bool _srmio_pc5_close( srmio_pc_t conn, srmio_error_t *err )
{
	assert( conn );

	(void) conn;
	(void) err;

	return true;
}



/************************************************************
 *
 * send/receive "messages" + helper
 *
 ************************************************************/


/*
 * Some SRM Firmware versions seem to use a protcoll heavyliy inspired by
 * ASCII control sequences - namely STX, ETX, ACK and NAK. Furthermore
 * most data is encoded "ASCII-friendly" - actually I was quite puzzled to
 * read timestamps as-is within the *hexdump*.
 *
 * Other Firmware versions use the same "commands", but omit the STX/ETX and
 * don't encode the data. The data is transmitted as-is. In this case you either
 * have to know how much data to read from the SRM or read until it times
 * out.
 *
 * In general communication has to be initiated by the computer by sending
 * some command:
 *
 *  - computer sends STX <command_char> <optional_data> ETX - SRM responds
 *  with STX <command_char> <optional_data> ETX
 *
 * command_char is a single char. The same command_char is used to - get
 * the SRM's individual settings or - adjust the SRM's settings.
 *
 * When optional data is encoded the "ASCII-friendly" way,
 *  - each byte's actual value is converted to a hex string,
 *  - each hex-digit is used as a seperate byte + 0x30.
 * eg. 0x3f is encoded as 0x33,0x3f
 *
 * To get a current setting, the computer sends 0x00 as optional_data. If
 * necessary this is encoded "ASCII-friendly".
 *
 * To change a setting, the computer appends the new setting to the
 * command as optional_data. If necessary, it's encoded ASCII-friendly.
 * The SRM responds without any optional_data to confirm the operation.
 *
 * The response to commands to get the actual ride data off the SRM is
 * always transmitted "unencoded. (Though the whole response might be
 * wrapped in STX/ETX). Furthermore the SRM waits for confirmation
 * (ACK/NAK) periodically. The download may be aborted by sending 0xAA,
 * instead.
 *
 * FYI: I've tried to keep this as bitsex- - ehm - endianess-independent
 * as possible. Therefore I've decided against read()ing a struct and
 * parse the bytes manually... Please send patches if you can do this more
 * elegant!
 *
 */


/*
 * encode data "ASCII-friendly"
 *
 * returns number of encoded bytes
 * returns -1 on error
 */
static int _srmio_pc5_msg_encode(
	unsigned char *out, size_t olen,
	const unsigned char *in, size_t ilen )
{
	size_t i;
	size_t o=0;

	assert( ! ilen || in );
	assert( out );
	assert( ! ilen || 2 * ilen > ilen ); /* overflow* */
	assert( olen >= 2 * ilen ); /* output buffer size */

	for( i=0; i < ilen; ++i ){
		out[o++] = ((in[i] & 0xf0 ) >> 4) | 0x30;
		out[o++] = (in[i] & 0x0f ) | 0x30;
	}

	DUMPHEX( "", out, o );
	return o;
}


/*
 * decode "ASCII-friendly" encoded data
 *
 * returns number of decoded bytes
 * returns -1 on error
 */
static int _srmio_pc5_msg_decode(
	unsigned char *out, size_t olen,
	const unsigned char *in, size_t ilen,
	srmio_error_t *err )
{
	unsigned i;
	unsigned o=0;

	assert( ! ilen || in );
	assert( out );
	assert( ilen % 2 == 0 ); /* input must be multiple of 2 */
	assert( olen * 2 >= ilen ); /* output buffer size */

	for( i=0; i < ilen ; i += 2 ){
		if( (in[i] & 0xf0) != 0x30
			|| (in[i+1] & 0xf0) != 0x30 ){

			srmio_error_set(err, "invalid input at %u", i );
			return -1;
		}
		out[o] = ((in[i] & 0x0f) << 4)
			| (in[i+1] & 0x0f);
		++o;
	}

	DUMPHEX( "", out, o );
	return o;
}


/*
 * send command to SRM,
 *
 * takes care of stx/etx/encode when neccessary.
 *
 * returns decoded response length on success
 * returns true on success
 */
static bool _srmio_pc5_msg_send( srmio_pc_t conn, char cmd,
	const unsigned char *arg, size_t alen,
	srmio_error_t *err )
{
	unsigned char buf[PC5_BUFSIZE];
	unsigned len = 0;

	DUMPHEX( "cmd=%c", arg, alen, cmd );

	assert( conn );
	assert( ! alen || arg );
	assert( 2 * alen +3 >= alen ); /* owerflow? */
	assert( 2 * alen +3 <= PC5_BUFSIZE ); /* request < buffer */

	/* build command */
	if( SELF(conn)->stxetx ){
		buf[len++] = STX;
		buf[len++] = cmd;

		len += _srmio_pc5_msg_encode( &buf[len], PC5_BUFSIZE-2, arg, alen );
		buf[len++] = ETX;

	} else {
		buf[len++] = cmd;
		if( alen > 0 ){
			memcpy(&buf[len], arg, alen );
			len += alen;
		}
	}


	/* send */
	if( ! srmio_io_flush( conn->io, err ) )
		return false;

	if( ! _srmio_pc5_write( conn, buf, len, err ) )
		return false;

	return true;
}

/*
 * read response from SRM up to wanted length
 *
 * takes care of stx/etx/decode internaly when neccessary.
 *
 * response (without command char) is returned in rbuf (when rbuf != NULL)
 *
 * returns decoded response length on success
 * returns -1 on error
 */
static int _srmio_pc5_msg_recv( srmio_pc_t conn,
	unsigned char *rbuf, size_t rsize, size_t want,
	srmio_error_t *err )
{
	unsigned char	buf[PC5_BUFSIZE];
	size_t	rlen = 0;
	int	ret;

	assert( conn );
	assert( ! rsize || rbuf ); /* opt. rbuf */
	assert( ! rbuf || want <= rsize ); /* opt. buf size */
	assert( 2 * want + 3 > want ); /* overflow */
	assert( 2*want +3 <= PC5_BUFSIZE );

	if( SELF(conn)->stxetx )
		/* stx, etc, encoding */
		want = 2*want +2;

	/* cmd char */
	++want;

	DPRINTF( "will read %lu ", (unsigned long)want );


	/* read till
	 * - rsize is read
	 * - ETX is found (when stxetx is used)
	 * - read times out
	 */
	while( rlen < want ){
		ret = _srmio_pc5_read( conn, &buf[rlen], 1, err );
		if( ret < 0 ){
			DUMPHEX( "", buf, rlen );
			return -1;
		}

		/* timeout */
		if( ret < 1 ){
			srmio_error_set( err, "read timed out" );
			DUMPHEX( "", buf, rlen );
			return -1;
		}

		++rlen;

		/* avoid running into timeout */
		if( SELF(conn)->stxetx && buf[rlen-1] == ETX ){
			DPRINTF( "msg complete - got ETX" );
			break;
		}

	}
	DUMPHEX( "", buf, rlen );

	if( rlen < want ){
		srmio_error_set( err, "got incomplete response: %u/%u",
			rlen, want );
		return -1;
	}

	if( SELF(conn)->stxetx ){
		if( rlen < 3 ){
			srmio_error_set( err, "response is too short for stxetx: %u",
				rlen );
			return -1;
		}

		if( buf[0] != STX ){
			srmio_error_set( err, "response is missing STX" );
			return -1;
		}

		if( buf[rlen-1] != ETX ){
			srmio_error_set( err, "response is missing ETX" );
			return -1;
		}
		rlen -= 3; /* stx, cmd, etx */

		if( rbuf ){
			ret = _srmio_pc5_msg_decode(rbuf, rsize, &buf[2], rlen, err );
			if( ret < 0 )
				return -1;
			rlen = ret;
		} else
			rlen /= 2;

	} else {
		if( rlen < 1 ){
			srmio_error_set( err, "response is too short: %u",
				rlen );
			return -1;
		}

		--rlen; /* cmd */

		if( rbuf )
			memcpy(rbuf, &buf[1], rlen);
	}

	return rlen;
}

/*
 * Some commands keep the PCV busy after it responded. This
 * sets the time to wait until the PCV accepts new commands.
 *
 * parameters:
 *  conn: connection handle
 *  delay: seconds the PCV will be busy
 *
 * return true on success
 */
static bool _srmio_pc5_msg_busy( srmio_pc_t conn, unsigned delay )
{
	time( &SELF(conn)->nready );
	SELF(conn)->nready += delay;

	return true;
}

/*
 * Wait for PCV to finish last command and become ready to
 * accept the next one (if necessary)
 *
 */
static void _srmio_pc5_msg_ready( srmio_pc_t conn )
{
	time_t now;
	unsigned delta;

	time( &now );
	if( SELF(conn)->nready <= now )
		return;

	delta = SELF(conn)->nready - now;

	STATMSG( "PCV still busy for %usec, waiting...", delta );
	sleep( delta );

	return;
}

/*
 * process one complete command (send, receive),
 * retry if necesssary
 *
 * returns decoded response length on success
 * returns -1 on error
 */
static int _srmio_pc5_msg( srmio_pc_t conn, char cmd,
	const unsigned char *arg,
	size_t alen,
	unsigned char *buf,
	size_t blen,
	size_t want,
	srmio_error_t *err )
{
	int retries;
	int ret;

	assert( conn );

	if( conn->xfer_state != srmio_pc_xfer_type_new ){
		srmio_error_set( err, "another command is still running" );
		return -1;
	}

	_srmio_pc5_msg_ready( conn );

	for( retries = 0; retries < 3; ++retries ){

		if( retries ){
			STATMSG( "SRM isn't responding, send break and retry" );
			srmio_io_send_break( conn->io, err );
			sleep(1);
			srmio_io_flush( conn->io, err );
		}

		if( ! _srmio_pc5_msg_send( conn, cmd, arg, alen, err ) )
			return -1;

		ret = _srmio_pc5_msg_recv( conn, buf, blen, want, err );
		if( ret >= 0 )
			break;

	}

	return ret;
}



/************************************************************
 *
 * set/get individual SRM parameter
 *
 ************************************************************/

#define TIMEDEC(x)	\
	( ( ((x) & 0xf0) >> 4) * 10 + ((x) & 0x0f) )
#define TIMEENC(x)	\
	( ((unsigned char)((x) / 10) << 4 ) | ( (x) % 10 ) )

/*
 * setting:	get Firmware Version, readonly
 * command:	P
 */

/*
 * parameter:
 *  conn: connection handle
 *
 * gets integer firmware version
 * returns true on success
 */
bool srmio_pc5_cmd_get_version( srmio_pc_t conn, unsigned *version,
	srmio_error_t *err )
{
	unsigned char buf[20];
	int ret;

	assert( conn );
	assert( conn->is_open );
	assert( version );

	ret = _srmio_pc5_msg( conn, 'P', NULL, 0, buf, 20, 2, err );
	if( ret < 0 )
		return false;
	if( ret < 2 ){
		srmio_error_set( err, "got truncated version response" );
		return false;
	}

	*version = buf_get_buint16( buf, 0 );
	return true;
}

/*
 * setting:	athlete name
 * command:	N
 * argument:	cccccc\x20\x20 - it's 6 chars, zero-padded
 */

/*
 * parameter:
 *  conn: connection handle
 *
 * gets malloc()ed string on succuess
 * returns true on success
 */
static bool _srmio_pc5_cmd_get_athlete( srmio_pc_t conn, char **name,
	srmio_error_t *err )
{
	unsigned char buf[20];
	int ret;

	assert( conn );
	assert( conn->is_open );
	assert( name );

	ret = _srmio_pc5_msg( conn, 'N', (unsigned char*)"\x0", 1,
		buf, 20, 8, err );
	if( ret < 0 )
		return false;
	if( ret < 6 ){
		srmio_error_set( err, "got truncated athlete response" );
		return false;
	}

	*name = buf_get_string( buf, 0, 6 );
	DPRINTF( "athlete=%s", (char*)*name );
	/* TODO: iconv cp850 -> internal ?? */

	return true;
}

/*
 * setting:	current time
 * command:	M
 * argument:	0xdd 0xmm 0xyy 0xHH 0xMM 0xSS
 */

/*
 * get PCV's current date + time
 *
 * parameter:
 *  conn: connection handle
 *  timep: upated with PCV time (see "man mktime" for struct tm)
 *
 * returns true on success
 */
bool srmio_pc5_cmd_get_time( srmio_pc_t conn, struct tm *timep,
	srmio_error_t *err )
{
	unsigned char buf[20];
	int ret;

	assert( conn );
	assert( conn->is_open );
	assert( timep );

	ret = _srmio_pc5_msg( conn, 'M', (unsigned char*)"\x0", 1,
		buf, 20, 6, err );
	if( ret < 0 )
		return false;
	if( ret < 6 ){
		srmio_error_set( err, "got truncated time response" );
		return false;
	}

	timep->tm_mday = TIMEDEC( buf[0] );
	timep->tm_mon = TIMEDEC( buf[1] ) -1;
	timep->tm_year = TIMEDEC( buf[2] ) + 100;
	timep->tm_hour = TIMEDEC( buf[3] );
	timep->tm_min = TIMEDEC( buf[4] );
	timep->tm_sec = TIMEDEC( buf[5] );
	timep->tm_isdst = -1;

	DPRINTF( "time=%s", asctime( timep ) );

	return true;
}

/*
 * set PCV clock to specified date + time
 *
 * parameter:
 *  conn: connection handle
 *  timep: time to set (see "man mktime" for struct tm)
 *
 * returns true on success
 */
static bool _srmio_pc5_cmd_set_time( srmio_pc_t conn, struct tm *timep,
	srmio_error_t *err )
{
	unsigned char buf[6];

	assert( conn );
	assert( conn->is_open );
	assert( timep );

	/* TODO: check for overflows */
	buf[0] = TIMEENC( timep->tm_mday );
	buf[1] = TIMEENC( timep->tm_mon +1 );
	buf[2] = TIMEENC( timep->tm_year -100 );
	buf[3] = TIMEENC( timep->tm_hour );
	buf[4] = TIMEENC( timep->tm_min );
	buf[5] = TIMEENC( timep->tm_sec );

	if( 0 > _srmio_pc5_msg( conn, 'M', buf, 6, NULL, 0, 0, err ))
		return false;

	DPRINTF( "time=%s", asctime(timep) );
	_srmio_pc5_msg_busy( conn, 2 );
	return true;
}

/*
 * setting:	wheel circumference
 * command:	G
 * argument:	0x0000 - uint16
 */

/*
 * get wheel circumference
 *
 * parameter:
 *  conn: connection handle
 *
 * gets circumference
 * returns true on success
 */
bool srmio_pc5_cmd_get_circum( srmio_pc_t conn, unsigned *circum,
	srmio_error_t *err )
{
	unsigned char buf[20];
	int ret;

	assert( conn );
	assert( conn->is_open );
	assert( circum );

	ret = _srmio_pc5_msg( conn, 'G', (unsigned char*)"\x0\x0", 2,
		buf, 20, 2, err );
	if( ret < 0 )
		return false;
	if( ret < 2 ){
		srmio_error_set( err, "got truncated circumference response" );
		return false;
	}

	*circum = buf_get_buint16( buf, 0 );
	DPRINTF( "circum=%d", *circum );
	return true;
}

/*
 * setting:	slope
 * command:	E
 * argument:	0x0000 - uint16
 */

/*
 * get slope
 *
 * parameter:
 *  conn: connection handle
 *
 * gets slope  eg. 17.4
 * returns true on success
 */
bool srmio_pc5_cmd_get_slope( srmio_pc_t conn, double *slope,
	srmio_error_t *err )
{
	unsigned char buf[20];
	int ret;

	assert( conn );
	assert( conn->is_open );
	assert( slope );

	ret = _srmio_pc5_msg( conn, 'E', (unsigned char*)"\x0\x0", 2, buf,
		20, 2, err );
	if( ret < 0 )
		return false;
	if( ret < 2 ){
		srmio_error_set( err, "got truncated slope response" );
		return false;
	}

	*slope = (double)( buf_get_buint16( buf, 0 ) ) / 10;
	DPRINTF( "slope=%.1f", *slope );
	return true;
}


/*
 * setting:	zero offset
 * command:	F
 * argument:	0x0000 - uint16
 */

/*
 * get zero offset
 *
 * parameter:
 *  conn: connection handle
 *
 * returns true on success
 */
bool srmio_pc5_cmd_get_zeropos( srmio_pc_t conn,
	unsigned *zeropos, srmio_error_t *err )
{
	unsigned char buf[20];
	int ret;

	assert( conn );
	assert( conn->is_open );
	assert( zeropos );

	ret = _srmio_pc5_msg( conn, 'F', (unsigned char*)"\x0\x0", 2,
		buf, 20, 2, err );
	if( ret < 0 )
		return false;
	if( ret < 2 ){
		srmio_error_set( err, "got truncated offset response" );
		return false;
	}

	*zeropos = buf_get_buint16( buf, 0 );
	DPRINTF( "zeropos=%d", *zeropos );
	return true;
}


/*
 * setting:	recording interval
 * command:	R
 * argument:	unsigned char, ASCII-friendly
 *
 * if bit7 == 0
 *	bits 0-3 have the actual recint in seconds: 1-15
 * else
 *	bits 0-3 have the digit behind the decimal point: 0.1 - 0.9
 */

/*
 * get recording interval
 *
 * parameter:
 *  conn: connection handle
 *
 * gets recint * 10 -> 1sec becomes 10
 * returns true on success
 */
bool srmio_pc5_cmd_get_recint( srmio_pc_t conn,
	srmio_time_t *recint, srmio_error_t *err )
{
	unsigned char buf[10];
	int ret;

	assert( conn );
	assert( conn->is_open );
	assert( recint );

	ret = _srmio_pc5_msg( conn, 'R', (unsigned char*)"\x0", 1,
		buf, 10, 1, err );
	if( ret < 0 )
		return false;
	if( ret < 1 ){
		srmio_error_set( err, "got truncated recint response" );
		return false;
	}

	*recint = ( *buf & 0x80 )
		? *buf & 0x0f
		: *buf * 10;

	DPRINTF( "raw=0x%02x recint=%.1f", *buf,
		(double)*recint/10 );
	return true;
}

/*
 * set recording interval
 *
 * parameter:
 *  conn: connection handle
 *  recint: new interval (see srmio_pc5_cmd_get_recint for details)
 *
 * returns true on success
 */
static bool _srmio_pc5_cmd_set_recint( srmio_pc_t conn,
	srmio_time_t recint, srmio_error_t *err )
{
	unsigned char raw;

	if( recint <= 0 ){
		srmio_error_set( err, "recint < 0sec isn't supported" );
		return false;
	}

	/* 0.5 .. 0.9 sec */
	if( recint >= 5 && recint < 10 ){
		raw = 0x80 | recint;

	/* 1 .. 30 sec */
	} else if( recint <= 300 && recint % 10 == 0 ){
		raw = recint / 10;

	} else {
		srmio_error_set( err, "unsupported recint: %.1lf",
			.1 * recint );
		return false;

	}

	if( 0 > _srmio_pc5_msg( conn, 'R', &raw, 1, NULL, 0, 1, err ) )
		return false;

	DPRINTF( "success" );
	return true;
}


/*
 * setting:	clear recorded data
 * command:	B and T
 * argument:	none
 */

/*
 * mark data on PCV as deleted
 *
 * parameter:
 *  conn: connection handle
 *
 * returns true on success
 */
static bool _srmio_pc5_cmd_clear( srmio_pc_t conn, srmio_error_t *err )
{
	/* TODO: what's the individual effect of B and T?
	 *
	 * T seems to do the actual clean.
	 */

	if( 0 >  _srmio_pc5_msg( conn, 'B', NULL, 0, NULL, 0, 0, err ) )
		return false;

	if( 0 >  _srmio_pc5_msg( conn, 'T', NULL, 0, NULL, 0, 0, err ) )
		return false;

	DPRINTF( "success" );
	return true;
}


/* TODO: srmio_pc5_reset_battery
non-stxetx firmware:
0.00000 WRITE 59 02 3a 00
0.00000  READ 59
0.00000 WRITE 59 02 3b 00
0.00000  READ 59
 */

/* TODO: implement srmio_pc5_cmd_set_* functions */
/* TODO: investigate/implement other commands */






/************************************************************
 *
 * get/parse data pkts
 *
 ************************************************************/

/*
 * communication to download the data chunks looks like this:
 *  computer: <STX>A<ETX>
 *  SRM:      <STX>A<num_pkts>
 *  SRM:      <64byte_block>
 *  computer: <ACK>/<NAK>
 *    ... repeat last 2 steps for each pkt
 *  SRM:      <ETX>
 *
 * As with all commands it depends on the FW, if STX/ETX must be
 * omitted/present.
 *
 * The SRM sends pure binary data between the 'A' and the final ETX.
 *
 * num_pkts is encoded as 16bit big-endian integer.
 *
 * The 64 byte blocks comes with a 9 byte "header" followed by 11 data
 * chunks.
 *
 * pkt header bytes:
 *  - byte 0-4: timestamp of first chunk + recint,
 *  - byte 0:
 *    - bit 0-3: day decimal digit 0
 *    - bit 4-5: day decimal digit 1
 *    - bit 6: recint bits 3
 *    - bit 7: unknown TODO
 *  - byte 1:
 *    - bit 0-3: month decimal digit 0
 *    - bit 4: month decimal digit 1
 *    - bit 5-7: recint bits 0-2
 *  - byte 2:
 *    - bit 0-3: hour decimal digit 0
 *    - bit 4-5: hour decimal digit 1
 *    - bit 6: recint 0.1 - 0.7 flag
 *    - bit 7: unknown TODO
 *  - byte 3:
 *    - bit 0-3: minute decimal digit 0
 *    - bit 4-7: minute decimal digit 1
 *  - 5-7: total distance in meter * 3.9 - 24bit big-endian integer
 *  - 8: temperature - (signed?) integer - TODO
 *
 * data chunks (5 byte each):
 *  - 0: multi-purpose:
 *	 - bit0-3: power msb
 *	 - bit4-5: speed msb
 *       - bit6: new marker
 *       - bit7: continue last marker
 *  - 1: power (lsb) - integer
 *  - 2: speed (lsb) * 5 - integer
 *  - 3: cadence - integer
 *  - 4: heartrate - integer
 *
 * Looking at the timestamps in my sample recordings I can find plenty
 * 1sec gaps. While srmwin gets the same data from the SRM, it fills gaps
 * up to 2sec automatically with averaged data.
 *
 * I've found some other occasional differences to the data srmwin saves
 * to disk, but couldn't identify what's causing them.
 *
 * Alternatively all (incl. "deleted") chunks may be downloaded with
 * 'y' instead of 'A'.
 *
 * It seems, the PCV goes to some kind of sleep mode when downloads take
 * longer than 30sec (PCV changes display to show only the time).
 *
 * While this might be coincidence, PCV doesn't answer commands after
 * longer downloads, too. Srmwin tries to wake up the PCV by sending it a
 * BREAK signal
 */


/*
 * retrieve next pkt from PCV into buffer
 * parse pkt-wide data
 * and request next pkt
 *
 * returns false if there's no further pkt to retrieve
 * or if there was an error.
 */
static bool srmio_pc5_xfer_pkt_next( srmio_pc_t conn )
{
	int retries;
	struct tm btm;
	int ret;
	time_t bstart;
	int valid;
	int flush = 0;

	assert( conn );

	DPRINTF( "getting pkt %u/%u",
		SELF(conn)->pkt_num +1,
		SELF(conn)->pkt_cnt);

	for( retries = 0; retries < 4; ++retries ){
		valid = 0;

		if( retries ){
			/* request retransmit */
			STATMSG( "received bad pkt %u, "
				"requesting retransmit",
				SELF(conn)->pkt_num );

			/*
			 * PCV / prolific is a bitch:
			 *
			 * sometimes, read times out and there's no
			 * further data arriving. so io_flush(), sleeping
			 * or another read make no difference.
			 *
			 * though, the old data *does* arrive when writing
			 * to the PCV, again. As a result, all further
			 * data is off.
			 */
			if( flush > 0 ){
				if( ! srmio_io_flush( conn->io, &conn->err ) ){
					conn->xfer_state = srmio_pc_xfer_state_failed;
					return false;
				}

				if( ! _srmio_pc5_write( conn, BLOCK_NAK,
					1, &conn->err ) ){

					conn->xfer_state = srmio_pc_xfer_state_failed;
					return false;
				}

				flush += PC5_PKT_SIZE;
				ret = _srmio_pc5_read( conn,
					SELF(conn)->pkt_data, flush, &conn->err);

				DPRINTF("flushed %d bytes", ret );
				if( ret < 0 ){
					return false;
				}

				/* retransmit was incomplete, as well: */
				if( ret <= PC5_PKT_SIZE ){
					flush = PC5_PKT_SIZE - ret;
					continue;
				}

				/* we got more than one pkt ... hopefully
				 * that's everything... */
				flush = 0;
			}

			if( ! _srmio_pc5_write( conn, BLOCK_NAK, 1, &conn->err ) ){

				conn->xfer_state = srmio_pc_xfer_state_failed;
				return false;
			}

		}

		ret = _srmio_pc5_read( conn, SELF(conn)->pkt_data,
			PC5_PKT_SIZE, &conn->err  );
		DUMPHEX( "", SELF(conn)->pkt_data, ret );

		if( ret < 0 ){
			conn->xfer_state = srmio_pc_xfer_state_failed;
			/* non-recoverable error */
			return false;

		} else if( ret == 0 && SELF(conn)->stxetx
			&& SELF(conn)->pkt_cnt == 3
			&& SELF(conn)->pkt_num == 0 ){

			/* workaround stxetx + 3 pkts problem */
			DPRINTF( "stxetx, fixing pkts=0" );
			SELF(conn)->pkt_cnt = 0;
			conn->xfer_state = srmio_pc_xfer_state_success;
			return false;

		} else if( ret == 1 && SELF(conn)->stxetx
			&& SELF(conn)->pkt_data[0] == ETX ){

			srmio_error_set( &conn->err, "got unexpected end of "
				"transfer for pkt %u", SELF(conn)->pkt_num );
			conn->xfer_state = srmio_pc_xfer_state_failed;
			return false;

		} else if( ret < PC5_PKT_SIZE ){
			/* incomplete pkt, retry */
			STATMSG( "pkt %d is too short: %d",
				SELF(conn)->pkt_num,
				ret );

			flush += PC5_PKT_SIZE - ret;
			continue;

		}

		/* parse pkt */

		btm.tm_year = SELF(conn)->pctime.tm_year;
		btm.tm_isdst = -1;
		btm.tm_mday = TIMEDEC( SELF(conn)->pkt_data[0] & 0x3f );
		btm.tm_mon = TIMEDEC( SELF(conn)->pkt_data[1] & 0x1f ) -1;
		btm.tm_hour = TIMEDEC( SELF(conn)->pkt_data[2] & 0x3f );
		btm.tm_min = TIMEDEC( SELF(conn)->pkt_data[3] );
		btm.tm_sec = TIMEDEC( SELF(conn)->pkt_data[4] );

		if( btm.tm_mon < SELF(conn)->pctime.tm_mon )
			-- btm.tm_year;

		bstart = mktime( &btm );
		if( (time_t) -1 == bstart ){
			conn->xfer_state = srmio_pc_xfer_state_failed;
			srmio_error_set( &conn->err, "mktime %s failed: %s",
				asctime( &btm ),
				strerror(errno) );
			return false;
		}

		SELF(conn)->pkt_time = (srmio_time_t)bstart *10;
		SELF(conn)->pkt_dist = (double)buf_get_buint24( SELF(conn)->pkt_data, 5 ) / 3.9;

		SELF(conn)->pkt_temp = buf_get_int8( SELF(conn)->pkt_data, 8 );
		SELF(conn)->pkt_recint = ( (SELF(conn)->pkt_data[1] & 0xe0) >> 5)
			| ( (SELF(conn)->pkt_data[0] & 0x40) >> 3);

		if( ! (SELF(conn)->pkt_data[2] & 0x40) )
			SELF(conn)->pkt_recint *= 10;


		if( ! SELF(conn)->pkt_recint ){
			STATMSG( "pkt #%u has no recint", SELF(conn)->pkt_num );
			continue;
		}

		valid = 1;
		break;
	}

	if( ! valid ){
		conn->xfer_state = srmio_pc_xfer_state_failed;
		srmio_error_set( &conn->err, "got invalid pkt #%u, retries failed",
			SELF(conn)->pkt_num );
		return false;
	}


	/* confirm receival of pkt */

	if( ! _srmio_pc5_write( conn, BLOCK_ACK, 1, &conn->err ) ){
		conn->xfer_state = srmio_pc_xfer_state_failed;
		return false;
	}

	SELF(conn)->chunk_num = 0;
	SELF(conn)->pkt_num++;

	DPRINTF( "mon=%u day=%u hour=%u min=%u sec=%u "
		"dist=%lu temp=%d recint=%.1f na0=%x na2=%x",
		(unsigned)btm.tm_mon,
		(unsigned)btm.tm_mday,
		(unsigned)btm.tm_hour,
		(unsigned)btm.tm_min,
		(unsigned)btm.tm_sec,
		SELF(conn)->pkt_dist,
		SELF(conn)->pkt_temp,
		(double)SELF(conn)->pkt_recint/10,
		(int)( ( SELF(conn)->pkt_data[0] & 0x80) >> 7 ),
		(int)( ( SELF(conn)->pkt_data[2] & 0x80) >> 7 ) );

	return true;
}


/*
 * actually start the data transfer off the PCV
 *
 * parameter:
 *  conn: transfer handle
 *
 * returns true on success
 */
static bool _srmio_pc5_xfer_start( srmio_pc_t conn, srmio_error_t *err )
{
	int ret;
	char cmd;

	assert( conn );

	DPRINTF("getting meta info");
	if( conn->xfer_state != srmio_pc_xfer_state_new ){
		srmio_error_set( err, "device is busy" );
		return false;
	}

	switch( conn->xfer_type ){
	  case srmio_pc_xfer_type_new:
		cmd = 'A';
		break;

	  case srmio_pc_xfer_type_all:
		cmd = 'y';
		break;

	  default:
		srmio_error_set( err, "invalid xfer mode" );
		return false;
	}

	SELF(conn)->block.athlete = NULL;
	if( ! srmio_pc5_cmd_get_time( conn, &SELF(conn)->pctime, err ))
		return false;

	if( ! srmio_pc5_cmd_get_slope( conn, &SELF(conn)->block.slope, err ) )
		return false;

	if( ! srmio_pc5_cmd_get_zeropos( conn, &SELF(conn)->block.zeropos, err) )
		return false;

	if( ! srmio_pc5_cmd_get_circum( conn, &SELF(conn)->block.circum, err ) )
		return false;

	if( ! srmio_pc5_cmd_get_recint( conn, &SELF(conn)->block.recint, err  ) )
		return false;

	if( ! srmio_pc_cmd_get_athlete( conn, &SELF(conn)->block.athlete, err ) )
		return false;



	_srmio_pc5_msg_ready( conn );

	DPRINTF("starting %c", cmd );
	if( ! _srmio_pc5_msg_send( conn, cmd, NULL, 0, err ) )
		goto clean1;

	conn->xfer_state = srmio_pc_xfer_state_running;
	conn->xfer_type = srmio_pc_xfer_type_new;
	conn->block_cnt = 1;
	SELF(conn)->block_num = 0;
	SELF(conn)->pkt_num = 0;
	SELF(conn)->chunk_num = PC5_PKT_CHUNKS; /* triggers first pkt xfer */


	/* get header + number of pkts to read */
	ret = _srmio_pc5_read( conn, SELF(conn)->pkt_data,
		SELF(conn)->stxetx ? 4 : 3, err );
	DUMPHEX( "xfer response", SELF(conn)->pkt_data, ret );
	if( ret < 0 ){
		goto clean2;
	}

	if( SELF(conn)->stxetx ){
		/* TODO: how to distinguish "3 pkts" and 0 + ETX?
		 * both: 0x02/  0x41/A 0x00/  0x03/
		 * this is worked around in get_pkt() */
		if( ret < 4 ){
			srmio_error_set( err, "got incomplete download response" );
			goto clean2;

		} else if( SELF(conn)->pkt_data[0] != STX
			|| SELF(conn)->pkt_data[1] != cmd ){

			srmio_error_set( err, "download response is garbled" );
			goto clean2;
		}

		SELF(conn)->pkt_cnt = buf_get_buint16( SELF(conn)->pkt_data, 2 );
		/* TODO: number of pkts for "download all" is wrong in FW 6b09*/

	} else {
		if( ret < 2 ){
			srmio_error_set( err, "got incomplete download response" );
			goto clean2;

		} else if( ret > 2 ){
			SELF(conn)->pkt_cnt = buf_get_buint16(
			SELF(conn)->pkt_data, 1 );

		} else {
			SELF(conn)->pkt_cnt = SELF(conn)->pkt_data[1];

		}

	}
	DPRINTF( "expecting %u pkts", SELF(conn)->pkt_cnt );
	if( (unsigned long)SELF(conn)->pkt_cnt * PC5_PKT_CHUNKS > UINT16_MAX ){
		srmio_error_set( err, "PCV reports too many packets: %u",
			SELF(conn)->pkt_cnt );
		goto clean2;
	}
	SELF(conn)->block.total = 1+SELF(conn)->pkt_cnt;

	return true;

clean2:
	conn->xfer_state = srmio_pc_xfer_state_failed;

clean1:
	free(SELF(conn)->block.athlete);
	SELF(conn)->block.athlete = NULL;
	return false;
}

/*
 * returns false if there's no further block to retrieve
 * or if there was an error.
 */
static bool _srmio_pc5_xfer_block_next( srmio_pc_t conn, srmio_pc_xfer_block_t block )
{
	assert(conn);
	assert(block);

	DPRINTF("");
	if( SELF(conn)->block_num  )
		return false;

	memcpy( block, &SELF(conn)->block, sizeof( struct _srmio_pc_xfer_block_t ));
	SELF(conn)->block.athlete = NULL;

	++SELF(conn)->block_num;

	return true;
}

/*
 * retrieve next chunk from PCV
 * automagically fetches next pkt when last chunk of pkt was returned.
 *
 * returns false if there's no further chunk to retrieve
 * or if there was an error.
 */
static bool _srmio_pc5_xfer_chunk_next( srmio_pc_t conn, srmio_chunk_t chunk,
	bool *is_intervall, bool *start_intervall )
{
	bool is_int, is_first;

	assert( conn );
	assert( chunk );

	while(1){
		unsigned char *cbuf;

		if( conn->xfer_state != srmio_pc_xfer_state_running )
			return false;

		if( SELF(conn)->pkt_num >= SELF(conn)->pkt_cnt ){
			conn->xfer_state = srmio_pc_xfer_state_success;
			return false;
		}

		if( SELF(conn)->chunk_num >= PC5_PKT_CHUNKS )
			if( ! srmio_pc5_xfer_pkt_next( conn ) )
				return false;

		cbuf = &SELF(conn)->pkt_data[9 + 5*SELF(conn)->chunk_num];
#ifdef DEBUG_CHUNK
		DUMPHEX( "", cbuf, 5 );
#endif

		is_first = cbuf[0] & 0x40;
		is_int = cbuf[0] & 0x80;

		if( start_intervall )
			*start_intervall = is_first;
		if( is_intervall )
			*is_intervall = is_int;

		if( 0 == memcmp( cbuf, "\0\0\0\0\0", 5 )){
			DPRINTF( "skipping empty chunk#%u",
				SELF(conn)->chunk_num );
			SELF(conn)->chunk_num++;
			continue;
		}

		chunk->time = SELF(conn)->pkt_time
			+ SELF(conn)->chunk_num * SELF(conn)->pkt_recint;
		if( chunk->time < SELF(conn)->pkt_time ){
			srmio_error_set( &conn->err, "chunk time had INT overflow" );
			conn->xfer_state = srmio_pc_xfer_state_failed;
			return false;
		}
		chunk->dur = SELF(conn)->pkt_recint;

		chunk->temp = SELF(conn)->pkt_temp;
		chunk->pwr = 0x0fff & buf_get_buint16( cbuf, 0 );
		chunk->speed =  (double)0.2 * (
			( ( cbuf[0] & 0x30) << 4)
			| cbuf[2] );
		chunk->cad = cbuf[3];
		chunk->hr = cbuf[4];
		chunk->ele = 0;

		DPRINTF( "%u: "
			"time=%.1f, "
			"temp=%.1f, "
			"pwr=%u, "
			"spd=%.3f, "
			"cad=%u, "
			"hr=%u, "
			"is_int=%d, "
			"is_first=%d",
			SELF(conn)->chunk_num,
			(double)chunk->time/10,
			chunk->temp,
			chunk->pwr,
			chunk->speed,
			chunk->cad,
			chunk->hr,
			is_int,
			is_first);

		SELF(conn)->chunk_num++;

		/* TODO: verify data when display is non-metric */
		/* TODO: verify temperature < 0°C */

		break;
	}

	return true;
}



/*
 * finalize chunk download
 */
static bool _srmio_pc5_xfer_finish( srmio_pc_t conn, srmio_error_t *err )
{
	assert( conn );

	DPRINTF("");

	/* abort */
	if( SELF(conn)->pkt_num < SELF(conn)->pkt_cnt ){
		STATMSG( "aborting download" );
		srmio_io_flush( conn->io, err );
		_srmio_pc5_write( conn, BLOCK_ABRT, 1, err );
		sleep(1);

	} else {
		/* read (and ignore) trailing ETX */
		if( SELF(conn)->stxetx )
			if( 1 == _srmio_pc5_read( conn,
				SELF(conn)->pkt_data, 1, err ) )
				DPRINTF( "final ETX: %02x",
				*SELF(conn)->pkt_data );

	}

	/* forget junk, PCV might've send after transfer */
	srmio_io_flush( conn->io, err );

	conn->xfer_state = srmio_pc_xfer_state_new;

#ifdef DEBUG
	/* issue *some* command for troubleshooting "stuck" PCV after
	 * download: */
	{
		unsigned version;
		srmio_pc5_cmd_get_version( conn, &version, NULL );
	}
#endif

	return true;
}

/*
 * gets current transfer status and progress
 *
 */
static bool _srmio_pc5_xfer_block_progress( srmio_pc_t conn,
	size_t *block_done )
{
	assert( conn );

	if( block_done )
		*block_done = SELF(conn)->pkt_num;

	return conn->xfer_state;
}

/************************************************************
 *
 * handle management
 */

/*
 * cleans up and frees a connection handle
 */
static void _srmio_pc5_free( srmio_pc_t conn )
{
	assert( conn );

	free( SELF(conn) );
}

static const srmio_pc_methods_t _pc5_methods = {
	.free				= _srmio_pc5_free,
	.open				= _srmio_pc5_open,
	.close				= _srmio_pc5_close,
	.cmd_get_athlete		= _srmio_pc5_cmd_get_athlete,
	.cmd_set_time			= _srmio_pc5_cmd_set_time,
	.cmd_set_recint			= _srmio_pc5_cmd_set_recint,
	.cmd_clear			= _srmio_pc5_cmd_clear,
	.xfer_start			= _srmio_pc5_xfer_start,
	.xfer_block_next		= _srmio_pc5_xfer_block_next,
	.xfer_block_progress		= _srmio_pc5_xfer_block_progress,
	.xfer_chunk_next		= _srmio_pc5_xfer_chunk_next,
	.xfer_finish			= _srmio_pc5_xfer_finish,
};

/*
 * allocate new connection handle for accessing the PCV
 */
srmio_pc_t srmio_pc5_new( srmio_error_t *err )
{
	srmio_pc5_t self;
	srmio_pc_t conn;

	if( NULL == (self = malloc(sizeof(struct _srmio_pc5_t)))){
		srmio_error_errno( err, "new pc5" );
		return NULL;
	}

	memset(self, 0, sizeof( struct _srmio_pc5_t ));

	if( NULL == ( conn = srmio_pc_new( &_pc5_methods, (void*)self, err )))
		goto clean1;

	conn->can_preview = false;
	conn->baudrate = srmio_io_baud_max;
	conn->parity = srmio_io_parity_max;

	return conn;

clean1:
	free(self);
	return NULL;
}


