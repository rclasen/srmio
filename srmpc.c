/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */


#include "common.h"
#include <stdarg.h>


#define STX	((char)0x2)
#define ETX	((char)0x3)

#define BLOCK_ACK	((const unsigned char *)"\x06")
#define BLOCK_NAK	((const unsigned char *)"\x15")
#define BLOCK_ABRT	((const unsigned char *)"\xaa")

/*
 * I'm a chicken and won't touch any SRM I don't know.
 *
 * This is a good(tm) idea as a misencoded command argument leads to
 * broken settings within the SRM. Seems the SRM's input checking is
 * limited.
 */
static const unsigned _srmpc_whitelist[] = {
	0x6b09,		/* fw 6b.09 - uses stxetx */
	0x4309,		/* fw 43.09 - no stxetx, fw was upgraded 04/2009 */
	0x3404,		/* fw 34.04 - no stxetx, reported by MorganFletcher */
	0,
};

/*
 * supported baudrates in order to try
 * default is 9600 8n1
 * srmwin tries:
 *  9600,4800,19200,2400 Baud
 *  each baudrate with none, then with even parity
 */
typedef enum {
	_srmpc_baud_9600,
	_srmpc_baud_19200,
	_srmpc_baud_4800,
	_srmpc_baud_2400,
	_srmpc_baud_max
} _srmpc_baudrate_t;

static const tcflag_t _srmpc_baudrates[_srmpc_baud_max] = {
	B9600,
	B19200,
	B4800,
	B2400,
};

static const int _srmpc_baudnames[_srmpc_baud_max] = {
	9600,
	19200,
	4800,
	2400,
};

static int _srmpc_msg_decode( 
	unsigned char *out, size_t olen,
	const unsigned char *in, size_t ilen );

static int _srmpc_msg_send( srmpc_conn_t conn, 
	char cmd, const unsigned char *arg, size_t alen );


/************************************************************
 *
 * helper / logging
 *
 ************************************************************/


static void _srm_log( srmpc_conn_t conn, const char *fmt, ... )
{
	va_list ap;
	char buf[SRM_BUFSIZE];

#ifndef DEBUG
	if( ! conn->lfunc )
		return;
#endif

	va_start( ap, fmt );

	if( 0 > vsnprintf(buf, SRM_BUFSIZE, fmt, ap ) )
		return
		;
	va_end( ap );
	
	DPRINTF("_srm_log: %s", buf );
#ifdef DEBUG
	if( conn->lfunc )
#endif
		(*conn->lfunc)( buf );
}

/************************************************************
 *
 * low level IO (open/read/write/close)
 *
 ************************************************************/

/*
 * send data towards SRM, 
 * automagically take care of pending IOFLUSH
 *
 * returns number of chars written
 * on error errno is set and returns -1
 */
static int _srmpc_write( srmpc_conn_t conn, const unsigned char *buf, size_t blen )
{
	int ret;

	DUMPHEX( "_srmpc_write", buf, blen );

	if( tcflush( conn->fd, TCIOFLUSH ) )
		return -1;

	ret = write( conn->fd, buf, blen );
	if( ret < 0 )
		return -1;

	if( (size_t)ret < blen ){
		errno = EIO;
		return -1;
	}

	if( 0 > tcdrain( conn->fd ) )
		return -1;

	return ret;
}

/*
 * read specified number of bytes
 *
 * returns number of chars read
 * on error errno is set and returns -1
 */
static int _srmpc_read( srmpc_conn_t conn, unsigned char *buf, size_t want )
{
	size_t got = 0;
	int ret;
	
	while( got < want ){
		ret = read( conn->fd, &buf[got], 1 );
		if( ret < 0 )
			return -1;

		if( ret < 1 )
			return got;

		got += ret;
	}

	return got;
}

/*
 * set serial parameter
 * wake up PCV
 * send "hello"
 * check returned version
 * decode version
 * return detected version (or -1 on error)
 */
