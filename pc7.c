/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

/*
 * based on Hiroyuki OYAMA's work. Thanks a lot!!!
 *
 * https://github.com/oyama/libsrmpc7
 */

#include "pc.h"

#define PC7_MAGIC		0xa4b0
#define PC7_CMD_BUFSIZE		512
#define PC7_PKTHEAD_SIZE	6
#define PC7_PKT_CHUNKS		16
#define PC7_CHUNK_SIZE		16

struct _srmio_pc7_packet_t {
	uint16_t	magic;
	uint16_t	cmd;
	unsigned char	data[PC7_CMD_BUFSIZE];
	size_t		datalen;
	uint8_t		sum;
};


struct _srmio_pc7_t {
	/* xfer block */
	size_t			block_num;
	srmio_time_t		block_recint;
	srmio_time_t		block_time;
	size_t			chunk_cnt;

	/* xfer pkt */
	struct _srmio_pc7_packet_t pkt;
	size_t			pkt_num;

	/* xfer chunk */
	size_t			chunk_num;
	bool			is_intervall;
};
typedef struct _srmio_pc7_t *srmio_pc7_t;

#define SELF(x)	((srmio_pc7_t)x->child)

/************************************************************
 *
 * low level packet handling
 */

/*
 * communication is binary only. Numbers are in big endian byte order.
 *
 * each "packet" starts with a magic and has a 8bit checksum.
 *
 * id	offset	len	type	comment
 * 0	0	2	uint16	"magic" - 0xa4b0
 * 1	2	2	uint16	length for rest of packet -> min=3
 * 2	4	2	uint16	command
 * 3	6	n	varies	command arguments?
 * 4	6+n	1	uint8	checksum
 */

static uint8_t _srmio_pc7_checksum( const unsigned char *buf, size_t len )
{
	uint8_t sum = 0;
	size_t i;

	assert( buf );

	for( i = 0; i < len; ++i ){
		sum ^= buf[i];
	}

	return sum;
}


static bool _srmio_pc7_msg_send( srmio_pc_t conn,
	struct _srmio_pc7_packet_t *pkt )
{
	unsigned char buf[PC7_CMD_BUFSIZE];
	int ret;
	size_t buflen;

	assert( conn );
	assert( pkt );

	DPRINTF("cmd 0x%04x", pkt->cmd);
	buflen = PC7_PKTHEAD_SIZE + pkt->datalen +1;

	if( ! buf_set_buint16( buf, 0, PC7_MAGIC ) )
		return false;
	if( ! buf_set_buint16( buf, 2, pkt->datalen+3 ) )
		return false;
	if( ! buf_set_buint16( buf, 4, pkt->cmd ) )
		return false;

	memcpy( &buf[PC7_PKTHEAD_SIZE], pkt->data, pkt->datalen );

	if( ! buf_set_uint8( buf, buflen -1, _srmio_pc7_checksum( buf, buflen -1 ) ) )
		return false;

	DUMPHEX("sending", buf, buflen );
	ret = srmio_io_write( conn->io, buf, buflen );
	if( ret < 0 )
		return false;

	if( (size_t)ret != buflen ){
		errno = EIO;
		return false;
	}

	return true;
}

static bool _srmio_pc7_msg_recv( srmio_pc_t conn,
	struct _srmio_pc7_packet_t *pkt )
{
	unsigned char buf[PC7_CMD_BUFSIZE];
	uint8_t sum;
	int ret;
	size_t buflen = PC7_PKTHEAD_SIZE;

	assert( conn );
	assert( pkt );

	ret = srmio_io_read( conn->io, buf, buflen );
	if( ret < 0 )
		return false;

#ifdef DEBUG_PKT2
	DUMPHEX( "pkt header", buf, ret );
#endif

	if( (size_t)ret < buflen ){
		errno = EIO;
		return false;
	}

	pkt->magic = buf_get_buint16( buf, 0 );
	pkt->datalen = buf_get_buint16( buf, 2 ) - 3;
	pkt->cmd = buf_get_buint16( buf, 4 );
	buflen += pkt->datalen + 1;

#ifdef DEBUG_PKT2
	DPRINTF( "get pkt body: %d bytes", pkt->datalen +1 );
#endif
	if( pkt->datalen +1 > PC7_CMD_BUFSIZE ){
		errno = EOVERFLOW;
		return false;
	}

	ret = srmio_io_read( conn->io, &buf[PC7_PKTHEAD_SIZE], pkt->datalen +1 );
	if( ret < 0 )
		return false;

