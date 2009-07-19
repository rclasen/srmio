/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include "srmio.h"
#include "debug.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define SRM_BUFSIZE	1024

#define STX	((char)0x2)
#define ETX	((char)0x3)
#define ACK	((char)0x6)
#define NAK	((char)0x15)

/*
 * I'm a chicken and won't touch any SRM I don't know.
 *
 * This is a good(tm) idea as a misencoded command argument leads to
 * broken settings within the SRM. Seems the SRM's input checking is
 * limited.
 */
static const char *_srmpc_whitelist[] = {
	"\x6b\x09",	/* fw 6b.09 - uses stxetx */
	"\x43\x09",	/* fw 43.09 - no stxetx, fw was upgraded 04/2009 */
	NULL,
};

srmpc_conn_t foo;

static int _srmpc_msg_decode( 
	char *out, size_t olen,
	const char *in, size_t ilen );

static int _srmpc_msg_send( srmpc_conn_t conn, char cmd, const char *arg, size_t alen );



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
static int _srmpc_write( srmpc_conn_t conn, const char *buf, size_t blen )
{
	DUMPHEX( "_srmpc_write", buf, blen );

	/* TODO: re-init? ... */

	if( tcflush( conn->fd, TCIOFLUSH ) )
		return -1;

	return write( conn->fd, buf, blen );
}

/*
 * read specified number of bytes
 * automagically take care of pending IOFLUSH
 *
 * returns number of chars read
 * on error errno is set and returns -1
 */
static int _srmpc_read( srmpc_conn_t conn, char *buf, size_t want )
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
 * allocate internal data structures,
 * open serial device
 * set serial device properties
 * 
 * communication uses 9600 8n1, no handshake
 *
 * on error errno is set and returns NULL
 */
srmpc_conn_t srmpc_open( const char *fname, int force )
{
	srmpc_conn_t	conn;
	struct termios	ios;
	char		buf[20];
	char		ver[2];
	int		ret;

	DPRINTF( "srmpc_open %s", fname );

	if( NULL == (conn = malloc(sizeof(struct _srmpc_conn_t))))
		return NULL;
	conn->stxetx = 1;

	/* TODO: uucp style lockfils */

	if( 0 > (conn->fd = open( fname, O_RDWR | O_NOCTTY )))
		goto clean2;

	/* get/set serial comm parameter */
	if( tcgetattr( conn->fd, &conn->oldios ) )
		goto clean3;

	memset(&ios, 0, sizeof(struct termios));
	ios.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
	ios.c_cc[VMIN] = 0;
	ios.c_cc[VTIME] = 10;

	if( tcflush( conn->fd, TCIOFLUSH ) )
		goto clean3;

	if( tcsetattr( conn->fd, TCSANOW, &ios ) )
		goto clean3;

	tcsendbreak( conn->fd, 0 );

	/* send opening 'P' to verify communication works */
	ret = _srmpc_msg_send( conn, 'P', NULL, 0 );
	if( ret < 0 )
		goto clean3;

	ret = _srmpc_read( conn, buf, 20 );
	if( ret < 0 )
		goto clean3;

	DUMPHEX( "srmpc_open got ", buf, ret );

	/* autodetect communcitation type */
	if( *buf == STX ){
		if( ret < 7 ){
			errno = EPROTO;
			return NULL;
		}

		_srmpc_msg_decode( ver, 2, &buf[2], 4 );

	} else {
		if( ret < 3 ){
			errno = EPROTO;
			return NULL;
		}

		conn->stxetx = 0;
		DPRINTF( "srmpc_open: disabling stx/etx" );

		memcpy( ver, &buf[1], 2 );
	}

	/* TODO: retry sending, try other speeds + parity + x787878 */

	/* verify it's a known/supported PCV */
	if( ! force ){
		int known = 0;
		const char **white;

		DUMPHEX( "srmpc_open whitelist", ver, 2 );
		for( white = _srmpc_whitelist; *white; ++white ){
			if( 0 == memcmp( ver, *white, 2 )){
				++known;
				break;
			}
		}
		if( ! known ){
			errno = ENOTSUP;
			goto clean3;
		}
	}

	return conn;
	
clean3:
	close(conn->fd);

clean2:
	free(conn);
	return NULL;
}