static int _srmpc_init(
	srmpc_conn_t conn,
	_srmpc_baudrate_t baudrate,
	int parity )
{
	struct termios	ios;
	int		ret;
	unsigned char	buf[20];
	unsigned char	verbuf[2];

	if( baudrate >= _srmpc_baud_max ){
		_srm_log( conn, "srmpc_init: invalid baud request: %d", baudrate );
		errno = EINVAL;
		return -1;
	}

	conn->stxetx = 1;
	_srm_log( conn, "trying comm %d/8%c1", 
		_srmpc_baudnames[baudrate], 
		parity ? 'e' : 'n' );

#ifdef HAVE_CFMAKERAW
	cfmakeraw( &ios );
#else
	memset(&ios, 0, sizeof(struct termios));
#endif
	ios.c_cflag = CS8 | CLOCAL | CREAD;
	if( parity ) ios.c_cflag |= PARENB;
	if( 0 > cfsetispeed( &ios, _srmpc_baudrates[baudrate] ) ){
		_srm_log( conn, "cfsetispeed failed: %s", strerror(errno));
		return -1;
	}
	if( 0 > cfsetospeed( &ios, _srmpc_baudrates[baudrate] ) ){
		_srm_log( conn, "cfsetospeed failed: %s", strerror(errno));
		return -1;
	}

	ios.c_iflag = IGNPAR;

	ios.c_cc[VMIN] = 0;
	ios.c_cc[VTIME] = 10;


	if( tcflush( conn->fd, TCIOFLUSH ) ){
		_srm_log( conn, "tcflush failed: %s", strerror(errno));
		return -1;
	}

	if( tcsetattr( conn->fd, TCSANOW, &ios ) ){
		_srm_log( conn, "tcsetattr failed: %s", strerror(errno));
		return -1;
	}

	tcsendbreak( conn->fd, 0 );

	/* send opening 'P' to verify communication works */
	ret = _srmpc_msg_send( conn, 'P', NULL, 0 );
	if( ret < 0 )
		goto clean1;

	ret = _srmpc_read( conn, buf, 20 );
	DPRINTF( "srmpc_init ret %d", ret );
	if( ret < 0 ){
		_srm_log( conn, "srmpc_read failed: %s", strerror(errno));
		goto clean2;
	}
	if( ret == 0 ){
		_srm_log( conn, "got no opening response" );
		errno = EHOSTDOWN;
		goto clean2;
	}
	conn->cmd_running = 0;

	DUMPHEX( "_srmpc_init got ", buf, ret );

	/* autodetect communcitation type */
	if( *buf == STX ){
		if( ret != 7 || buf[1] != 'P' ){
			_srm_log( conn, "opening response is garbled" );
			errno = EPROTO;
			goto clean2;
		}

		if( 0 >  _srmpc_msg_decode( verbuf, 2, &buf[2], 4 ) ){
			_srm_log( conn, "msg decode failed: %s", strerror(errno));
			goto clean2;
		}

	} else {
		if( ret != 3 ||  buf[0] != 'P' ){
			_srm_log( conn, "opening response is garbled" );
			errno = EPROTO;
			goto clean2;
		}

		conn->stxetx = 0;
		DPRINTF( "_srmpc_init: disabling stx/etx" );

		memcpy( verbuf, &buf[1], 2 );
	}

	return ( verbuf[0] << 8 ) | verbuf[1];

clean2:
	conn->cmd_running = 0;
clean1:
	/* no need for termios cleanup - handled by srmpc_open() */
	return -1;
}


static int _srmpc_init_all( srmpc_conn_t conn )
{
	_srmpc_baudrate_t baudrate;

	for( baudrate = 0; baudrate < _srmpc_baud_max; ++baudrate ){
		int parity;

		for( parity = 0; parity < 2; ++parity ){
			int	ret;

			ret = _srmpc_init( conn, baudrate, parity );
			if( ret >= 0 ){
				_srm_log( conn, "found PCV version 0x%x at %d/8%c1", 
					(unsigned)ret, 
					_srmpc_baudnames[baudrate], 
					parity ? 'e' : 'n' );

				return ret;
			}
		}
	}

	_srm_log( conn, "no PCV found" );
	errno = ENOTSUP;
	return -1;
}


/*
 * allocate internal data structures,
 * open serial device
 * set serial device properties
 *
 * parameter:
 *  fname: path to serial device
 *  force: ignore whitelist (when != 0)
 *  lfunc: function to invoke for verbose status reporting
 *
 * returns newly allocated connection handle.
 *
 * on error errno is set and returns NULL
 */
srmpc_conn_t srmpc_open( const char *fname, int force,
	srmpc_log_callback_t lfunc )
{
	srmpc_conn_t	conn;
	int		pcv;

	DPRINTF( "srmpc_open %s", fname );

	if( NULL == (conn = malloc(sizeof(struct _srmpc_conn_t))))
		return NULL;
	conn->lfunc = lfunc;

	_srm_log( conn, "%s opening device %s",
		PACKAGE_STRING,
		fname );

	/* TODO: uucp style lockfils */

	if( 0 > (conn->fd = open( fname, O_RDWR | O_NOCTTY ))){
		_srm_log( conn, "open failed: %s", strerror(errno) );
		goto clean2;
	}

	/* get serial comm parameter for restore on close*/
	if( tcgetattr( conn->fd, &conn->oldios ) ){
		_srm_log( conn, "tcgetattr failed: %s", strerror(errno) );
		goto clean3;
	}

	/* set serial parameter and get PCV version */
	/* TODO: allow user to specify baudrate+parity */
	if( 0 > ( pcv = _srmpc_init_all( conn )))
		goto clean4;


	/* verify it's a known/supported PCV */
	if( ! force ){
		int known = 0;
		const unsigned *white;

		for( white = _srmpc_whitelist; *white; ++white ){
			if( (unsigned)pcv == *white ){
				++known;
				break;
			}
		}
		if( ! known ){
			_srm_log( conn, "PC Version 0x%x not whitelisted",
				pcv );
			errno = ENOTSUP;
			goto clean4;
		}
	}

	return conn;

clean4:
	tcflush( conn->fd, TCIOFLUSH );
	tcsetattr( conn->fd, TCSANOW, &conn->oldios );

clean3:
	close(conn->fd);

clean2:
	free(conn);
	return NULL;
}

/*
 * close serial device
 * and free memory
 *
 * parameter:
 *  conn: connection handle
 *
 * returns: nothing
 *
 */
