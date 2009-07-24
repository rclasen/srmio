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

struct _srm_block_t {
	srm_time_t daydelta;
	size_t	chunks;
};

static int _xread( int fd, unsigned char *buf, size_t len )
{
	int ret;

	ret = read( fd, buf, len );

	if( ret < 0)
		return -1;

	if( ret < (int)len ){
		errno = EPIPE;
		return -1;
	}

	return ret;
}

#define CINT16(buf,pos)	((buf[pos+1] << 8) | buf[pos] )
#define CINT32(buf,pos)	( (buf[pos+3] << 24) \
	| (buf[pos+2] << 16) \
	| (buf[pos+1] << 8) \
	| buf[pos] )

#define DINT16(buf, pos, x) \
	buf[pos] = (unsigned char)((x) & 0xff); \
	buf[pos+1] = (unsigned char)(((x) >> 8) & 0xff)
#define DINT32(buf, pos, x) \
	buf[pos] = (unsigned char)((x) & 0xff); \
	buf[pos+1] = (unsigned char)(((x) >> 8) & 0xff); \
	buf[pos+2] = (unsigned char)(((x) >> 16) & 0xff); \
	buf[pos+3] = (unsigned char)(((x) >> 24) & 0xff)

#define SRM2EPOCH 32872

/* convert "days since 1880-01-01" to srm_time_t */
static srm_time_t _srm_mktime( int days )
{
	struct tm t = {
		.tm_sec = 0,
		.tm_min = 0,
		.tm_hour = 0,
		.tm_mday = 1,
		.tm_mon = 0,
		.tm_year = 70,
		.tm_isdst = -1,
	};

	DPRINTF( "_srm_mktime %d", days );
	if( days < SRM2EPOCH ){
		/* TODO: SRM2EPOCH is a hack that might cause problems
		 * with misadjusted SRM clocks or other time glitches */
		errno = ENOTSUP;
		return -1;
	}
	t.tm_mday += days - SRM2EPOCH;

	return (srm_time_t)10 * mktime( &t );
}


typedef srm_chunk_t (*srm_data_read_cfunc)( const unsigned char *buf );

static srm_chunk_t _srm_data_chunk_srm6( const unsigned char *buf )
{
	srm_chunk_t ck;

	if( NULL == (ck = srm_chunk_new() ))
		return NULL;

	ck->pwr = ( buf[1] & 0x0f) 
		| ( buf[2] << 4 );
	ck->speed = (double)( ((buf[1] & 0xf0) << 3) 
		| (buf[0] & 0x7f) ) * 3 / 26;
	ck->cad = buf[5];
	ck->hr = buf[4];

	return ck;
}

	
static srm_chunk_t _srm_data_chunk_srm7( const unsigned char *buf )
{
	srm_chunk_t ck;

	if( NULL == (ck = srm_chunk_new() ))
		return NULL;

	ck->pwr = CINT16(buf, 0);
	ck->cad = buf[2];
	ck->hr = buf[3];
	ck->speed = ( (double)CINT32(buf, 4) * 3.6) / 1000;
	ck->ele = CINT32(buf, 8);
	ck->temp = 0.1 * (signed int)CINT16(buf,12);

	return ck;
}