	DUMPHEX( "got pkt body", &buf[PC7_PKTHEAD_SIZE], ret );

	if( (size_t)ret < pkt->datalen +1 ){
		errno = EIO;
		return false;
	}

	pkt->sum = buf_get_uint8( buf, buflen -1 );
	sum = _srmio_pc7_checksum( buf, buflen -1 );

	DPRINTF( "magic=0x%04x datalen=%u cmd=0x%04x sum=0x%02x/%02x",
		pkt->magic, pkt->datalen, pkt->cmd, pkt->sum, sum );

	if( sum != pkt-> sum ){
		errno = EIO;
		return false;
	}

	memcpy( pkt->data, &buf[PC7_PKTHEAD_SIZE], pkt->datalen );

	return true;
}

static bool _srmio_pc7_msg( srmio_pc_t conn,
	struct _srmio_pc7_packet_t *first,
	struct _srmio_pc7_packet_t *retry,
	struct _srmio_pc7_packet_t *recv )
{
	int retries;
	struct _srmio_pc7_packet_t *send = first;

	assert( send );
	assert( recv );

	DPRINTF("cmd 0x%04x", send->cmd );

	for( retries = 0; retries < 3; ++retries ){
		if( retries ){
			if( retry ) send = retry;
			DPRINTF("cmd 0x%04x/0x%04x retry %d",
				first->cmd, send->cmd, retries );
			srmio_io_send_break( conn->io );
			sleep(1);
			srmio_io_flush( conn->io );
		}

		if( ! _srmio_pc7_msg_send( conn, send ) )
			return false;

		if( ! _srmio_pc7_msg_recv( conn, recv ) )
			continue;

#if REALLY_PARANOID
		if( recv->magic != PC7_MAGIC ){
			errno = EPROTO;
			continue;
		}

		if( first->cmd != recv->cmd ){
			errno = EPROTO;
			continue;
		}
#endif

		return true;
	}

	return false;
}

static bool _srmio_pc7_open( srmio_pc_t conn )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0101, // helo
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->io );

	DPRINTF( "" );

	srmio_io_set_baudrate( conn->io, srmio_io_baud_38400 );
	srmio_io_set_parity( conn->io, srmio_io_parity_none );
	srmio_io_set_flow( conn->io, srmio_io_flow_none );

	if( ! srmio_io_update( conn->io ) )
		return false;

	srmio_io_send_break( conn->io );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv ) )
		return false;

	if( recv.datalen != 2 ){
		errno = EPROTO;
		return false;
	}

	conn->firmware = buf_get_buint16( recv.data, 0 );
	DPRINTF( "firmware 0x%04x", conn->firmware );

	return true;
}

static bool _srmio_pc7_close( srmio_pc_t conn )
{
	assert( conn );

	return true;
}

/************************************************************
 *
 * set/get individual parameter
 */

static bool _srmio_pc7_cmd_get_athlete( srmio_pc_t conn, char **name )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0205, // athlete
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );
	assert( name );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv ) )
		return false;

	if( recv.datalen != 20 ){
		errno = EPROTO;
		return false;
	}


	// TODO: athlete encoding
	if( NULL == ( *name = buf_get_string( recv.data, 0, recv.datalen ) ))
		return false;

	DPRINTF("athlete: %s", *name );

	return true;
}

static bool get_time( const unsigned char *buf, size_t pos, struct tm *dst )
{
	assert( buf );
	assert( dst );

	// 2011-07-16 20:19:xx
	// 0x10/  0x07/  0x07/  0xdb/  0x14/  0x11/  0x28/(
	// 16     7      7      219    20     17     40
	// mday   mon    year1  year0  hour   min    sec
	dst->tm_mday = buf[pos];
	dst->tm_mon = buf[pos+1] -1;
	dst->tm_year = buf_get_buint16( buf, pos+2 ) -1900;
	dst->tm_hour = buf[pos+4];
	dst->tm_min = buf[pos+5];
	dst->tm_sec = buf[pos+6];
	dst->tm_isdst = -1;

	return true;
}

static bool set_time( unsigned char *buf, size_t pos, struct tm *src )
{
	assert( buf );
	assert( src );

	buf[pos] = src->tm_mday;
	buf[pos+1] = src->tm_mon +1;
	if( ! buf_set_buint16( buf, pos+2, src->tm_year +1900 ) )
		return false;
	buf[pos+4] = src->tm_hour;
	buf[pos+5] = src->tm_min;
	buf[pos+6] = src->tm_sec;

	return true;
}

