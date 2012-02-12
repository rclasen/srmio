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

#define PC7_CLEAR_BUSY		0x01
#define PC7_CLEAR_DONE		0x02

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
	struct _srmio_pc7_packet_t *pkt,
	srmio_error_t *err )
{
	srmio_error_t lerr;
	unsigned char buf[PC7_CMD_BUFSIZE];
	int ret;
	size_t buflen;

	assert( conn );
	assert( pkt );

	SRMIO_PC_DEBUG( conn,"cmd 0x%04x", pkt->cmd);
	buflen = PC7_PKTHEAD_SIZE + pkt->datalen +1;

	if( ! buf_set_buint16( buf, 0, PC7_MAGIC ) ){
		SRMIO_PC_ERRNO( conn, err, "set packet magic" );
		return false;
	}

	if( ! buf_set_buint16( buf, 2, pkt->datalen+3 ) ){
		SRMIO_PC_ERRNO( conn, err, "set packet length" );
		return false;
	}

	if( ! buf_set_buint16( buf, 4, pkt->cmd ) ){
		SRMIO_PC_ERRNO( conn, err, "set cmd" );
		return false;
	}


	memcpy( &buf[PC7_PKTHEAD_SIZE], pkt->data, pkt->datalen );

	if( ! buf_set_uint8( buf, buflen -1, _srmio_pc7_checksum( buf,
		buflen -1 ) ) ){

		SRMIO_PC_ERRNO( conn, err, "set checksum" );
		return false;
	}


	SRMIO_PC_DUMP( conn, buf, buflen, "sending" );
	ret = srmio_io_write( conn->io, buf, buflen, &lerr );
	if( ret < 0 ){
		SRMIO_PC_ERROR( conn, err, "write failed: %s", lerr.message );
		return false;
	}

	if( (size_t)ret != buflen ){
		SRMIO_PC_ERROR( conn, err, "failed to send complete command" );
		return false;
	}

	return true;
}

static bool _srmio_pc7_msg_recv( srmio_pc_t conn,
	struct _srmio_pc7_packet_t *pkt,
	srmio_error_t *err )
{
	srmio_error_t lerr;
	unsigned char buf[PC7_CMD_BUFSIZE];
	uint8_t sum;
	int ret;
	size_t buflen = PC7_PKTHEAD_SIZE;

	assert( conn );
	assert( pkt );

	ret = srmio_io_read( conn->io, buf, buflen, &lerr );
	if( ret < 0 ){
		SRMIO_PC_ERROR( conn, err, "read pkt head failed: %s", lerr.message );
		return false;
	}

	SRMIO_PC_DUMP( conn, buf, ret, "got pkt header" );

	if( (size_t)ret < buflen ){
		SRMIO_PC_ERROR( conn, err, "got incomplete packet header: %u/%u",
			ret, buflen );
		return false;
	}

	pkt->magic = buf_get_buint16( buf, 0 );
	pkt->datalen = buf_get_buint16( buf, 2 ) - 3;
	pkt->cmd = buf_get_buint16( buf, 4 );
	buflen += pkt->datalen + 1;

	SRMIO_PC_DEBUG( conn, "pkt body: %d bytes", pkt->datalen +1 );
	if( pkt->datalen +1 > PC7_CMD_BUFSIZE ){
		SRMIO_PC_ERROR( conn, err, "response too large, pleas fix buffer size" );
		return false;
	}

	ret = srmio_io_read( conn->io, &buf[PC7_PKTHEAD_SIZE],
		pkt->datalen +1, err );
	if( ret < 0 ){
		SRMIO_PC_ERROR( conn, err, "read pkt body failed: %s", lerr.message );
		return false;
	}

	SRMIO_PC_DUMP( conn, &buf[PC7_PKTHEAD_SIZE], ret, "got pkt body" );

	if( (size_t)ret < pkt->datalen +1 ){
		SRMIO_PC_ERROR( conn, err, "got incomplete packet body : %u/%u",
			ret, pkt->datalen +1 );
		return false;
	}

	pkt->sum = buf_get_uint8( buf, buflen -1 );
	sum = _srmio_pc7_checksum( buf, buflen -1 );

	SRMIO_PC_DEBUG( conn, "magic=0x%04x datalen=%u cmd=0x%04x sum=0x%02x/%02x",
		pkt->magic, pkt->datalen, pkt->cmd, pkt->sum, sum );

	if( sum != pkt-> sum ){
		SRMIO_PC_ERROR( conn, err, "packet checksum is invalid" );
		return false;
	}

	memcpy( pkt->data, &buf[PC7_PKTHEAD_SIZE], pkt->datalen );

	return true;
}

