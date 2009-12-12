/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include "common.h"

/* used for list of blocks within SRM files */
struct _srm_block_t {
	srm_time_t	daydelta;
	unsigned	chunks;
};

static int _xread( int fd, unsigned char *buf, size_t len )
{
	int ret;

	ret = read( fd, buf, len );

	if( ret < 0)
		return -1;

	if( (size_t)ret < len ){
		errno = EIO;
		return -1;
	}

	return ret;
}

#define CINT16(buf,pos)	((buf[pos+1] << 8) | buf[pos] )
#define CINT32(buf,pos)	( (buf[pos+3] << 24) \
	| (buf[pos+2] << 16) \
	| (buf[pos+1] << 8) \
	| buf[pos] )

static inline int _setuint8( unsigned char *buf, size_t pos, uint16_t x )
{
	if( x > UINT8_MAX ){
		errno = ERANGE;
		return -1;
	}

	buf[pos] = x;

	return 0;
}

static inline int _setuint16( unsigned char *buf, size_t pos, uint32_t x )
{
	if( x > UINT16_MAX ){
		errno = ERANGE;
		return -1;
	}

	buf[pos] = x & 0xff;
	buf[pos+1] = (x >> 8) & 0xff;

	return 0;
}

static inline int _setuint32( unsigned char *buf, size_t pos, uint64_t x )
{
	if( x > UINT32_MAX ){
		errno = ERANGE;
		return -1;
	}

	buf[pos] = x & 0xff;
	buf[pos+1] = (x >> 8) & 0xff;
	buf[pos+2] = (x >> 16) & 0xff;
	buf[pos+3] = (x >> 24) & 0xff;

	return 0;
}


static inline int _setint16( unsigned char *buf, size_t pos, int32_t x )
{
	if( x > INT16_MAX || x < INT16_MIN ){
		errno = ERANGE;
		return -1;
	}

	buf[pos] = x & 0xff;
	buf[pos+1] = (x >> 8) & 0xff;

	return 0;
}

static inline int _setint32( unsigned char *buf, size_t pos, int64_t x )
{
	if( x > INT32_MAX || x < INT32_MIN ){
		errno = ERANGE;
		return -1;
	}

	buf[pos] = x & 0xff;
	buf[pos+1] = (x >> 8) & 0xff;
	buf[pos+2] = (x >> 16) & 0xff;
	buf[pos+3] = (x >> 24) & 0xff;

	return 0;
}


/* days since 0000-01-01 */
#define DAYS_SRM 686656u
#define DAYS_EPOCH 719528u
#define SRM2EPOCH (DAYS_EPOCH - DAYS_SRM)

/* convert "days since 1880-01-01" to srm_time_t */
static srm_time_t _srm_mktime( unsigned days )
{
	time_t ret;
	struct tm t;

	memset( &t, 0, sizeof(struct tm));
	t.tm_mday = 1;
	t.tm_year = 70;
	t.tm_isdst = -1;

	if( days < SRM2EPOCH ){
		/* TODO: SRM2EPOCH is a hack that might cause problems
		 * with misadjusted SRM clocks or other time glitches */
		errno = ENOTSUP;
		return (srm_time_t)-1;
	}
	t.tm_mday += days - SRM2EPOCH;

	ret = mktime( &t );
	if( ret == (time_t) -1 )
		return (srm_time_t)-1;

	DPRINTF( "_srm_mktime %u days -> %lu", days, (unsigned long)ret );
	return (srm_time_t)ret * 10;
}

const unsigned cumul_days[12] = {
	0,	/* begin of jan */
	31,
	59,
	90,
	120,
	151,
	181,
	212,
	243,
	273,
	304,
	334	/* begin of dec */
};