bool srmio_pc7_cmd_get_time( srmio_pc_t conn, struct tm *timep )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x020e, // time
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;
	bool result;

	assert( conn );
	assert( conn->is_open );
	assert( timep );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv ) )
		return false;

	if( recv.datalen != 7 ){
		errno = EPROTO;
		return false;
	}

	result = get_time( recv.data, 0, timep );

	DPRINTF( "time: %s", asctime(timep) );
	return result;
}

static bool _srmio_pc7_cmd_set_time( srmio_pc_t conn, struct tm *timep )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x020e, // time
		.datalen = 7,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );
	assert( timep );

	DPRINTF( "time: %s", asctime(timep) );
	if( ! set_time( send.data, 0, timep ) )
		return false;

	return _srmio_pc7_msg( conn, &send, NULL, &recv );
}

bool srmio_pc7_cmd_get_circum( srmio_pc_t conn, unsigned *circum )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0203, // circum
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );
	assert( circum );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv ) )
		return false;

	if( recv.datalen != 2 ){
		errno = EPROTO;
		return false;
	}

	*circum = buf_get_buint16( recv.data, 0 );
	DPRINTF( "circum: %u", *circum );

	return true;
}


bool srmio_pc7_cmd_get_slope( srmio_pc_t conn, double *slope )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0201, // slope
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );
	assert( slope );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv ) )
		return false;

	if( recv.datalen != 2 ){
		errno = EPROTO;
		return false;
	}

	*slope = buf_get_buint16( recv.data, 0 ) / 10;
	DPRINTF( "slope: %.1lf", *slope );

	return true;
}

bool srmio_pc7_cmd_get_zeropos( srmio_pc_t conn, unsigned *zeropos )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0202, // zeropos
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );
	assert( zeropos );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv ) )
		return false;

	if( recv.datalen != 4 ){
		errno = EPROTO;
		return false;
	}

	*zeropos = buf_get_buint16( recv.data, 0 );
	// TODO: last 2 bytes in zeropos msg are unknown. elevation? slope?
	DPRINTF( "zeropos: %u", *zeropos );

	return true;
}

bool srmio_pc7_cmd_get_recint( srmio_pc_t conn, srmio_time_t *recint )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0206, // recint
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );
	assert( recint );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv ) )
		return false;

	if( recv.datalen != 2 ){
		errno = EPROTO;
		return false;
	}

	*recint = 0.01 * buf_get_buint16( recv.data, 0 );
	DPRINTF( "recint: %.1lf", 0.1 * *recint );

	return true;
}


static bool _srmio_pc7_cmd_set_recint( srmio_pc_t conn, srmio_time_t recint )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0206, // recint
		.datalen = 2,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );

	DPRINTF( "recint: %.1lf", 0.1 * recint );
	switch( recint ){
	  case 5:
	  case 10:
	  case 50:
		/* ok, do nothing */
		break;

	  default:
		errno = EINVAL;
		return false;
	}

	if( ! buf_set_buint16( recv.data, 0, recint * 100 ) )
		return false;

	return _srmio_pc7_msg( conn, &send, NULL, &recv );
}

/*
 * send cmd 0x0407
 * rcv data 0x01 0x01 (after ~ 0.5sec)
 *               ... - one per sec
 * rcv data 0x01 0xNN (example: 0x22)
 * rcv data 0x02 0xNN (example: 0x22)
 * done
 *
 * only responds, if there is data to delete!!
 */

static bool _srmio_pc7_cmd_clear( srmio_pc_t conn )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0407, // clear
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv ) )
		return false;

	if( recv.datalen != 0 ){
		errno = EPROTO;
		return false;
	}

	while( *recv.data < 2 ){
		if( ! _srmio_pc7_msg_recv( conn, &recv ) )
			// not really true, but who cares...
			return true;
	}

	return true;
}

/*
 * TODO: other_commands
		.cmd = 0x0106, // online_status
		.cmd = 0x010c, // TODO
 */

/************************************************************
 *
 * data download
 */

/*
 * pc7 download happens in 3 stages:
 * - first get number of "blocks"
 * - while( get "block" header )
 *   - get "pakets" - each with 16 chunks
 *
 * pc7 seems to end/start a new block when it doesn't record. There are no
 * gaps within a block.
 *
 * pc7 remembers the "current" block/packet. You can only ask for the next
 * or a retransmission of the current one.
 *
 * you don't have to fetch (all) blocks/packets. Getting the next block
 * aborts the current block. Getting the block count, again, aborts the
 * download and next block request will return the first block.
 *
 * timing seems to be quite uncritical.
 */