static bool _srmio_pc7_msg( srmio_pc_t conn,
	struct _srmio_pc7_packet_t *first,
	struct _srmio_pc7_packet_t *retry,
	struct _srmio_pc7_packet_t *recv,
	srmio_error_t *err )
{
	int retries;
	struct _srmio_pc7_packet_t *send = first;

	assert( send );
	assert( recv );

	SRMIO_PC_DEBUG( conn,"cmd 0x%04x", send->cmd );

	for( retries = 0; retries < 3; ++retries ){
		if( retries ){
			if( retry ) send = retry;
			SRMIO_PC_LOG( conn, "cmd 0x%04x/0x%04x retry %d",
				first->cmd, send->cmd, retries );
			srmio_io_send_break( conn->io, NULL );
			sleep(1);
			srmio_io_flush( conn->io, NULL );
		}

		if( ! _srmio_pc7_msg_send( conn, send, err ) )
			return false;

		if( ! _srmio_pc7_msg_recv( conn, recv, err ) ){
			SRMIO_PC_LOG( conn,
				"failed to get response, considering retry");
			continue;
		}

#if REALLY_PARANOID
		if( recv->magic != PC7_MAGIC ){
			SRMIO_PC_ERROR( conn, err, "packet lacks magic bytes");
			continue;
		}

		if( first->cmd != recv->cmd ){
			SRMIO_PC_ERROR( conn, err, "response packet for wrong command");
			continue;
		}
#endif

		return true;
	}

	return false;
}

static bool _srmio_pc7_open( srmio_pc_t conn, srmio_error_t *err )
{
	srmio_error_t lerr;
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0101, // helo
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->io );

	SRMIO_PC_DEBUG( conn, "" );

	if( ! srmio_io_set_baudrate( conn->io, srmio_io_baud_38400, &lerr ) ){
		SRMIO_PC_ERROR( conn, err, "set baudrate failed: %s",
			lerr.message );
		return false;
	}

	if( ! srmio_io_set_parity( conn->io, srmio_io_parity_none, &lerr ) ){
		SRMIO_PC_ERROR( conn, err, "set parity failed: %s",
			lerr.message );
		return false;
	}

	if( ! srmio_io_set_flow( conn->io, srmio_io_flow_none, &lerr ) ){
		SRMIO_PC_ERROR( conn, err, "set flow failed: %s",
			lerr.message );
		return false;
	}

	if( ! srmio_io_update( conn->io, &lerr ) ){
		SRMIO_PC_ERROR( conn, err, "io update failed: %s",
			lerr.message );
		return false;
	}

	if( ! srmio_io_send_break( conn->io, &lerr ) ){
		SRMIO_PC_ERROR( conn, err, "send break failed: %s",
			lerr.message );
		return false;
	}

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv, err ) )
		return false;

	if( recv.datalen != 2 ){
		SRMIO_PC_ERROR( conn, err, "unexpected response size" );
		return false;
	}

	conn->firmware = buf_get_buint16( recv.data, 0 );
	SRMIO_PC_LOG( conn, "found Powercontrol 6/7, version 0x%04x", conn->firmware );

	return true;
}

static bool _srmio_pc7_close( srmio_pc_t conn, srmio_error_t *err )
{
	assert( conn );

	(void) conn;
	(void) err;

	return true;
}

/************************************************************
 *
 * set/get individual parameter
 */

static bool _srmio_pc7_cmd_get_athlete( srmio_pc_t conn, char **name,
	srmio_error_t *err )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0205, // athlete
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );
	assert( name );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv, err ) )
		return false;

	if( recv.datalen != 20 ){
		SRMIO_PC_ERROR( conn, err, "unexpected response size" );
		return false;
	}


	// TODO: athlete encoding
	if( NULL == ( *name = buf_get_string( recv.data, 0, recv.datalen ))){
		SRMIO_PC_ERRNO( conn, err, "get athlete" );
		return false;
	}

	SRMIO_PC_DEBUG( conn,"athlete: %s", *name );

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

static bool _srmio_pc7_cmd_get_time( srmio_pc_t conn, struct tm *timep,
	srmio_error_t *err )
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

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv, err ) )
		return false;

	if( recv.datalen != 7 ){
		SRMIO_PC_ERROR( conn, err, "unexpected response size" );
		return false;
	}

	result = get_time( recv.data, 0, timep );

	SRMIO_PC_DEBUG( conn, "time: %s", asctime(timep) );
	return result;
}

static bool _srmio_pc7_cmd_set_time( srmio_pc_t conn, struct tm *timep,
	srmio_error_t *err )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x020e, // time
		.datalen = 7,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );
	assert( timep );

	SRMIO_PC_DEBUG( conn, "time: %s", asctime(timep) );
	if( ! set_time( send.data, 0, timep ) ){
		SRMIO_PC_ERRNO( conn, err, "invalid date" );
		return false;
	}

	return _srmio_pc7_msg( conn, &send, NULL, &recv, err );
}