void srmpc_close( srmpc_conn_t conn )
{
	DPRINTF( "srmpc_close" );
	if( ! conn )
		return;

	tcflush( conn->fd, TCIOFLUSH );
	if( tcsetattr( conn->fd, TCSANOW, &conn->oldios ) )
		_srm_log( conn, "failed to restore ios state: %s",
			strerror(errno) );

	close( conn->fd );
	/* TODO: uu_unlock */
	free( conn );
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
 * on error errno is set and returns -1
 */
static int _srmpc_msg_encode( 
	unsigned char *out, size_t olen,
	const unsigned char *in, size_t ilen ) 
{
	size_t i;
	size_t o=0;

	if( 2 * ilen < ilen ){
		ERRMSG( "_srmpc_msg_encode: input too large, INT overflow" );
		errno = EOVERFLOW;
		return -1;
	}

	if( olen < 2 * ilen ){
		ERRMSG( "_srmpc_msg_encode: dst buffer too small %lu/%lu", 
			(unsigned long)ilen, 
			(unsigned long)olen);
		errno = ERANGE;
		return -1;
	}

	for( i=0; i < ilen; ++i ){
		out[o++] = ((in[i] & 0xf0 ) >> 4) | 0x30;
		out[o++] = (in[i] & 0x0f ) | 0x30;
	}

	DUMPHEX( "_srmpc_msg_encode", out, o );
	return o;
}


/*
 * decode "ASCII-friendly" encoded data
 *
 * returns number of decoded bytes
 * on error errno is set and returns -1
 */
static int _srmpc_msg_decode( 
	unsigned char *out, size_t olen,
	const unsigned char *in, size_t ilen ) 
{
	int o=0;

	if( olen * 2 < olen ){
		ERRMSG( "_srmpc_msg_decode: input too large, INT overflow");
		errno = EOVERFLOW;
		return -1;
	}

	if( ilen % 2 ){
		ERRMSG( "_srmpc_msg_decode: uneven input size: %lu",
			(unsigned long)ilen );
		errno = EINVAL;
		return -1;
	}

	if( olen * 2 < ilen ){
		ERRMSG( "_srmpc_msg_decode: dst buffer too small %lu/%lu",
			(unsigned long)ilen,
			(unsigned long)olen);
		errno = ERANGE;
		return -1;
	}

	while( ilen > 1 ){
		out[o] = ((in[0] - 0x30) << 4)
			| (in[1] - 0x30);
		ilen -= 2;
		in += 2;
		++o;
	}

	DUMPHEX( "_srmpc_msg_decode", out, o );
	return o;
}


/*
 * send command to SRM,
 *
 * takes care of stx/etx/encode when neccessary.
 *
 * returns 0 on success
 * on error errno is set and returns -1
 */
static int _srmpc_msg_send( srmpc_conn_t conn, char cmd, const unsigned char *arg, size_t alen )
{
	unsigned char buf[SRM_BUFSIZE];
	unsigned len = 0;
	int ret;

	DPRINTF( "_srmpc_msg_send %c ...", cmd );
	DUMPHEX( "_srmpc_msg_send", arg, alen );


	if( conn->cmd_running ){
		_srm_log( conn, "another command is still running" );
		errno = EBUSY;
		return -1;
	}

	if( 2 * alen +3 < alen ){
		_srm_log( conn, "too large argument, INT overflow" );
		errno = EOVERFLOW;
		return -1;
	}

	/* sledgehammer aproach to prevent exceeding the buffer: */
	if( 2 * alen +3 > SRM_BUFSIZE ){
		_srm_log( conn, "too large argument for buffer" );
		errno = ERANGE;
		return -1;
	}

	/* build command */
	if( conn->stxetx ){
		buf[len++] = STX;
		buf[len++] = cmd;

		ret = _srmpc_msg_encode( &buf[len], SRM_BUFSIZE-2, arg, alen );
		if( ret < 0 ){
			_srm_log( conn, "failed to encode message: %s", strerror(errno) );
			return -1;
		}

		len += ret;
		buf[len++] = ETX;

	} else {
		buf[len++] = cmd;
		if( alen > 0 ){
			memcpy(&buf[len], arg, alen );
			len += alen;
		}
	}


	/* send */
	ret = _srmpc_write( conn, buf, len );
	if( ret < 0 ){
		_srm_log( conn, "send failed: %s", strerror(errno) );
		return -1;

	} else if( (unsigned)ret < len ){
		_srm_log( conn, "failed to send complete command to PC");
		errno = EIO;
		return -1;
	}

	conn->cmd_running = 1;
	return 0;
}

/*
 * read response from SRM up to wanted length
 *
 * takes care of stx/etx/decode internaly when neccessary.
 *
 * response (without command char) is returned in rbuf (when rbuf != NULL)
 *
 * returns 0 on success
 * on error errno is set and returns -1
 */
static int _srmpc_msg_recv( srmpc_conn_t conn, unsigned char *rbuf, size_t rsize, size_t want )
{
	unsigned char	buf[SRM_BUFSIZE];
	size_t	rlen = 0;
	int	ret;

	if( rbuf && want > rsize ){
		_srm_log( conn, "_srmpc_msg_recv rbuf too small want %lu rsize %lu",
			(unsigned long)want,
			(unsigned long)rsize);
		errno = ERANGE;
		return -1;
	}

	if( 2 * want + 3 < want ){
		_srm_log( conn, "too large argument, INT overflow" );
		errno = EOVERFLOW;
		return -1;
	}

	if( conn->stxetx )
		/* stx, etc, encoding */
		want = 2*want +2;

 	/* cmd char */
	++want;


	DPRINTF( "_srmpc_msg_recv will read %lu ", (unsigned long)want );

	if( want >= SRM_BUFSIZE ){
		_srm_log( conn, "_srmpc_msg_recv tmp buf too small for rsize=%lu",
			(unsigned long)rsize );
		errno = ERANGE;
		return -1;
	}

	/* read till
	 * - rsize is read
	 * - ETX is found (when stxetx is used)
	 * - read times out
	 */
	while( rlen < want ){
		ret = _srmpc_read( conn, &buf[rlen], 1 );

		if( ret < 0 ){
			_srm_log( conn, "read failed: %s", strerror(errno));
			DUMPHEX( "_srmpc_msg_recv", buf, rlen );
			return -1; /* read failed */

		} 
		
		/* timeout */
		if( ret < 1 ){
			_srm_log( conn, "read msg timeout" );
			break;
		}

		++rlen;

		/* avoid running into timeout */
		if( conn->stxetx && buf[rlen-1] == ETX ){
			DPRINTF( "_srmpc_msg_recv ETX" );
			break;
		}

	}
	DPRINTF( "_srmpc_msg_recv got %lu chars", (unsigned long)rlen );
	DUMPHEX( "_srmpc_msg_recv", buf, rlen );
	
	if( ! rlen ){
		_srm_log( conn, "response timed out" );
		errno = ETIMEDOUT;
		return -1;
	}

	conn->cmd_running = 0;
	if( conn->stxetx ){
		if( rlen < 3 ){
			_srm_log( conn, "_srmpc_msg_recv response is too short" );
			errno = EBADMSG;
			return -1;
		}

		if( buf[0] != STX ){
			_srm_log( conn, "_srmpc_msg_recv STX is missing" );
			errno = EPROTO;
			return -1;
		}

		if( buf[rlen-1] != ETX ){
			_srm_log( conn, "_srmpc_msg_recv ETX is missing" );
			errno = EPROTO;
			return -1;
		}
		rlen -= 3; /* stx, cmd, etx */

		if( rbuf ){
			ret = _srmpc_msg_decode(rbuf, rsize, &buf[2], rlen );
			if( ret < 0 ){
				_srm_log( conn, "msg decode failed: %s", strerror(errno));
				return -1;
			}
			rlen = ret;
		} else
			rlen /= 2;

	} else {
		if( rlen < 1 ){
			_srm_log( conn, "_srmpc_msg_recv response is too short" );
			errno = EBADMSG;
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
 * sets the time to wait until the PC accepts new commands.
 *
 * parameters:
 *  conn: connection handle
 *  delay: seconds the PCV will be busy
 *
 * returns 0 on success
 * on error errno is set and returns -1
 */
static int _srmpc_msg_busy( srmpc_conn_t conn, unsigned delay )
{
	time( &conn->nready );
	conn->nready += delay;

	return 0;
}

/*
 * Wait for PCV to finish last command and become ready to 
 * accept the next one (if necessary)
 *
 * returns 0 on success
 * on error errno is set and returns -1
 */
static int _srmpc_msg_ready( srmpc_conn_t conn )
{
	time_t now;
	unsigned delta;

	time( &now );
	if( conn->nready <= now )
		return 0;

	delta = conn->nready - now;

	_srm_log( conn, "PC still busy for %usec, waiting...", delta );
	sleep( delta );

	return 0;
}

/*
 * process one complete command (send, receive), 
 * retry if necesssary
 *
 * returns 0 on success
 * on error errno is set and returns -1
 */
static int _srmpc_msg( srmpc_conn_t conn, char cmd, 
	const unsigned char *arg,
	size_t alen, 
	unsigned char *buf,
	size_t blen,
	size_t want )
{
	int retries;
	int ret;

	if( 0 > _srmpc_msg_ready( conn )){
		_srm_log( conn, "failed to wait for PC becoming available: %s", strerror(errno));
		return -1;
	}

	for( retries = 0; retries < 3; ++retries ){

		if( retries ){
			conn->cmd_running = 0;
			_srm_log( conn, "SRM isn't responding, send break and retry" );
			tcsendbreak( conn->fd, 0 );
			sleep(1);
		}

		if( _srmpc_msg_send( conn, cmd, arg, alen ) )
			return -1;

		ret = _srmpc_msg_recv( conn, buf, blen, want);
		/* TODO: retry on all errors? */
		if( ret >= 0 || errno != ETIMEDOUT )
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
 * returns integer firmware version
 * on error errno is set and returns -1
 */
int srmpc_get_version( srmpc_conn_t conn )
{
	unsigned char buf[20];
	int ret;

	ret = _srmpc_msg( conn, 'P', NULL, 0, buf, 20, 2 );
	if( ret < 0 )
		return -1;
	if( ret < 2 ){
		_srm_log( conn, "got truncated version response" );
		errno = EPROTO;
		return -1;
	}

	return ( buf[0] << 8 ) | buf[1];
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
 * returns malloc()ed string on succuess
 * on error errno is set and returns -1
 */
char *srmpc_get_athlete( srmpc_conn_t conn )
{
	unsigned char buf[20];
	int ret;

	ret = _srmpc_msg( conn, 'N', (unsigned char*)"\x0", 1, buf, 20, 8 );
	if( ret < 0 )
		return NULL;
	if( ret < 6 ){
		_srm_log( conn, "got truncated athlete response" );
		errno = EPROTO;
		return NULL;
	}

	buf[6] = 0;
	DPRINTF( "srmpc_get_athlete got=%s", (char*)buf );
	/* TODO: iconv cp850 -> internal ?? */
	return strdup( (char*)buf );
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
 * returns 0 on succuess
 * on error errno is set and returns -1
 */
int srmpc_get_time( srmpc_conn_t conn, struct tm *timep )
{
	unsigned char buf[20];
	int ret;

	ret = _srmpc_msg( conn, 'M', (unsigned char*)"\x0", 1, buf, 20, 6 );
	if( ret < 0 )
		return -1;
	if( ret < 6 ){
		_srm_log( conn, "got truncated time response" );
		errno = EPROTO;
		return -1;
	}

	timep->tm_mday = TIMEDEC( buf[0] );
	timep->tm_mon = TIMEDEC( buf[1] ) -1;
	timep->tm_year = TIMEDEC( buf[2] ) + 100;
	timep->tm_hour = TIMEDEC( buf[3] );
	timep->tm_min = TIMEDEC( buf[4] );
	timep->tm_sec = TIMEDEC( buf[5] );
	timep->tm_isdst = -1;

	DPRINTF( "srmpc_get_time time=%s", asctime( timep ) );

	return 0;
}

/*
 * set PCV clock to specified date + time
 *
 * parameter:
 *  conn: connection handle
 *  timep: time to set (see "man mktime" for struct tm)
 *
 * returns 0 on succuess
 * on error errno is set and returns -1
 */
int srmpc_set_time( srmpc_conn_t conn, struct tm *timep )
{
	unsigned char buf[6];

	/* TODO: check for overflows */
	buf[0] = TIMEENC( timep->tm_mday );
	buf[1] = TIMEENC( timep->tm_mon +1 );
	buf[2] = TIMEENC( timep->tm_year -100 );
	buf[3] = TIMEENC( timep->tm_hour );
	buf[4] = TIMEENC( timep->tm_min );
	buf[5] = TIMEENC( timep->tm_sec );

	if( 0 > _srmpc_msg( conn, 'M', buf, 6, NULL, 0, 0 ))
		return -1;
		
	DPRINTF( "srmpc_set_time set %s", asctime(timep) );
	_srmpc_msg_busy( conn, 2 );
	return 0;
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
 * returns circumference on succuess
 * on error errno is set and returns -1
 */
int srmpc_get_circum( srmpc_conn_t conn )
{
	unsigned char buf[20];
	int ret;
	int circum;

	ret = _srmpc_msg( conn, 'G', (unsigned char*)"\x0\x0", 2, buf, 20, 2 );
	if( ret < 0 )
		return -1;
	if( ret < 2 ){
		_srm_log( conn, "got truncated circumference response" );
		errno = EPROTO;
		return -1;
	}

	circum = ( buf[0] << 8 ) 
		| ( buf[1] );
	DPRINTF( "srmpc_get_circum circum=%d", circum );
	return circum;
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
 * returns slope  eg. 17.4
 * on error errno is set and returns < 0
 */
double srmpc_get_slope( srmpc_conn_t conn )
{
	unsigned char buf[20];
	int ret;
	double slope;

	ret = _srmpc_msg( conn, 'E', (unsigned char*)"\x0\x0", 2, buf, 20, 2 );
	if( ret < 0 )
		return -1;
	if( ret < 2 ){
		_srm_log( conn, "got truncated slope response" );
		errno = EPROTO;
		return -1;
	}

	slope = (double)(( buf[0] <<8 ) | buf[1]  ) / 10;
	DPRINTF( "srmpc_get_slope slope=%.1f", slope );
	return slope;
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
 * returns circumference on succuess
 * on error errno is set and returns -1
 */
int srmpc_get_zeropos( srmpc_conn_t conn )
{
	unsigned char buf[20];
	int ret;
	int zeropos;

	ret = _srmpc_msg( conn, 'F', (unsigned char*)"\x0\x0", 2, buf, 20, 2 );
	if( ret < 0 )
		return -1;
	if( ret < 2 ){
		_srm_log( conn, "got truncated offset response" );
		errno = EPROTO;
		return -1;
	}

	zeropos = ( buf[0] << 8) | buf[1];
	DPRINTF( "srmpc_get_zeropos zeropos=%d", zeropos );
	return zeropos;
}


/*
 * setting:	recording interval
 * command:	R
 * argument:	unsigned char, ASCII-friendly
 * 
 * if bit7 == 0 
 * 	bits 0-3 have the actual recint in seconds: 1-15
 * else
 *	bits 0-3 have the digit behind the decimal point: 0.1 - 0.9
 */

/*
 * get recording interval
 *
 * parameter:
 *  conn: connection handle
 *
 * returns recint * 10 -> 1sec becomes 10
 * on error errno is set and returns -1
 */
int srmpc_get_recint( srmpc_conn_t conn )
{
	unsigned char buf[10];
	int ret;
	int recint;

	ret = _srmpc_msg( conn, 'R', (unsigned char*)"\x0", 1, buf, 10, 1 );
	if( ret < 0 )
		return -1;
	if( ret < 1 ){
		_srm_log( conn, "got truncated recint response" );
		errno = EPROTO;
		return -1;
	}
	
	recint = ( *buf & 0x80 ) 
		? *buf & 0x0f
		: *buf * 10;

	DPRINTF( "srmpc_get_recint raw=0x%02x recint=%d", *buf, recint);
	return recint;
}

/*
 * set recording interval
 *
 * parameter:
 *  conn: connection handle
 *  recint: new interval (see srmpc_get_recint for details)
 *
 * returns 0 on success
 * on error errno is set and returns -1
 */
int srmpc_set_recint( srmpc_conn_t conn, srm_time_t recint )
{
	unsigned char raw;

	if( recint <= 0 ){
		_srm_log( conn, "recint < 0sec isn't supported" );
		errno = ENOTSUP;
		return -1;
	}

	/* 0.1 .. 0.9 sec? */
	if( recint < 10 ){
		raw = 0x80 | recint;
	
	/* 1 .. 15 sec? */
	} else {
		if( recint > 150 || recint % 10 ){
			_srm_log( conn, "fractional recint > 1sec isn't supported" );
			errno = ENOTSUP;
			return -1;
		}

		raw = recint / 10;
	}

	if( 0 > _srmpc_msg( conn, 'R', &raw, 1, NULL, 0, 1 ) )
		return -1;

	DPRINTF( "srmpc_set_recint: success" );
	return 0;
}


/* TODO: implement srmpc_set_* functions */
/* TODO: investigate/implement other commands */






/************************************************************
 *
 * get/parse data blocks
 *
 ************************************************************/

/*
 * communication to download the data chunks looks like this:
 *  computer: <STX>A<ETX>
 *  SRM:      <STX>A<num_blocks>
 *  SRM:      <64byte_block>
 *  computer: <ACK>/<NAK>
 *    ... repeat last 2 steps for each block
 *  SRM:      <ETX>
 *
 * As with all commands it depends on the FW, if STX/ETX must be
 * omitted/present.
 *
 * The SRM sends pure binary data between the 'A' and the final ETX.
 *
 * num_blocks is encoded as 16bit big-endian integer.
 *
 * The 64 byte blocks comes with a 9 byte "header" followed by 11 data 
 * chunks.
 *
 * block header bytes:
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
 * retrieve next block from PC into buffer
 * parse block-wide data
 * and request next block
 *
 * returns
 *  0 on success
 *  -1 on error
 *  1 when there's no block left
 */
static int _srmpc_get_block( srmpc_get_chunk_t gh )
{
	int retries;
	struct tm btm;
	int ret;

	_srm_log( gh->conn, "getting block %u/%u",
		gh->blocknum +1,
		gh->blocks);

	for( retries = 0; retries < 4; ++retries ){
		if( retries ){
			/* request retransmit */
			_srm_log( gh->conn, "received incomplete block %u, "
				"requesting retransmit", gh->blocknum );
			sleep(1);

			if( 0 > _srmpc_write( gh->conn, BLOCK_NAK, 1 ) ){
				_srm_log( gh->conn, "block NAK failed: %s", strerror(errno) );
				return -1;
			}
		}

		ret = _srmpc_read( gh->conn, gh->buf, 64 );
		DPRINTF( "_srmpc_get_block: got %d chars", ret );
		DUMPHEX( "_srmpc_get_block", gh->buf, ret );

		if( ret < 0 ){
			_srm_log( gh->conn, "read block failed: %s", strerror(errno) );
			/* non-recoverable error */
			return -1;

		} else if( ret == 64 ){
			/* got complete block, terminate retry loop */
			break;

		} else if( ret == 0 && gh->conn->stxetx
			&& gh->blocks == 3
			&& gh->blocknum == 0 ){

			/* workaround stxetx + 3 blocks problem */
			DPRINTF( "_srmpc_get_block: stxetx, fixing blocks=0" );
			gh->blocks = 0;
			gh->finished++;
			return 1;

		} else if( ret == 1 && gh->conn->stxetx
			&& gh->buf[0] == ETX ){

			_srm_log( gh->conn, "got unexpected end of "
				"transfer for block %u", gh->blocknum );
			gh->finished++;
			return 1;
		}

	}

	if( ret != 64 ){
		_srm_log( gh->conn, "got partial chunk, receive timed out" );
		errno = ETIMEDOUT;
		return -1;
	}


	/* confirm receival of block */

	if( 0 > _srmpc_write( gh->conn, BLOCK_ACK, 1 ) ){
		_srm_log( gh->conn, "block ACK failed: %s", strerror(errno) );
		return -1;
	}

	gh->chunknum = 0;
	gh->blocknum++;

	/* parse block */

	btm.tm_year = gh->pctime.tm_year;
	btm.tm_isdst = -1;
	btm.tm_mday = TIMEDEC( gh->buf[0] & 0x3f );
	btm.tm_mon = TIMEDEC( gh->buf[1] & 0x1f ) -1;
	btm.tm_hour = TIMEDEC( gh->buf[2] & 0x3f );
	btm.tm_min = TIMEDEC( gh->buf[3] );
	btm.tm_sec = TIMEDEC( gh->buf[4] );

	if( btm.tm_mon < gh->pctime.tm_mon )
		-- btm.tm_year;

	ret = mktime( &btm );
	if( 0 > ret ){
		_srm_log( gh->conn, "mktime failed: %s", strerror(errno) );
		return -1;
	}

	gh->bstart = (srm_time_t)ret *10;
	gh->dist = ( (gh->buf[5] << 16 )
		| ( gh->buf[6] << 8 )
		| gh->buf[7] )
		/ 3.9;

	gh->temp = gh->buf[8];
	gh->recint = ( (gh->buf[1] & 0xe0) >> 5)
		| ( (gh->buf[0] & 0x40) >> 3);
	if( ! (gh->buf[2] & 0x40) )
		gh->recint *= 10;

	DPRINTF( "_srmpc_get_block mon=%u day=%u hour=%u min=%u sec=%u "
		"dist=%lu temp=%d recint=%.1f na0=%x na2=%x",
		(unsigned)btm.tm_mon,
		(unsigned)btm.tm_mday,
		(unsigned)btm.tm_hour,
		(unsigned)btm.tm_min,
		(unsigned)btm.tm_sec,
		gh->dist,
		gh->temp,
		(double)gh->recint/10,
		(int)( ( gh->buf[0] & 0x80) >> 7 ),
		(int)( ( gh->buf[2] & 0x80) >> 7 ) );

	return 0;
}

/*
 * allocates and parses next chunk from buffer.
 *
 * returns
 *  0 when chunk was returned
 *  1 when there's no chunk left in this block
 *  -1 on error
 */
static int _srmpc_get_chunk( srmpc_get_chunk_t gh, srm_chunk_t *chunkp )
{
	unsigned char *cbuf;
	srm_chunk_t chunk;


	cbuf = &gh->buf[9 + 5*gh->chunknum];
	DUMPHEX( "_srmpc_get_chunk", cbuf, 5 );

	if( NULL == (chunk = srm_chunk_new())){
		_srm_log( gh->conn, "chunk_new failed: %s", strerror(errno));
		return -1;
	}
	*chunkp = chunk;

	gh->isfirst = cbuf[0] & 0x40;
	gh->iscont = cbuf[0] & 0x80;

	if( 0 == memcmp( cbuf, "\0\0\0\0\0", 5 )){
		DPRINTF( "_srmpc_get_chunk: skipping empty chunk#%u",
			gh->chunknum );
		++gh->chunknum;
		return 1;
	}

	chunk->time = gh->bstart
		+ gh->chunknum * gh->recint;
	if( chunk->time < gh->bstart ){
		_srm_log( gh->conn, "chunk time had INT overflow" );
		errno = EOVERFLOW;
		return -1;
	}

	chunk->temp = gh->temp;
	chunk->pwr = ( ( cbuf[0] & 0x0f) << 8 ) | cbuf[1];
	chunk->speed =  (double)0.2 * (
		( ( cbuf[0] & 0x30) << 4)
		| cbuf[2] );
	chunk->cad = cbuf[3];
	chunk->hr = cbuf[4];
	chunk->ele = 0;

	gh->chunknum++;

	/* TODO: verify data when display is non-metric */
	/* TODO: verify temperature < 0°C */

	return 0;
}



/*
 * get all blocks/chunks off the SRM, parse it and pass the decoded chunks
 * to the callback function.
 *
 * parameter:
 *  conn: connection handle
 *  deleted: instruct PCV to send deleted data, too (when != 0)
 *
 * on error errno is set and returns NULL
 */
srmpc_get_chunk_t srmpc_get_chunk_start( srmpc_conn_t conn, int deleted )
{
	srmpc_get_chunk_t gh;
	int ret;
	char cmd;

	if( deleted )
		cmd = 'y';
	else
		cmd = 'A';

	if( NULL == (gh = malloc( sizeof(struct _srmpc_get_chunk_t)) )){
		_srm_log( conn, "malloc failed: %s", strerror(errno));
		return NULL;
	}

	memset(gh, 0, sizeof( struct _srmpc_get_chunk_t ));
	gh->conn = conn;
	gh->chunknum = SRM_BLOCKCHUNKS;

	if( 0 > srmpc_get_time( conn, &gh->pctime ))
		goto clean1;

	if( 0 > _srmpc_msg_ready( conn ))
		goto clean1;

	if( _srmpc_msg_send( conn, cmd, NULL, 0 ) )
		goto clean1;

	/* get header + number of blocks to read */
	ret = _srmpc_read( conn, gh->buf, conn->stxetx ? 4 : 3 );
	DPRINTF( "srmpc_get_chunk_start read %d chars", ret );
	if( ret < 0 ){
		_srm_log( conn, "reading data failed: %s", strerror(errno));
		goto clean1;
	}

	DUMPHEX( "srmpc_get_chunk_start read response", gh->buf, ret );
	if( conn->stxetx ){
		/* TODO: how to distinguish "3 blocks" and 0 + ETX?
		 * both: 0x02/  0x41/A 0x00/  0x03/
		 * this is worked around in get_block() */
		if( ret < 4 ){
			_srm_log( conn, "got incomplete download response" );
			errno = EPROTO;
			goto clean1;
		} else if( gh->buf[0] != STX || gh->buf[1] != cmd ){
			_srm_log( conn, "download response is garbled" );
			errno = EPROTO;
			goto clean1;
		}

		gh->blocks = ( gh->buf[2] << 8) | gh->buf[3];
		/* TODO: number of blocks for "download all" is wrong in FW 6b09*/

	} else {
		if( ret < 2 ){
			_srm_log( conn, "got incomplete download response" );
			errno = EPROTO;
			goto clean1;

		} else if( ret > 2 ){
			gh->blocks = ( gh->buf[1] << 8 ) | gh->buf[2];

		} else {
			gh->blocks = gh->buf[1];

		}

	}
	DPRINTF( "srmpc_get_chunk_start expecting %u blocks", gh->blocks );
	if( (unsigned long)gh->blocks * SRM_BLOCKCHUNKS > UINT16_MAX ){
		_srm_log( conn, "cannot handle that many blocks, sorry" );
		errno = EOVERFLOW;
		goto clean1;
	}

	return gh;
clean1:
	free(gh);
	return NULL;
}

/*
 * retrieve next chunk from PC
 * automagically fetches next block when last chunk of block was returned.
 * returns NULL when last chunk was returned
 */
srm_chunk_t srmpc_get_chunk_next( srmpc_get_chunk_t gh )
{
	srm_chunk_t chunk;
	int ret;

	do {
		if( gh->finished )
			return NULL;

		if( gh->blocknum >= gh->blocks )
			return NULL;

		if( gh->chunknum >= SRM_BLOCKCHUNKS ){

			if( _srmpc_get_block( gh ) )
				return NULL;

		}

		ret = _srmpc_get_chunk( gh, &chunk );
		if( ret < 0 )
			return NULL;

	} while( ret || ! chunk );

	return chunk;
}



/*
 * finalize chunk download
 * free get_chunk_t handle
 */
void srmpc_get_chunk_done( srmpc_get_chunk_t gh )
{
	if( gh->blocks ){

		/* abort */
		if( gh->blocknum < gh->blocks ){
			_srm_log( gh->conn, "aborting download" );
			tcflush( gh->conn->fd, TCIOFLUSH );
			_srmpc_write( gh->conn, BLOCK_ABRT, 1 );

		/* read (and ignore) trailing ETX */
		} else if( gh->conn->stxetx ){
			if( 1 == _srmpc_read( gh->conn, gh->buf, 1 ) )
			DPRINTF( "srmpc_get_chunk_done final ETX: %02x",
				*gh->buf );
		}
	}
	gh->conn->cmd_running = 0;

#ifdef DEBUG
	/* issue *some* command for troubleshooting "stuck" PCV after
	 * download: */
	srmpc_get_version( gh->conn );
#endif

	free(gh);
}


/*
 * wrapper arround _start(), _next(), _done() for backwards compatible
 * callback based interface for chunk download.
 *
 * parameter:
 *  conn: connection handle
 *  deleted: instruct PCV to send deleted data, too (when != 0)
 *  cbfunc: callback to process each retrieved data chunk
 *  cbdata: passed to cbfunc
 *
 * on error errno is set and returns NULL
 */
int srmpc_get_chunks(
	srmpc_conn_t conn,
	int deleted,
	srmpc_chunk_callback_t cbfunc,
	void *cbdata )
{
	srmpc_get_chunk_t gh;
	srm_chunk_t chunk;

	if( ! cbfunc )
		return 0;

	if( NULL == (gh = srmpc_get_chunk_start( conn, deleted ) ))
		return -1;

	while( NULL != ( chunk = srmpc_get_chunk_next( gh ))){
		if( (*cbfunc)( gh, cbdata, chunk ) )
			return -1;
	}

	srmpc_get_chunk_done( gh );

	return 0;
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
 * returns 0 on success
 * on error errno is set and returns -1
 */
int srmpc_clear_chunks( srmpc_conn_t conn )
{
	/* TODO: what's the individual effect of B and T?
	 *
	 * T seems to do the actual clean.
	 */

	if( 0 >  _srmpc_msg( conn, 'B', NULL, 0, NULL, 0, 0 ) )
		return -1;

	if( 0 >  _srmpc_msg( conn, 'T', NULL, 0, NULL, 0, 0 ) )
		return -1;

	DPRINTF( "srmpc_clear_chunks: success" );
	return 0;
}

/************************************************************
 *
 * use srmpc_get_chunks() to fill srm_data_t structure 
 * with all chunks.
 *
 * Also serves as example on how to use the download API.
 *
 ************************************************************/

/*
 * retrieve recorded data from PC and build  "friendly" srm_data_t structure.
 *
 * parameter:
 *  conn: connection handle
 *  deleted: instruct PCV to send deleted data, too (when != 0). 
 *          Same as pressing shift while downloading in srmwin.
 *  fixup: postprocess data (fix timestamps, fill micro-gaps, ...) (when != 0)
 *
 * returns pointer to newly allocated srm_data_t structure.
 * on error NULL is returned and errno is set.
 */
srm_data_t srmpc_get_data( srmpc_conn_t conn, int deleted, int fixup )
{
	int mfirst = -1;
	srm_data_t data;
	srmpc_get_chunk_t gh;
	srm_chunk_t chunk;
	srm_marker_t mk;
	int ret;

	if( NULL == (data = srm_data_new())){
		_srm_log( conn, "srm_data_new failed: %s", strerror(errno));
		return NULL;
	}

	if( NULL == (mk = srm_marker_new() )){
		_srm_log( conn, "srm_marker_new failed: %s", strerror(errno));
		goto clean1;
	}

	if( 0 > srm_data_add_markerp( data, mk )){
		_srm_log( conn, "adding marker failed: %s", strerror(errno));
		srm_marker_free( mk );
		goto clean1;
	}


	/* get metadata */

	if( 0 > (data->slope = srmpc_get_slope( conn ) ))
		goto clean1;

	if( 0 > ( ret = srmpc_get_zeropos( conn ) ))
		goto clean1;
	data->zeropos = ret;

	if( 0 > ( ret = srmpc_get_circum( conn ) ))
		goto clean1;
	data->circum = ret;

	if( NULL == (mk->notes = srmpc_get_athlete( conn ) ))
		goto clean1;


	/* get chunks */

	if( NULL == (gh = srmpc_get_chunk_start( conn, deleted ) ))
		goto clean1;

	while( NULL != ( chunk = srmpc_get_chunk_next( gh ))){

		if( 0 > srm_data_add_chunk( data, chunk ) ){
			_srm_log( conn, "add chunk failed: %s", strerror(errno));
			goto clean2;
		}

		/* finish previous marker */
		if( mfirst >= 0 && ( ! gh->iscont || gh->isfirst ) )
			srm_data_add_marker( data, mfirst, data->cused -1 );

		/* start marker */
		if( gh->isfirst ){
			mfirst = (int)data->cused;
			DPRINTF( "srmpc_get_data: new marker at %d", mfirst );

		} else if( ! gh->iscont )
			mfirst = -1;

	}

	/* TODO: catch and handle errors during download properly */

	/* TODO: start new file on recint change */
	data->recint = gh->recint;
	if( ! data->recint ){
		_srm_log( conn, "block has no recint" );
		goto clean2;
	}


	srmpc_get_chunk_done( gh );
	gh = NULL;


	/* finalize first + last marker */

	mk->last = data->cused-1;

	if( mfirst >= 0 )
		srm_data_add_marker( data, mfirst,
			data->cused -1 );


	/* data fixup */

	if( fixup ){
		srm_data_t fixed;

		_srm_log( conn, "postprocessing data" );

		if( NULL == ( fixed = srm_data_fixup( data ) ) ){
			_srm_log( conn, "data fixup failed: %s", strerror(errno));
			goto clean2;
		}

		srm_data_free( data );
		data = fixed;
	}

	return data;

clean2:
	if( gh )
		srmpc_get_chunk_done( gh );

clean1:
	srm_data_free(data);
	return NULL;
}