srm_data_t srm_data_read( const char *fname )
{
	srm_data_t tmp;
	int fd;
	unsigned char buf[1024];
	srm_time_t timerefday;
	srm_data_read_cfunc cfunc = NULL;
	size_t clen;
	size_t bcnt;
	struct _srm_block_t **blocks;
	size_t mcnt;
	size_t i;

	if( NULL == (tmp = srm_data_new()))
		return NULL;

	if( 0 > ( fd = open( fname, O_RDONLY )))
		goto clean1;


	/* header */

	if( 0 > _xread( fd, buf, 86 ) )
		goto clean2;
	DUMPHEX( "srm_data_read head", (char*)buf, 86 );

	if( 0 == strncmp( (char*)buf, "SRM6", 4 )){
		clen = 5;
		cfunc = _srm_data_chunk_srm6;

	} else if( 0 == strncmp( (char*)buf, "SRM7", 4 )){
		clen = 14;
		cfunc = _srm_data_chunk_srm7;

	} else {
		errno = ENOTSUP;
		goto clean2;
	}

	if( 0 > (timerefday = _srm_mktime( CINT16(buf,4) )))
		goto clean2;
#ifdef DEBUG
	{ 
	time_t t = timerefday / 10;
	DPRINTF( "srm_data_read timerefday %u %.1lf %s", CINT16(buf,4), 
		(double)timerefday/10, ctime( &t));
	}
#endif

	tmp->circum = CINT16(buf,6);
	tmp->recint = 10 * buf[8] / buf[9];
	bcnt = CINT16(buf,10);
	mcnt = CINT16(buf,12) +1;

	if( NULL == (tmp->notes = strndup( (char*)&buf[16], 70 ) ) )
		goto clean2;
	DPRINTF( "srm_data_read bcnt=%d mcnt=%d,", bcnt, mcnt );

	/* marker */
	if( NULL == (tmp->marker = malloc( (mcnt +1) * sizeof(srm_marker_t *))))
		goto clean2;
	*tmp->marker = NULL;
	tmp->mavail = mcnt;

	for( ; tmp->mused < mcnt; ++tmp->mused ){
		srm_marker_t tm;

		if( 0 > _xread( fd, buf, 270 ))
			goto clean2;
		DUMPHEX( "srm_data_read marker", (char*)buf, 270 );

		if( NULL == (tm = srm_marker_new()))
			goto clean2;

		tmp->marker[tmp->mused] = tm;
		tmp->marker[tmp->mused+1] = NULL;

		tm->first = CINT16(buf,256)-1;
		tm->last = CINT16(buf,258)-1;
		if( NULL == (tm->notes = strndup( (char*)buf, 255 ) ))
			goto clean2;

		DPRINTF( "srm_data_read marker %d %d %s", tm->first,
			tm->last, tm->notes );
	}

	/* blocks */
	if( NULL == (blocks = malloc( (bcnt+1) * sizeof( struct _srm_block_t *))))
		goto clean2;
	*blocks=NULL;

	for( i = 0; i < bcnt; ++i ){
		struct _srm_block_t *tb;
		
		if( 0 > _xread( fd, buf, 6 ))
			goto clean3;
		DUMPHEX( "srm_data_read block", (char*)buf, 6 );

		if( NULL == (tb = malloc(sizeof(struct _srm_block_t))))
			goto clean3;

		blocks[i] = tb;
		blocks[i+1] = NULL;;

		tb->daydelta = CINT32(buf,0) / 10;
		tb->chunks = CINT16(buf,4);

#ifdef DEBUG
		{
		time_t t = (timerefday + tb->daydelta) / 10;
		DPRINTF( "srm_data_read block %.1lf %u %s", 
			(double)tb->daydelta/10,
			tb->chunks, ctime( &t) );
		}
#endif

	}

	/* calibration */
	if( 0 > _xread( fd, buf, 7 ))
		goto clean3;
	DUMPHEX( "srm_data_read calibration", (char*)buf, 7 );

	tmp->zeropos = CINT16(buf, 0);
	tmp->slope = (double)(CINT16(buf, 2) * 140) / 42781;
	/* ccnt = CINT16(buf, 2) */;
	DPRINTF( "srm_data_read cal zpos=%d slope=%.1lf",
		tmp->zeropos, tmp->slope );
	

	/* chunks */
	for( i = 0; blocks[i]; ++i ){
		size_t ci;
		for( ci = 0; ci < blocks[i]->chunks; ++ci ){
			srm_chunk_t ck;

			if( 0 > _xread( fd, buf, clen ))
				goto clean3;

			if( NULL == (ck = (*cfunc)( buf )))
				goto clean3;

			ck->time = timerefday + blocks[i]->daydelta +
				ci * tmp->recint;

			if( 0 > srm_data_add_chunkp( tmp, ck ) )
				goto clean3;

		}
	}



	close(fd);
	return tmp;

clean3:
	for( i = 0; blocks[i]; ++i )
		free( blocks[i] );
	free( blocks );

clean2:
	close(fd);

clean1:
	srm_data_free(tmp);
	return NULL;
}

static int _xwrite( int fd, unsigned char *buf, size_t len )
{
	int ret;

	ret = write( fd, buf, len );

	if( ret < 0)
		return -1;

	if( ret < (int)len ){
		errno = EPIPE;
		return -1;
	}

	return ret;
}

/* TODO: srm_data_write_srm6 */