/*
 * close serial device
 * and free memory
 */
void srmpc_close( srmpc_conn_t conn )
{
	DPRINTF( "srmpc_close" );
	if( ! conn )
		return;

	tcflush( conn->fd, TCIOFLUSH );
	tcsetattr( conn->fd, TCSANOW, &conn->oldios );
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
 * (ACK/NAK) periodically.
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
	char *out, size_t olen,
	const char *in, size_t ilen ) 
{
	size_t i;
	size_t o=0;

	if( olen < 2 * ilen ){
		DPRINTF( "_srmpc_msg_encode: dst buffer too small %d/%d", ilen, olen);
		errno = ERANGE;
		return -1;
	}

	for( i=0; i < ilen; ++i ){
		out[o++] = (((unsigned char)(in[i]) & 0xf0 ) >> 4) | 0x30;
		out[o++] = ((unsigned char)(in[i]) & 0x0f ) | 0x30;
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
	char *out, size_t olen,
	const char *in, size_t ilen ) 
{
	int o=0;

	if( ilen % 2 ){
		DPRINTF( "_srmpc_msg_decode: uneven input size: %d", ilen );
		errno = EINVAL;
		return -1;
	}

	if( olen * 2 < ilen ){
		DPRINTF( "_srmpc_msg_decode: dst buffer too small %d/%d", ilen, olen);
		errno = ERANGE;
		return -1;
	}

	while( ilen > 1 ){
		out[o] = (((unsigned char)(in[0]) - 0x30) << 4);
		out[o] |= ((unsigned char)(in[1]) - 0x30);
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
static int _srmpc_msg_send( srmpc_conn_t conn, char cmd, const char *arg, size_t alen )
{
	char buf[SRM_BUFSIZE];
	int len = 0;
	int ret;

	DPRINTF( "_srmpc_msg_send %c ...", cmd );
	DUMPHEX( "_srmpc_msg_send", arg, alen );

	/* sledgehammer aproach to prevent overflows: */
	if( 2 * alen +3 > SRM_BUFSIZE ){
		errno = ERANGE;
		return -1;
	}

	/* build command */
	if( conn->stxetx ){
		buf[len++] = STX;
		buf[len++] = cmd;

		ret = _srmpc_msg_encode( &buf[len], SRM_BUFSIZE-2, arg, alen );
		if( ret < 0 )
			return -1;

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
		return -1;

	} else if( ret < len ){
		errno = ECOMM;
		return -1;
	}

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
static int _srmpc_msg_recv( srmpc_conn_t conn, char *rbuf, size_t rsize, size_t want )
{
	char	buf[SRM_BUFSIZE];
	size_t	rlen = 0;
	int	ret;

	if( rbuf && want > rsize ){
		DPRINTF( "_srmpc_msg_recv rbuf too small want %d rsize %d", want, rsize);
		errno = ERANGE;
		return -1;
	}

	if( conn->stxetx )
		/* stx, etc, encoding */
		want = 2*want +2;

 	/* cmd char */
	++want;

	DPRINTF( "_srmpc_msg_recv will read %d ", want );

	if( want >= SRM_BUFSIZE ){
		DPRINTF( "_srmpc_msg_recv tmp buf too small for rsize=%d", rsize );
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
			DUMPHEX( "_srmpc_msg_recv", buf, rlen );
			return -1; /* read failed */

		} 
		
		/* timeout */
		if( ret < 1 ){
			DPRINTF( "_srmpc_msg_recv timeout" );
			break;
		}

		++rlen;

		/* avoid running into timeout */
		if( conn->stxetx && buf[rlen-1] == ETX ){
			DPRINTF( "_srmpc_msg_recv ETX" );
			break;
		}

	}
	DPRINTF( "_srmpc_msg_recv got %u chars", rlen );
	DUMPHEX( "_srmpc_msg_recv", buf, rlen );
	
	if( conn->stxetx ){
		if( rlen < 3 ){
			errno = EBADMSG;
			return -1;
		}

		if( buf[0] != STX ){
			DPRINTF( "_srmpc_msg_recv STX is missing" );
			errno = EPROTO;
			return -1;
		}

		if( buf[rlen-1] != ETX ){
			DPRINTF( "_srmpc_msg_recv ETX is missing" );
			errno = EPROTO;
			return -1;
		}
		rlen -= 3; /* stx, cmd, etx */

		if( rbuf )
			rlen = _srmpc_msg_decode(rbuf, rsize, &buf[2], rlen );
		else
			rlen /= 2;

	} else {
		if( rlen < 1 ){
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
 * process one complete command (send, receive), 
 * retry if necesssary
 *
 * returns 0 on success
 * on error errno is set and returns -1
 */
static int _srmpc_msg( srmpc_conn_t conn, char cmd, 
	const char *arg,
	size_t alen, 
	char *buf,
	size_t blen,
	size_t want )
{
	/* TODO: retry */

	if( _srmpc_msg_send( conn, cmd, arg, alen ) )
		return -1;

	return _srmpc_msg_recv( conn, buf, blen, want );
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
 * setting:	athlete name
 * command:	N
 * argument:	cccccc\x20\x20 - it's 6 chars, zero-padded
 * 
 * returns malloc()ed string on succuess
 * on error errno is set and returns -1
 */
char *srmpc_get_athlete( srmpc_conn_t conn )
{
	char buf[20];
	int ret;

	ret = _srmpc_msg( conn, 'N', "\x0", 1, buf, 20, 8 );
	if( ret < 0 )
		return NULL;
	if( ret < 6 ){
		errno = EPROTO;
		return NULL;
	}

	buf[6] = 0;
	DPRINTF( "srmpc_get_athlete got=%s", buf );
	return strdup( buf );
}

/*
 * setting:	current time
 * command:	M
 * argument:	0xdd 0xmm 0xyy 0xHH 0xMM 0xSS
 * 
 * returns 0 on succuess
 * on error errno is set and returns -1
 */
int srmpc_get_time( srmpc_conn_t conn, struct tm *timep )
{
	char buf[20];
	int ret;

	ret = _srmpc_msg( conn, 'M', "\x0", 1, buf, 20, 6 );
	if( ret < 0 )
		return -1;
	if( ret < 6 ){
		errno = EPROTO;
		return -1;
	}

	timep->tm_mday = TIMEDEC( (unsigned char)(buf[0]) );
	timep->tm_mon = TIMEDEC( (unsigned char)(buf[1]) ) -1;
	timep->tm_year = TIMEDEC( (unsigned char)(buf[2]) ) + 100;
	timep->tm_hour = TIMEDEC( (unsigned char)(buf[3]) );
	timep->tm_min = TIMEDEC( (unsigned char)(buf[4]) );
	timep->tm_sec = TIMEDEC( (unsigned char)(buf[5]) );
	timep->tm_isdst = -1;

	DPRINTF( "srmpc_get_time time=%s", asctime( timep ) );

	return 0;
}

int srmpc_set_time( srmpc_conn_t conn, struct tm *timep )
{
	char buf[6];

	buf[0] = TIMEENC( timep->tm_mday );
	buf[1] = TIMEENC( timep->tm_mon +1 );
	buf[2] = TIMEENC( timep->tm_year -100 );
	buf[3] = TIMEENC( timep->tm_hour );
	buf[4] = TIMEENC( timep->tm_min );
	buf[5] = TIMEENC( timep->tm_sec );

	if( 0 > _srmpc_msg( conn, 'M', buf, 6, NULL, 0, 0 ))
		return -1;
		
	sleep(1); /* need to wait or next cmd fails */
	DPRINTF( "srmpc_set_time set %s", asctime(timep) );
	return 0;
}

/*
 * setting:	wheel circumference
 * command:	G
 * argument:	0x0000 - uint16
 * 
 * returns circumference on succuess
 * on error errno is set and returns -1
 */
int srmpc_get_circum( srmpc_conn_t conn )
{
	char buf[20];
	int ret;
	int circum;

	ret = _srmpc_msg( conn, 'G', "\x0\x0", 2, buf, 20, 2 );
	if( ret < 0 )
		return -1;
	if( ret < 2 ){
		errno = EPROTO;
		return -1;
	}

	circum = ( (unsigned char)(buf[0])<<8) 
		| ( (unsigned char)(buf[1]) );
	DPRINTF( "srmpc_get_circum circum=%d", circum );
	return circum;
}

/*
 * setting:	slope
 * command:	E
 * argument:	0x0000 - uint16
 * 
 * returns slope  eg. 17.4
 * on error errno is set and returns < 0
 */
double srmpc_get_slope( srmpc_conn_t conn )
{
	char buf[20];
	int ret;
	double slope;

	ret = _srmpc_msg( conn, 'E', "\x0\x0", 2, buf, 20, 2 );
	if( ret < 0 )
		return -1;
	if( ret < 2 ){
		errno = EPROTO;
		return -1;
	}

	slope = (double)(( (unsigned char)(buf[0])<<8) 
		| ( (unsigned char)(buf[1]) ) ) / 10;
	DPRINTF( "srmpc_get_slope slope=%.1lf", slope );
	return slope;
}


/*
 * setting:	zero offset
 * command:	F
 * argument:	0x0000 - uint16
 * 
 * returns circumference on succuess
 * on error errno is set and returns -1
 */
int srmpc_get_zeropos( srmpc_conn_t conn )
{
	char buf[20];
	int ret;
	int zeropos;

	ret = _srmpc_msg( conn, 'F', "\x0\x0", 2, buf, 20, 2 );
	if( ret < 0 )
		return -1;
	if( ret < 2 ){
		errno = EPROTO;
		return -1;
	}

	zeropos = ( (unsigned char)(buf[0])<<8) 
		| ( (unsigned char)(buf[1]) );
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
 *
 * returns recint * 10 -> 1sec becomes 10
 * on error errno is set and returns -1
 */
int srmpc_get_recint( srmpc_conn_t conn )
{
	char buf[10];
	int ret;
	int recint;

	ret = _srmpc_msg( conn, 'R', "\x0", 1, buf, 10, 1 );
	if( ret < 0 )
		return -1;
	if( ret < 1 ){
		errno = EPROTO;
		return -1;
	}
	
	recint = ( *buf & 0x80 ) 
		? (unsigned char)(*buf) & 0x80 
		: *buf * 10;

	DPRINTF( "srmpc_get_recint raw=0x%02x recint=%d", *buf, recint);
	return recint;
}

int srmpc_set_recint( srmpc_conn_t conn, int recint )
{
	char raw;

	if( recint <= 0 ){
		errno = ENOTSUP;
		return -1;
	}

	/* 0.1 .. 0.9 sec? */
	if( recint < 10 ){
		raw = 0x80 | recint;
	
	/* 1 .. 15 sec? */
	} else {
		if( recint > 150 || recint % 10 ){
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
 * get/parse data blocks / callback
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
 */


/*
 * parse single 64byte data block, invoke callback for each chunk
 *
 * returns -1 on failure, 0 on success
 */
static int _srmpc_parse_block( char *buf, srmpc_chunk_callback_t cbfunc, void *data )
{
	time_t bstart;
	struct tm btm;
	unsigned int dist;
	int temp;	
	int num;
	struct _srm_chunk_t chunk; /* TODO: hack? should use srm_chunk_new()? */
	unsigned int recint;

	DUMPHEX( "_srmpc_parse_block", buf, 64 );

	if( ! cbfunc )
		return 0;

	/* get current year */
	time( &bstart );
	localtime_r( &bstart, &btm );

	/* parse timestamp */
	btm.tm_isdst = -1;
	btm.tm_mday = TIMEDEC( (unsigned char)(buf[0]) & 0x3f );
	if( btm.tm_mon < (btm.tm_mon = TIMEDEC( (unsigned char)(buf[1]) & 0x1f ) -1 ))
		-- btm.tm_year;
	btm.tm_hour = TIMEDEC( (unsigned char)(buf[2] & 0x3f ) );
	btm.tm_min = TIMEDEC( (unsigned char)(buf[3]) );
	btm.tm_sec = TIMEDEC( (unsigned char)(buf[4]) );
	bstart = mktime( &btm );
	/* TODO: with recint <1sec the timestamp's resolution isn't
	 * sufficient. Try to guess tsec from previous block */

	dist = ( (unsigned char)(buf[5]) << 16 
		| (unsigned char)(buf[6]) << 8
		| (unsigned char)(buf[7]) ) / 3.9;

 	temp = buf[8];
	recint = ( ((unsigned char)(buf[1]) & 0xe0) >> 5)
		| ( ((unsigned char)(buf[0]) & 0x40) >> 3);
	if( ! (buf[2] & 0x40) )
		recint *= 10;

	DPRINTF( "_srmpc_parse_block mon=%d day=%d hour=%d min=%d sec=%d "
		"dist=%d temp=%d recint=%d na0=%x na2=%x", 
		btm.tm_mon,
		btm.tm_mday,
		btm.tm_hour,
		btm.tm_min,
		btm.tm_sec,
		dist,
		temp,
		recint,
		(int)( ( (unsigned char)(buf[0]) & 0xc0) >> 6 ),
		(int)( ( (unsigned char)(buf[2]) & 0x00) >> 7 ) );

	for( num=0; num < 11; ++num ){
		char * cbuf = &buf[9 + 5*num];
		int mfirst = ((unsigned char)(cbuf[0]) & 0x40);
		int mcont = ((unsigned char)(cbuf[0]) & 0x80);

		DUMPHEX( "_srmpc_parse_block chunk", cbuf, 5 );

		/* TODO: is it ok to stop the block on an all-zero chunk? */
		if( 0 == memcmp( cbuf, "\0\0\0\0\0", 5 )){
			DPRINTF( "_srmpc_parse_block: chunk %d empty, ending block", num );
			return 0;
		}

		chunk.time = bstart + num * recint / 10;
		chunk.tsec = num * recint % 10;
		chunk.temp = temp;
		chunk.pwr = ( ( (unsigned char)(cbuf[0]) & 0x0f) << 8 ) 
			| (unsigned char)(cbuf[1]);
		chunk.speed =  (double)0.2 * ( 
			( ( (unsigned char)(cbuf[0]) & 0x30) << 4) 
			| (unsigned char)(cbuf[2]) );
		chunk.cad = (unsigned char)(cbuf[3]);
		chunk.hr = (unsigned char)(cbuf[4]);
		chunk.ele = 0;


		/* TODO: verify data when display is non-metric */
		/* TODO: verify temperature < 0°C */

		if( (*cbfunc)( &chunk, recint, dist, 
			mfirst,
			mcont,
			data ) )

			return -1;

	}

	return 0;
}

/*
 * get all blocks/chunks off the SRM, parse it and pass the decoded chunks
 * to the callback function.
 *
 * on error errno is set and returns -1
 */
int srmpc_get_chunks( 
	srmpc_conn_t conn, 
	int getall,
	srmpc_chunk_callback_t cbfunc, 
	void *data )
{
	char buf[SRM_BUFSIZE];
	int ret;
	int blocks;
	int retries = 0;
	char cmd = (getall ? 'y' : 'A');


	if( _srmpc_msg_send( conn, cmd, NULL, 0 ) )
		return -1;

	/* get header + number of blocks to read */
	ret = _srmpc_read( conn, buf, conn->stxetx ? 4 : 3 );
	DPRINTF( "srmpc_get_chunks read %d chars", ret ); 
	if( ret < 0 ){
		return -1;
	} 
	DUMPHEX( "srmpc_get_chunks read response", buf, ret ); 
	if( conn->stxetx ){
		if( ret < 4 ){
			errno = EPROTO;
			return -1;
		} else if( buf[0] != STX || buf[1] != cmd ){
			errno = EPROTO;
			return -1;
		}

		blocks = ( (unsigned char)(buf[2]) << 8) |  (unsigned char)(buf[3]);
	
	} else {
		if( ret < 2 ){
			errno = EPROTO;
			return -1;

		} else if( ret > 2 ){
			blocks = ( (unsigned char)(buf[1]) << 8) 
				|  (unsigned char)(buf[2]);

		} else {
			blocks = (unsigned char)(buf[1]);

		}

	}
	DPRINTF( "srmpc_get_chunks expecting %d blocks (max %d chunks)",
		blocks, 11*blocks  );

	/* TODO: pass number of blocks + current block to callback for
	 * progress indication */

	/* read 64byte blocks, each with header + 11 chunks */
	while( blocks > 0 ){
		char	resp = ACK;

		ret = _srmpc_read( conn, buf, 64 );
		DPRINTF( "srmpc_get_chunks: got %d chars", ret );

		if( ret < 0 ){
			return -1;

		} else if( ret < 1 ){
			errno = ETIMEDOUT;
			return -1;

		} else if( ret == 1 && buf[0] == ETX ){
			DPRINTF( "srmpc_get_chunks: got ETX" );
			break;
			
		} else if( ret < 64 ){
			if( retries > 2 ){
				errno = ETIMEDOUT;
				return -1;
			}

			DPRINTF( "srmpc_get_chunks: requesting retransmit" );
			sleep(1);
			resp = NAK;
			retries++;

		} else {
			retries = 0;
			if( 0 > _srmpc_parse_block( buf, cbfunc, data ))
				return -1;

			--blocks;
		}

		/* ACK / NAK this block */
		if( 0 > _srmpc_write( conn, &resp, 1 ) )
			return -1;

		DPRINTF( "srmpc_get_chunks: %d blocks left", blocks );
	}
	
	/* read (and ignore) trailing ETX */
	if( blocks == 0 && conn->stxetx ){
		if( 1 == _srmpc_read( conn, buf, 1 ) )
			DPRINTF( "srmpc_get_chunks final ETX: %02x",
				(unsigned char)*buf );
	}

	sleep(1); /* need to wait or next cmd fails */
	return 0;
}

/*
 * setting:	clear recorded data
 * command:	B and T
 * argument:	none
 * 
 * returns true on success
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
 * get/parse data blocks / list
 *
 * won't be useful beyond serving as a example
 *
 ************************************************************/

static int _srmpc_chunk_data_cb( 
	srm_chunk_t chunk, 
	int recint,
	unsigned int dist,
	int mfirst,
	int mcont,
	void *data )
{
	srm_data_t clist = (srm_data_t)data;

	(void)dist; /* ignore; */

	/* TODO: start new file on recint change */
	if( ! clist->recint )
		clist->recint = recint;

	/* TODO: is it ok to use the current recint or do we have to
	 * deduce it from two blocks' timestamps? */

	/* TODO: fill small gaps (<= 2sec) at block boundaries with averaged data? */

	if( 0 > srm_data_add_chunk( clist, chunk ) )
		return -1;

	/* finish previous marker */
	if( clist->mfirst >= 0 && ( ! mcont || mfirst ) )
		srm_data_add_marker( clist, clist->mfirst, clist->cused -1 );

	if( mfirst ){
		clist->mfirst = (int)clist->cused;
		DPRINTF( "_srmpc_chunk_data_cb: new marker at %d",
			clist->mfirst );

	} else if( ! mcont )
		clist->mfirst = -1;
	

	return 0;
}

srm_data_t srmpc_get_data( srmpc_conn_t conn, int getall )
{
	srm_data_t data;
	srm_marker_t mk;

	if( NULL == (data = srm_data_new()))
		return NULL;

	if( 0 > (data->slope = srmpc_get_slope( conn ) ))
		goto clean1;

	if( 0 > (data->zeropos = srmpc_get_zeropos( conn ) ))
		goto clean1;

	if( 0 > (data->circum = srmpc_get_circum( conn ) ))
		goto clean1;

	if( NULL == (mk = srm_marker_new() ))
		goto clean1;

	if( NULL == (mk->notes = srmpc_get_athlete( conn ) ))
		goto clean2;

	if( 0 > srm_data_add_markerp( data, mk ))
		goto clean2;

	if( 0 > srmpc_get_chunks(conn, getall, _srmpc_chunk_data_cb, data ) )
		goto clean2;

	data->marker[0]->last = data->cused-1;

	if( data->mfirst >= 0 )
		srm_data_add_marker( data, data->mfirst, data->cused );

	return data;

clean2:
	srm_marker_free(mk);

clean1:
	free(data);
	return NULL;
}