/* convert time_t into "days since 1880-01-01" */
static unsigned _srm_mkdays( srm_time_t input )
{
	time_t tstamp;
	struct tm tm;
	unsigned year;
	unsigned days;


	tstamp = input / 10;
#ifdef HAVE_LOCALTIME_R
	if( NULL == localtime_r( &tstamp, &tm ) )
		return -1;
#else
	{ struct tm *tmp;
	if( NULL == ( tmp = localtime( &tstamp )))
		return -1;
	memcpy( &tm, tmp, sizeof(struct tm));
	}
#endif

	year = tm.tm_year + 1900;

	days = 365 * year 
		+ (year - 1)/4 
		- (year - 1)/100 
		+ (year - 1)/400

		+ cumul_days[tm.tm_mon]
		+ tm.tm_mday;

	if( tm.tm_mon >=2 ){
		if( year % 4 == 0 )
			++days;

		if( year % 100 == 0 )
			--days;

		if( year % 400 == 0 )
			++days;
	}

	if( days < DAYS_SRM ){
		errno = ERANGE;
		return -1;
	}
	days -= DAYS_SRM;

	DPRINTF( "_srm_mkdays %.1f -> %u", 
		(double)input/10,
		days );
	return days;
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

/*
 * read SRM6/SRM7 files, fill newly allocated data structure.
 *
 * on success data pointer is returned.
 * returns NULL and sets errno on failure.
 */
srm_data_t srm_data_read( const char *fname )
{
	srm_data_t tmp;
	int fd;
	unsigned char buf[1024];
	srm_time_t timerefday;
	srm_data_read_cfunc cfunc = NULL;
	unsigned chunklen;
	unsigned bcnt;
	struct _srm_block_t **blocks = NULL;
	unsigned mcnt;
	unsigned i;

	if( NULL == (tmp = srm_data_new()))
		return NULL;

	if( 0 > ( fd = open( fname, O_RDONLY )))
		goto clean1;


	/* header */

	if( 0 > _xread( fd, buf, 86 ) )
		goto clean2;
	DUMPHEX( "srm_data_read head", buf, 86 );

	if( 0 == strncmp( (char*)buf, "SRM6", 4 )){
		chunklen = 5;
		cfunc = _srm_data_chunk_srm6;

	} else if( 0 == strncmp( (char*)buf, "SRM7", 4 )){
		chunklen = 14;
		cfunc = _srm_data_chunk_srm7;

	} else {
		errno = ENOTSUP;
		goto clean2;
	}

	if( (srm_time_t)-1 == (timerefday = _srm_mktime( CINT16(buf,4) )))
		goto clean2;
#ifdef DEBUG
	{ 
	time_t t = timerefday / 10;
	DPRINTF( "srm_data_read timerefday %u %.1f %s", 
		(unsigned)CINT16(buf,4), 
		(double)timerefday/10, ctime( &t));
	}
#endif

	tmp->circum = CINT16(buf,6);
	tmp->recint = 10 * buf[8] / buf[9];
	bcnt = CINT16(buf,10);
	mcnt = CINT16(buf,12) +1;
	DPRINTF( "srm_data_read bcnt=%u mcnt=%u,", bcnt, mcnt );

	/* "notes" is preceeded by length + zero padded. Ignore length... */
	if( NULL == (tmp->notes = malloc(71) ))
		goto clean2;
	/* TODO: iconv notes cp850 -> internal */
	memcpy( tmp->notes, (char*)&buf[16], 70 );
	tmp->notes[70] = 0;

	/* marker */
	if( NULL == (tmp->marker = malloc( (mcnt +1) * sizeof(srm_marker_t *))))
		goto clean2;
	*tmp->marker = NULL;
	tmp->mavail = mcnt;

	for( ; tmp->mused < mcnt; ++tmp->mused ){
		srm_marker_t tm;

		if( 0 > _xread( fd, buf, 270 ))
			goto clean2;
		DUMPHEX( "srm_data_read marker", buf, 270 );

		if( NULL == (tm = srm_marker_new()))
			goto clean2;

		tmp->marker[tmp->mused] = tm;
		tmp->marker[tmp->mused+1] = NULL;

		tm->first = CINT16(buf,256)-1;
		tm->last = CINT16(buf,258)-1;

		if( NULL == (tm->notes = malloc(256)))
			goto clean2;
		/* TODO: iconv notes cp850 -> internal */
		memcpy( tm->notes, (char*)buf, 255 );
		tm->notes[255] = 0;

		DPRINTF( "srm_data_read marker %u %u %s",
			tm->first,
			tm->last,
			tm->notes );
	}

	/* blocks */
	if( NULL == (blocks = malloc( (bcnt+1) * sizeof( struct _srm_block_t *))))
		goto clean2;
	*blocks=NULL;

	for( i = 0; i < bcnt; ++i ){
		struct _srm_block_t *tb;
		
		if( 0 > _xread( fd, buf, 6 ))
			goto clean3;
		DUMPHEX( "srm_data_read block", buf, 6 );

		if( NULL == (tb = malloc(sizeof(struct _srm_block_t))))
			goto clean3;

		blocks[i] = tb;
		blocks[i+1] = NULL;;

		tb->daydelta = CINT32(buf,0) / 10;
		tb->chunks = CINT16(buf,4);

#ifdef DEBUG
		{
		time_t t = (timerefday + tb->daydelta) / 10;
		DPRINTF( "srm_data_read block %.1f %u %s", 
			(double)tb->daydelta/10,
			tb->chunks,
			ctime( &t) );
		}
#endif

	}

	/* calibration */
	if( 0 > _xread( fd, buf, 7 ))
		goto clean3;
	DUMPHEX( "srm_data_read calibration", buf, 7 );

	tmp->zeropos = CINT16(buf, 0);
	tmp->slope = (double)(CINT16(buf, 2) * 140) / 42781;
	/* ccnt = CINT16(buf, 2) */;
	DPRINTF( "srm_data_read cal zpos=%d slope=%.1f",
		tmp->zeropos, tmp->slope );
	

	/* chunks */
	for( i = 0; blocks[i]; ++i ){
		unsigned ci;

		for( ci = 0; ci < blocks[i]->chunks; ++ci ){
			srm_chunk_t ck;

			if( 0 > _xread( fd, buf, chunklen ))
				goto clean3;

			if( NULL == (ck = (*cfunc)( buf )))
				goto clean3;

			ck->time = timerefday + blocks[i]->daydelta +
				ci * tmp->recint;
			if( ck->time < timerefday ){
				errno = EOVERFLOW;
				goto clean3;
			}

			if( 0 > srm_data_add_chunkp( tmp, ck ) )
				goto clean3;

		}

		free( blocks[i] );
	}
	free( blocks );


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

	if( (size_t)ret < len ){
		errno = EIO;
		return -1;
	}

	return ret;
}

/* TODO: srm_data_write_srm6 */

/*
 * write contents of data structure into specified file
 */
int srm_data_write_srm7( srm_data_t data, const char *fname )
{
	unsigned char buf[1024];
	int fd;
	srm_marker_t *blocks;
	srm_time_t timerefday;
	unsigned i;

	if( ! data ){
		errno = EINVAL;
		return -1;
	}

	if( data->notes && strlen(data->notes) > 255 ){
		errno = EINVAL;
		return -1;
	}

	if( data->cused < 1 || data->cused > UINT16_MAX ){
		errno = EINVAL;
		return -1;
	}

	if( data->mused < 1 || data->mused > UINT16_MAX ){
		errno = EINVAL;
		return -1;
	}

	if( NULL == (blocks = srm_data_blocks( data )))
		return -1;

	/* header */
	{
		srm_chunk_t *ck;
		srm_time_t mintime = -1;
		unsigned long bcnt;
		unsigned mcnt = data->mused -1;
		unsigned days;

		for( bcnt = 0; blocks[bcnt] && bcnt <= ULONG_MAX; ++bcnt );
	
		/* time-jumps in PCV lead to nonlinear timestamps, find
		 * lowest one: */
		for( ck = data->chunks; *ck; ++ck ){
			if( mintime > (*ck)->time )
				mintime = (*ck)->time;
		}

		if( (unsigned)-1 == ( days = _srm_mkdays( mintime )))
			goto clean1;
		if( (srm_time_t)-1 == (timerefday = _srm_mktime( days  )))
			goto clean1;

		DPRINTF( "srm_data_write mcnt=%u bcnt=%lu "
			"mintime=%.1f "
			"days=%u timerefday=%.1f "
			"'%s'",
			mcnt,
			bcnt,
			(double)mintime/10,
			days,
			(double)timerefday/10,
			data->notes );

		if( timerefday > mintime ){
			DPRINTF("srm_data_write: failed to determin time reference" );
			errno = EOVERFLOW;
			goto clean1;
		}

		if( 0 >= (fd = open( fname, O_WRONLY | O_CREAT | O_TRUNC, 
			S_IRUSR | S_IWUSR |
			S_IRGRP | S_IWGRP |
			S_IROTH | S_IWOTH )))

			goto clean1;
	
		/* header */
		memcpy( buf, "SRM7", 4 );
		if( 0 > _setuint16( buf, 4, days ) )
			goto clean1;
		if( 0 > _setuint16( buf, 6, data->circum ) )
			goto clean1;
		if( data->recint < 10 ){
			buf[8] = (unsigned char)(data->recint % 10 );
			buf[9] = 10u;
		} else {
			if( 0 > _setuint8( buf, 8, data->recint / 10 ) )
				goto clean1;
			buf[9] = 1u;
		}
		if( 0 > _setuint16( buf, 10, bcnt ) )
			goto clean1;
		if( 0 > _setuint16( buf, 12, mcnt ) )
			goto clean1;
		buf[14] = 0;
		buf[15] = data->notes ? strlen( data->notes) : 0;
		/* TODO: iconv notes -> cp850 */
		strncpy( (char*)&buf[16], data->notes ? data->notes : "", 70 );

		if( 0 > _xwrite( fd, buf, 86 ))
			goto clean2;
	}


	/* marker */
	for( i = 0; i < data->mused; ++i ){
		srm_marker_t mk = data->marker[i];
		unsigned first = mk->first+1;
		unsigned last = mk->last+1;

		/* TODO: iconv notes -> cp850 */
		strncpy( (char*)buf, mk->notes ? mk->notes : "", 255 );
		buf[255] = 1; /* active */
		if( 0 > _setuint16( buf, 256, first ) )
			goto clean1;
		if( 0 > _setuint16( buf, 258, last ) )
			goto clean1;
		memset( &buf[260], 0, 10 );

		DPRINTF( "srm_data_write marker @%x %u %u %s",
			(int)lseek( fd, 0, SEEK_CUR),
			first,
			last,
			mk->notes );

		if( 0 > _xwrite( fd, buf, 270 ))
			goto clean2;
	}

	/* blocks */
	for( i = 0; blocks[i]; ++i ){
		srm_marker_t bk = blocks[i];
		srm_chunk_t ck = data->chunks[bk->first];
		unsigned blockdelta;
		unsigned len = bk->last - bk->first +1;

		blockdelta = ck->time - timerefday;
		if( blockdelta * 10 < blockdelta ){
			errno = EOVERFLOW;
			goto clean2;
		}
		blockdelta *= 10;

		DPRINTF( "srm_data_write block @%x %.1f %u",
			(int)lseek( fd, 0, SEEK_CUR),
			(double)ck->time/10,
			blockdelta );
		if( 0 > _setuint32( buf, 0, blockdelta) )
			goto clean1;
		if( 0 > _setuint16( buf, 4, len) )
			goto clean1;
		
		if( 0 > _xwrite( fd, buf, 6 ))
			goto clean2;
	}


	/* calibration */
	DPRINTF( "srm_data_write cal @%x", 
		(int)lseek( fd, 0, SEEK_CUR) );
	{
		unsigned slope = 0.5 + ( data->slope * 42781) / 140;

		if( 0 > _setuint16( buf, 0, data->zeropos ) )
			goto clean1;
		if( 0 > _setuint16( buf, 2, slope ) )
			goto clean1;
		if( 0 > _setuint16( buf, 4, data->cused ) )
			goto clean1;
		buf[6] = 0;

		if( 0 > _xwrite( fd, buf, 7 ))
			goto clean2;

	}
	

	/* data */
	DPRINTF( "srm_data_write data @%x", 
		(int)lseek( fd, 0, SEEK_CUR) );
	for( i = 0; blocks[i]; ++i ){
		srm_marker_t bk = blocks[i];
		unsigned ci;

		DPRINTF( "srm_data_write block#%u from %u to %u",
			i,
			bk->first,
			bk->last );

		for( ci = bk->first; ci <= bk->last; ++ci ){
			srm_chunk_t ck = data->chunks[ci];
			unsigned speed = 0.5 + ( ck->speed * 1000) / 3.6;
			int temp = 0.5 + ck->temp * 10;

			if( 0 > _setuint16( buf, 0, ck->pwr ) )
				goto clean1;
			if( 0 > _setuint8( buf, 2, ck->cad ) )
				goto clean1;
			if( 0 > _setuint8( buf, 3, ck->hr ) )
				goto clean1;
			if( 0 > _setuint32( buf, 4, speed ) )
				goto clean1;
			if( 0 > _setint32( buf, 8, ck->ele ) )
				goto clean1;
			if( 0 > _setint16( buf, 12, temp ) )
				goto clean1;

			if( 0 > _xwrite( fd, buf, 14 ))
				goto clean2;
		}

		srm_marker_free(blocks[i]);
	}
	free( blocks );

	return 0;

clean2:
	close(fd);
clean1:
	return -1;
}