static bool _srmio_pc7_xfer_block_progress(
	srmio_pc_t conn, size_t *block_done )
{
	assert( conn );

	if( block_done )
		*block_done = SELF(conn)->chunk_num;

	return true;
}

/*
 * get block count and re-/start download
 */
static bool _srmio_pc7_xfer_start( srmio_pc_t conn )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0401, // xfer_count
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	DPRINTF("");

	// TODO: xfer deleted data?

	assert( conn );
	assert( conn->xfer_state == srmio_pc_xfer_state_new );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv ) )
		goto fail;

	if( recv.datalen != 2 ){
		errno = EPROTO;
		goto fail;
	}

	conn->xfer_state = srmio_pc_xfer_state_running;
	SELF(conn)->block_num = 0;

	conn->block_cnt = buf_get_buint16( recv.data, 0 );
	DPRINTF("blocks: %d", conn->block_cnt);

	return true;

fail:
	conn->xfer_state = srmio_pc_xfer_state_failed;
	return false;
}

/*
 * get next block header from pc7
 *
 * block response data packing:
 *
 * id	offset	len	type	comment
 * 0	0	2	uint16	block number 1..max
 * 1	2	7	dtime	date/time - see get/set_time()
 * 2	9	2	uint16	recint * 1000
 * 3	11	20	string	athlete
 * 4	31	2	uint16	slope * 10
 * 5	33	2	uint16	zeropos
 *  	35	4		TODO: unknown
 * 6	39	2	uint16	circum
 * 7	41	2	uint16	chunk count
 * 8	43	5		TODO: unknown
 */

/*
 * magic lengt comnd
 * a4 b0 00 32 04 02

 * pktno date_________________ recin athlete________
 * 00 01 0f 0a 07 d9 07 04  0b 03 e8 43 6f 6e 6e 6f
 *_______________________________________athlete slope_
 * 72 20 53 70 65 6e 63 65  72 00 00 00 00 00 00 00
 *___ zrpos             circum ckcnt 
 * d7 02 8f 00 39 44 d7 08  2f 00 41 ae 00 00 aa c6
 */

static bool _srmio_pc7_xfer_block_next( srmio_pc_t conn, srmio_pc_xfer_block_t block )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0402, // xfer_block
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t retry = {
		.cmd = 0x0403,
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;
	size_t block_num;
	struct tm bstarts;
	time_t bstart;

	assert( conn );

	DPRINTF("get block #%d", SELF(conn)->block_num );
	block->athlete = NULL;

	if( conn->xfer_state != srmio_pc_xfer_state_running )
		return false;

	if( SELF(conn)->block_num >= conn->block_cnt ){
		conn->xfer_state = srmio_pc_xfer_state_success;
		return false;
	}

	if( ! _srmio_pc7_msg( conn, &send, &retry, &recv ) )
		goto fail;

	if( recv.datalen == 0 ){
		conn->xfer_state = srmio_pc_xfer_state_success;
		return false;
	}

	if( recv.datalen != 47 ){
		errno = EPROTO;
		goto fail;
	}

	/* decode */

	block_num = buf_get_buint16( recv.data, 0 );
	if( SELF(conn)->block_num +1 != block_num ){
		errno = EPROTO;
		goto fail;
	}

	SELF(conn)->block_num = block_num;
	SELF(conn)->chunk_num = 0;
	SELF(conn)->pkt_num = 0;
	SELF(conn)->is_intervall = false;

	if( ! get_time( recv.data, 2, &bstarts ) )
		goto fail;
	bstart = mktime( &bstarts );
	if( (time_t)-1 == bstart ){
		DPRINTF("bad timestamp, skipping block" );
		SELF(conn)->chunk_cnt = 0;
		bstart = 0;
		SELF(conn)->block_time = 0;
	} else {
		SELF(conn)->chunk_cnt = buf_get_buint16( recv.data, 41 );
		SELF(conn)->block_time = (srmio_time_t)bstart * 10;
	}

	SELF(conn)->block_recint = 0.01 * buf_get_buint16( recv.data, 9 );

	if( block ){
		block->start = SELF(conn)->block_time;
		block->recint = SELF(conn)->block_recint;

		// TODO: athlete encoding
		if( NULL == (block->athlete = buf_get_string( recv.data, 11, 20 ) ))
			goto fail;

		block->slope = (double)buf_get_buint16( recv.data, 31 ) / 10;
		block->zeropos = buf_get_buint16( recv.data, 33 );
		block->circum = buf_get_buint16( recv.data, 39 );
		block->total = SELF(conn)->chunk_cnt;
	}


	DPRINTF("block %d, chunks=%d, time=%.1f, recint=%.1f",
		block_num, SELF(conn)->chunk_cnt,
		0.1 * SELF(conn)->block_time,
		0.1 * SELF(conn)->block_recint);
	return true;

fail:
	if( block->athlete )
		free( block->athlete );

	conn->xfer_state = srmio_pc_xfer_state_failed;
	return false;
}