static bool _srmio_pc7_cmd_get_circum( srmio_pc_t conn, unsigned *circum,
	srmio_error_t *err )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0203, // circum
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );
	assert( circum );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv, err ) )
		return false;

	if( recv.datalen != 2 ){
		SRMIO_PC_ERROR( conn, err, "unexpected response size" );
		return false;
	}

	*circum = buf_get_buint16( recv.data, 0 );
	SRMIO_PC_DEBUG( conn, "circum: %u", *circum );

	return true;
}


static bool _srmio_pc7_cmd_get_slope( srmio_pc_t conn, double *slope,
	srmio_error_t *err )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0201, // slope
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );
	assert( slope );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv, err ) )
		return false;

	if( recv.datalen != 2 ){
		SRMIO_PC_ERROR( conn, err, "unexpected response size" );
		return false;
	}

	*slope = buf_get_buint16( recv.data, 0 ) / 10;
	SRMIO_PC_DEBUG( conn, "slope: %.1lf", *slope );

	return true;
}

static bool _srmio_pc7_cmd_get_zeropos( srmio_pc_t conn, unsigned *zeropos,
	srmio_error_t *err )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0202, // zeropos
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );
	assert( zeropos );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv, err ) )
		return false;

	if( recv.datalen != 4 ){
		SRMIO_PC_ERROR( conn, err, "unexpected response size" );
		return false;
	}

	*zeropos = buf_get_buint16( recv.data, 0 );
	// TODO: last 2 bytes in zeropos msg are unknown. elevation? slope?
	SRMIO_PC_DEBUG( conn, "zeropos: %u", *zeropos );

	return true;
}

static bool _srmio_pc7_cmd_get_recint( srmio_pc_t conn, srmio_time_t *recint,
	srmio_error_t *err )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0206, // recint
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );
	assert( recint );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv, err ) )
		return false;

	if( recv.datalen != 2 ){
		SRMIO_PC_ERROR( conn, err, "unexpected response size" );
		return false;
	}

	*recint = 0.01 * buf_get_buint16( recv.data, 0 );
	SRMIO_PC_DEBUG( conn, "recint: %.1lf", 0.1 * *recint );

	return true;
}


static bool _srmio_pc7_cmd_set_recint( srmio_pc_t conn, srmio_time_t recint,
	srmio_error_t *err )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0206, // recint
		.datalen = 2,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );

	SRMIO_PC_DEBUG( conn, "recint: %.1lf", 0.1 * recint );
	switch( recint ){
	  case 5:
	  case 10:
	  case 50:
		/* ok, do nothing */
		break;

	  default:
		SRMIO_PC_ERROR( conn, err, "unsuported recording interval: %.1lf",
			.1 * recint );
		return false;
	}

	if( ! buf_set_buint16( recv.data, 0, recint * 100 ) ){
		SRMIO_PC_ERRNO( conn, err, "set recint" );
		return false;
	}

	return _srmio_pc7_msg( conn, &send, NULL, &recv, err );
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