int srm_data_write_srm7( srm_data_t data, const char *fname )
{
	unsigned char buf[1024];
	int fd;
	srm_marker_t *blocks;
	srm_time_t timerefday;
	size_t i;

	if( ! data )
		return 0;

	if( data->mused < 1 ){
		errno = EINVAL;
		return -1;
	}


	if( NULL == (blocks = srm_data_blocks( data )))
		return -1;

	{
		size_t bcnt;
		size_t mcnt = data->mused -1;
		size_t days;

		for( bcnt = 0; blocks[bcnt] ; ++bcnt );
	
		if( bcnt > 0xffff 
			|| data->mused > 0xffff 
			|| data->cused > 0xffff ){

			errno = ERANGE;
			goto clean1;
		}

		days = data->chunks[0]->time / ( 10 * 24 * 3600 ) + SRM2EPOCH;
		timerefday = _srm_mktime( days  );
		DPRINTF( "srm_data_write mcnt=%u bcnt=%u days=%u "
			"timerefday=%.1lf '%s'",
			mcnt, bcnt, days, (double)timerefday/10, data->notes );


		if( 0 >= (fd = open( fname, O_WRONLY | O_CREAT | O_TRUNC, 
			S_IRUSR | S_IWUSR |
			S_IRGRP | S_IWGRP |
			S_IROTH | S_IWOTH )))

			goto clean1;
	
		/* header */
		memcpy( buf, "SRM7", 4 );
		DINT16( buf, 4, days );
		DINT16( buf, 6, data->circum );
		if( data->recint < 10 ){
			buf[8] = (unsigned char)(data->recint % 10 );
			buf[9] = 10;
		} else {
			buf[8] = (unsigned char)(data->recint / 10);
			buf[9] = 1;
		}
		DINT16( buf, 10, bcnt );
		DINT16( buf, 12, mcnt );
		buf[14] = 0;
		buf[15] = data->notes ? strlen( data->notes) : 0;
		strncpy( (char*)&buf[16], data->notes ? data->notes : "", 70 );

		if( 0 > _xwrite( fd, buf, 86 ))
			goto clean2;
	}


	/* marker */
	for( i = 0; i < data->mused; ++i ){
		srm_marker_t mk = data->marker[i];
		int first = mk->first+1;
		int last = mk->last+1;

		strncpy( (char*)buf, mk->notes ? mk->notes : "", 255 );
		buf[255] = 1;
		DINT16(buf, 256, first );
		DINT16(buf, 258, last );
		memset( &buf[260], 0, 10 );

		DPRINTF( "srm_data_write marker @%x %d %d %s",
			(int)lseek( fd, 0, SEEK_CUR),
			first, last, mk->notes );

		if( 0 > _xwrite( fd, buf, 270 ))
			goto clean2;
	}

	/* blocks */
	for( i = 0; blocks[i]; ++i ){
		srm_marker_t bk = blocks[i];
		srm_chunk_t ck = data->chunks[bk->first];
		unsigned int blockdelta = (ck->time - timerefday) * 10;
		int len = bk->last - bk->first +1;

		DPRINTF( "srm_data_write block @%x %.1lf %lu",
			(int)lseek( fd, 0, SEEK_CUR),
			(double)ck->time/10, blockdelta );
		DINT32(buf, 0, blockdelta);
		DINT16(buf, 4, len);
		
		if( 0 > _xwrite( fd, buf, 6 ))
			goto clean2;
	}


	/* calibration */
	DPRINTF( "srm_data_write cal @%x", 
		(int)lseek( fd, 0, SEEK_CUR) );
	{
		int slope = 0.5 + ( data->slope * 42781) / 140;

		DINT16( buf, 0, data->zeropos );
		DINT16( buf, 2, slope );
		DINT16( buf, 4, data->cused );
		buf[6] = 0;

		if( 0 > _xwrite( fd, buf, 7 ))
			goto clean2;

	}
	

	/* data */
	DPRINTF( "srm_data_write data @%x", 
		(int)lseek( fd, 0, SEEK_CUR) );
	for( i = 0; blocks[i]; ++i ){
		srm_marker_t bk = blocks[i];
		size_t ci;

		DPRINTF( "srm_data_write block#%d from %d to %d",
			i, bk->first, bk->last );

		for( ci = bk->first; ci <= bk->last; ++ci ){
			srm_chunk_t ck = data->chunks[ci];
			unsigned int speed = ( ck->speed * 1000) / 3.6;
			int temp = ck->temp * 10;

			DINT16(buf, 0, ck->pwr );
			buf[2] = (unsigned char)ck->cad;
			buf[3] = (unsigned char)ck->hr;
			DINT32( buf, 4, speed );
			DINT32( buf, 8, ck->ele );
			DINT16( buf, 12, temp );

			if( 0 > _xwrite( fd, buf, 14 ))
				goto clean2;
		}
	}

	return 0;

clean2:
	close(fd);
clean1:
	return -1;
}