/*
 * get next data packet from pc7
 *
 * xfer response data packing:
 *
 * id	offset	len	type	comment
 * 0	0	2	uint16	pkt number 1..max
 * 1	2	n*16	chunks	see chunk_next for details
 *
 * TODO: that's 258 bytes for n=16 -> doesn't fit packets with 257 bytes!
 */
static bool _srmio_pc7_xfer_pkt_next( srmio_pc_t conn )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0404, // xfer_pkt
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t retry = {
		.cmd = 0x0405, // xfer_pkt_retry
		.datalen = 0,
	};
	uint16_t pkt_num;

	assert( conn );

	DPRINTF("get pkt #%d",  SELF(conn)->pkt_num +1);

	if( conn->xfer_state != srmio_pc_xfer_state_running )
		return false;

	if( ! _srmio_pc7_msg( conn, &send, &retry, &SELF(conn)->pkt ) )
		goto fail;

	if( SELF(conn)->pkt.datalen == 0 ){
		DPRINTF("empty pkt, premature block end");
		SELF(conn)->chunk_num = SELF(conn)->chunk_cnt;
		return false;
	}

	if( SELF(conn)->pkt.datalen != 257 ){
		errno = EPROTO;
		goto fail;
	}

	pkt_num = buf_get_buint16( SELF(conn)->pkt.data, 0 );
	DPRINTF("got pkt #%d", pkt_num );

	if( SELF(conn)->pkt_num +1 != pkt_num ){
		errno = EPROTO;
		goto fail;
	}
	SELF(conn)->pkt_num = pkt_num;


	return true;

fail:
	conn->xfer_state = srmio_pc_xfer_state_failed;
	return false;
}

/*
 * chunk data packing:
 *
 * id	offset	len	type	comment
 * 0	0	2	uint16	power (W)
 * 1	2	1	uint8	cadence (1/min)
 * 2	3	2	uin16	speed * 10 (km/h)
 * 3	5	1	uint8	heartrate (1/min)
 * 4	6	2	in16	elevation (m)
 * 5	8	2	in16	temperature * 10 (°C)
 * 6	10	1	uint8	interval bool
 * 7	11	5		TODO: unknown
 */

/*
 * complete xfer packet:
 *
 * magic lengt comnd pktnm
 * a4 b0 01 04 04 04 00 01 
 *
 * power cd speed hr eleva  tempe it UNKNOWN_______
 * 00 00 00 00 43 00 00 21  00 ba 00 00 aa aa aa aa
 * 00 00 00 00 4f 00 00 21  00 b9 00 00 aa aa aa aa
 * 00 00 00 00 58 00 00 21  00 b9 00 00 aa aa aa aa
 * 00 00 00 00 59 00 00 21  00 b9 00 00 aa aa aa aa
 * 00 dc 27 00 6c 00 00 20  00 ba 00 00 aa aa aa aa
 * 00 e5 33 00 7c 00 00 20  00 ba 00 00 aa aa aa aa
 * 00 e5 33 00 80 00 00 20  00 b9 00 00 aa aa aa aa
 * 00 cb 37 00 82 00 00 20  00 b9 00 00 aa aa aa aa
 * 00 cb 37 00 7b 00 00 20  00 b9 00 00 aa aa aa aa
 * 00 cb 37 00 6f 00 00 20  00 b9 00 00 aa aa aa aa
 * 00 cb 37 00 62 00 00 20  00 b9 00 00 aa aa aa aa
 * 00 00 00 00 76 00 00 20  00 b9 00 00 aa aa aa aa
 * 00 da 32 00 76 00 00 20  00 b9 00 00 aa aa aa aa
 * 00 bb 33 00 76 00 00 20  00 b9 00 00 aa aa aa aa
 * 00 cd 34 00 7a 00 00 20  00 b9 00 00 aa aa aa aa
 * 00 c8 34 00 7a 00 00 1f  00 b9 00 00 aa aa aa 07
 *                                     checksum ^^
 */