static bool _srmio_pc7_cmd_clear( srmio_pc_t conn, srmio_error_t *err )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0407, // clear
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	assert( conn );
	assert( conn->is_open );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv, err ) )
		return false;

	// TODO: handle "no response = nothing to clear" more graceful

	while( recv.datalen == 2 && recv.data[0] != PC7_CLEAR_DONE ){
		if( ! _srmio_pc7_msg_recv( conn, &recv, err ) )
			return false;
	}

	if( recv.datalen != 2 ){
		SRMIO_PC_ERROR( conn, err, "unexpected clear response size: %u",
			recv.datalen );
		return false;
	}

	if( recv.data[0] != PC7_CLEAR_DONE ){
		SRMIO_PC_ERROR( conn, err, "unexpected clear status: %c/%c",
			recv.data[0], recv.data[1] );
		return false;
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

/* TODO: used/max 118 hours recording time ? */

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
static bool _srmio_pc7_xfer_start( srmio_pc_t conn, srmio_error_t *err )
{
	struct _srmio_pc7_packet_t send = {
		.cmd = 0x0401, // xfer_count
		.datalen = 0,
	};
	struct _srmio_pc7_packet_t recv;

	SRMIO_PC_DEBUG( conn,"");

	// TODO: xfer deleted data?

	assert( conn );
	assert( conn->xfer_state == srmio_pc_xfer_state_new );

	if( ! _srmio_pc7_msg( conn, &send, NULL, &recv, err ) )
		goto fail;

	if( recv.datalen != 2 ){
		SRMIO_PC_ERROR( conn, err, "unexpected response size" );
		goto fail;
	}

	conn->xfer_state = srmio_pc_xfer_state_running;
	SELF(conn)->block_num = 0;

	conn->block_cnt = buf_get_buint16( recv.data, 0 );
	SRMIO_PC_DEBUG( conn,"blocks: %d", conn->block_cnt);

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

	SRMIO_PC_DEBUG( conn,"get block #%d", SELF(conn)->block_num );
	block->athlete = NULL;

	if( conn->xfer_state != srmio_pc_xfer_state_running )
		return false;

	if( SELF(conn)->block_num >= conn->block_cnt ){
		conn->xfer_state = srmio_pc_xfer_state_success;
		return false;
	}

	if( ! _srmio_pc7_msg( conn, &send, &retry, &recv, &conn->err ) )
		goto fail;

	if( recv.datalen == 0 ){
		conn->xfer_state = srmio_pc_xfer_state_success;
		return false;
	}

	if( recv.datalen != 47 ){
		SRMIO_PC_ERROR( conn, &conn->err, "unexpected response size" );
		goto fail;
	}

	/* decode */

	block_num = buf_get_buint16( recv.data, 0 );
	if( SELF(conn)->block_num +1 != block_num ){
		SRMIO_PC_ERROR( conn, &conn->err,
			"got unexpeced block number %u, expected %u",
			block_num, SELF(conn)->block_num +1 );
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
		SRMIO_PC_LOG( conn, "bad timestamp, skipping block %u",
			block_num );
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
		block->end = block->start + block->total * block->recint;
	}


	SRMIO_PC_DEBUG( conn,"block %d, chunks=%d, time=%.1f, recint=%.1f",
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

	SRMIO_PC_DEBUG( conn,"get pkt #%d",  SELF(conn)->pkt_num +1);

	if( conn->xfer_state != srmio_pc_xfer_state_running )
		return false;

	if( ! _srmio_pc7_msg( conn, &send, &retry, &SELF(conn)->pkt, &conn->err ) )
		goto fail;

	if( SELF(conn)->pkt.datalen == 0 ){
		SRMIO_PC_DEBUG( conn,"empty pkt, premature block end");
		SELF(conn)->chunk_num = SELF(conn)->chunk_cnt;
		return false;
	}

	if( SELF(conn)->pkt.datalen != 257 ){
		SRMIO_PC_ERROR( conn, &conn->err, "unexpected response size" );
		goto fail;
	}

	pkt_num = buf_get_buint16( SELF(conn)->pkt.data, 0 );
	SRMIO_PC_DEBUG( conn,"got pkt #%d", pkt_num );

	if( SELF(conn)->pkt_num +1 != pkt_num ){
		SRMIO_PC_ERROR( conn, &conn->err,
			"got unexpeced packet number %u, expected %u",
			pkt_num, SELF(conn)->pkt_num +1 );
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

	SRMIO_PC_DUMP( conn, buf, PC7_CHUNK_SIZE, "chunk" );

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


	SRMIO_PC_DEBUG( conn, "%u: "
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

static bool _srmio_pc7_xfer_finish( srmio_pc_t conn, srmio_error_t *err )
{
	assert( conn );

	SRMIO_PC_DEBUG( conn,"");
	if( SELF(conn)->pkt_num * PC7_PKT_CHUNKS < SELF(conn)->chunk_cnt ){
		SRMIO_PC_DEBUG( conn,"abort");
		conn->xfer_state = srmio_pc_xfer_state_abort;
	}

	srmio_io_flush( conn->io, err );
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
	.cmd_get_time		= _srmio_pc7_cmd_get_time,
	.cmd_get_circum		= _srmio_pc7_cmd_get_circum,
	.cmd_get_slope		= _srmio_pc7_cmd_get_slope,
	.cmd_get_zeropos	= _srmio_pc7_cmd_get_zeropos,
	.cmd_get_recint		= _srmio_pc7_cmd_get_recint,
};

srmio_pc_t srmio_pc7_new( srmio_error_t *err )
{
	srmio_pc7_t self;
	srmio_pc_t conn;

	if( NULL == (self = malloc(sizeof(struct _srmio_pc7_t))))
		return NULL;

	memset(self, 0, sizeof( struct _srmio_pc7_t ));

	if( NULL == ( conn = srmio_pc_new( &_pc7_methods, (void*)self, err ) ))
		goto clean1;

	conn->can_preview = true;
	conn->baudrate = srmio_io_baud_38400;
	conn->parity = srmio_io_parity_none;

	return conn;

clean1:
	free(self);
	return NULL;
}