static bool _srmio_pc7_xfer_chunk_next( srmio_pc_t conn, srmio_chunk_t chunk,
	bool *is_intervall, bool *start_intervall )
{
	unsigned char *buf;
	bool is_int, is_first;
	size_t pkt_chunk;

	assert( conn );
	assert( chunk );

	if( conn->xfer_state != srmio_pc_xfer_state_running )
		return false;

	if( SELF(conn)->chunk_num >= SELF(conn)->chunk_cnt ){
		return false;
	}

	pkt_chunk = SELF(conn)->chunk_num % PC7_PKT_CHUNKS;
	SELF(conn)->chunk_num++;

	if( pkt_chunk == 0 )
		if( ! _srmio_pc7_xfer_pkt_next( conn ) )
			return false;

	buf = SELF(conn)->pkt.data + 2 + (pkt_chunk * PC7_CHUNK_SIZE);

#ifdef DEBUG_CHUNK
	DUMPHEX( "", buf, PC7_CHUNK_SIZE );
#endif

	chunk->time = SELF(conn)->block_time + (SELF(conn)->chunk_num -1) *
		SELF(conn)->block_recint;
	chunk->dur = SELF(conn)->block_recint;
	chunk->pwr = buf_get_buint16( buf, 0 );
	chunk->cad = buf_get_uint8( buf, 2 );
	chunk->speed = 0.1 * buf_get_buint16( buf, 3 );
	chunk->hr = buf_get_uint8( buf, 5 );
	chunk->ele = buf_get_bint16( buf, 6 );
	chunk->temp = 0.1 * buf_get_bint16( buf, 8 );

	// TODO: hmm, this means, there has to be at least one chunk
	// that's not part of a lap as seperator between two laps. pc5 had
	// a seperate bit to indicate intervall start... couldn't find
	// this for pc7
	is_int = buf_get_uint8( buf, 10 );
	is_first = ! SELF(conn)->is_intervall && is_int;
	SELF(conn)->is_intervall = is_int;

	if( is_intervall )
		*is_intervall = is_int;

	if( start_intervall )
		*start_intervall = is_first;


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

	return true;
}

static bool _srmio_pc7_xfer_finish( srmio_pc_t conn )
{
	assert( conn );

	DPRINTF("");
	if( SELF(conn)->pkt_num * PC7_PKT_CHUNKS < SELF(conn)->chunk_cnt ){
		DPRINTF("abort");
		conn->xfer_state = srmio_pc_xfer_state_abort;
	}

	srmio_io_flush( conn->io );
	conn->xfer_state = srmio_pc_xfer_state_new;

	return true;
}

/************************************************************
 *
 * handle management
 */

static void _srmio_pc7_free( srmio_pc_t conn )
{
	assert( conn );

	free( SELF(conn) );
}


static const srmio_pc_methods_t _pc7_methods = {
	.free			= _srmio_pc7_free,
	.open			= _srmio_pc7_open,
	.close			= _srmio_pc7_close,
	.cmd_get_athlete	= _srmio_pc7_cmd_get_athlete,
	.cmd_set_time		= _srmio_pc7_cmd_set_time,
	.cmd_set_recint		= _srmio_pc7_cmd_set_recint,
	.cmd_clear		= _srmio_pc7_cmd_clear,
	.xfer_start		= _srmio_pc7_xfer_start,
	.xfer_block_next	= _srmio_pc7_xfer_block_next,
	.xfer_block_progress	= _srmio_pc7_xfer_block_progress,
	.xfer_chunk_next	= _srmio_pc7_xfer_chunk_next,
	.xfer_finish		= _srmio_pc7_xfer_finish,
};

srmio_pc_t srmio_pc7_new( void )
{
	srmio_pc7_t self;
	srmio_pc_t conn;

	if( NULL == (self = malloc(sizeof(struct _srmio_pc7_t))))
		return NULL;

	memset(self, 0, sizeof( struct _srmio_pc7_t ));

	if( NULL == ( conn = srmio_pc_new( &_pc7_methods, (void*)self ) ))
		goto clean1;

	conn->can_preview = true;
	conn->baudrate = srmio_io_baud_38400;
	conn->parity = srmio_io_parity_none;

	return conn;

clean1:
	free(self);
	return NULL;
}

