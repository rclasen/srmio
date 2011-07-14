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
	srmio_time_t	daydelta;
	unsigned	chunks;
};

static int _xread( int fd, unsigned char *buf, size_t len )
{
	int ret;

	ret = read( fd, buf, len );

	if( ret < 0){
		ERRMSG("read failed: %s", strerror(errno));
		return -1;
	}

	if( (size_t)ret < len ){
		ERRMSG("failed to read some data");
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

/* convert "days since 1880-01-01" to srmio_time_t */
static srmio_time_t _srm_mktime( unsigned days )
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
		ERRMSG("date is before supported range" );
		errno = ENOTSUP;
		return (srmio_time_t)-1;
	}
	t.tm_mday += days - SRM2EPOCH;

	ret = mktime( &t );
	if( ret == (time_t) -1 ){
		ERRMSG("mktime failed: %s", strerror(errno));
		return (srmio_time_t)-1;
	}

	DPRINTF( "_srm_mktime %u days -> %lu", days, (unsigned long)ret );
	return (srmio_time_t)ret * 10;
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
static unsigned _srm_mkdays( srmio_time_t input )
{
	time_t tstamp;
	struct tm tm;
	unsigned year;
	unsigned days;


	tstamp = input / 10;
#ifdef HAVE_LOCALTIME_R
	if( NULL == localtime_r( &tstamp, &tm ) ){
		ERRMSG("localtime failed: %s", strerror(errno));
		return -1;
	}
#else
	{ struct tm *tmp;
	if( NULL == ( tmp = localtime( &tstamp ))){
		ERRMSG("localtime failed: %s", strerror(errno));
		return -1;
	}
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
		ERRMSG("resutling date is outside supported range");
		errno = ERANGE;
		return -1;
	}
	days -= DAYS_SRM;

	DPRINTF( "_srm_mkdays %.1f -> %u",
		(double)input/10,
		days );
	return days;
}

typedef srmio_chunk_t (*srmio_file_read_cfunc)( const unsigned char *buf );

static srmio_chunk_t _srmio_data_chunk_srm6( const unsigned char *buf )
{
	srmio_chunk_t ck;

	if( NULL == (ck = srmio_chunk_new() ))
		return NULL;

	ck->pwr = ( buf[1] & 0x0f)
		| ( buf[2] << 4 );
	ck->speed = (double)( ((buf[1] & 0xf0) << 3)
		| (buf[0] & 0x7f) ) * 3 / 26;
	ck->cad = buf[5];
	ck->hr = buf[4];

	return ck;
}


static srmio_chunk_t _srmio_data_chunk_srm7( const unsigned char *buf )
{
	srmio_chunk_t ck;

	if( NULL == (ck = srmio_chunk_new() ))
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
 * read SRM5/6/7 files, fill newly allocated data structure.
 *
 * on success data pointer is returned.
 * returns NULL and sets errno on failure.
 */
srmio_data_t srmio_file_srm_read( const char *fname )
{
	srmio_data_t tmp;
	int fd;
	unsigned char buf[1024];
	srmio_time_t recint;
	srmio_time_t timerefday;
	srmio_file_read_cfunc cfunc = NULL;
	unsigned chunklen;
	unsigned mcmtlen;
	unsigned bcnt;
	struct _srm_block_t **blocks = NULL;
	unsigned mcnt;
	unsigned ckcnt;
	unsigned i;

	if( NULL == (tmp = srmio_data_new())){
		ERRMSG("srmio_data_new failed: %s", strerror(errno));
		return NULL;
	}

	if( 0 > ( fd = open( fname, O_RDONLY ))){
		ERRMSG("open failed: %s", strerror(errno));
		goto clean1;
	}


	/* header */

	if( 0 > _xread( fd, buf, 86 ) )
		goto clean2;
	DUMPHEX( "srmio_file_read head", buf, 86 );

	if( 0 != strncmp( (char*)buf, "SRM", 3 )){
		ERRMSG("unrecognized file format");
		errno = ENOTSUP;
		goto clean2;

	}

	switch( buf[3] ){
	  case '5':
		mcmtlen = 3;
		chunklen = 5;
		cfunc = _srmio_data_chunk_srm6;
		break;

	  case '6':
		mcmtlen = 255;
		chunklen = 5;
		cfunc = _srmio_data_chunk_srm6;
		break;

	  case '7':
		mcmtlen = 255;
		chunklen = 14;
		cfunc = _srmio_data_chunk_srm7;
		break;

	  default:
		ERRMSG("unsupported file format version: %c", buf[3] );
		errno = ENOTSUP;
		goto clean2;
	}

	if( (srmio_time_t)-1 == (timerefday = _srm_mktime( CINT16(buf,4) )))
		goto clean2;
#ifdef DEBUG
	{
	time_t t = timerefday / 10;
	DPRINTF( "srmio_file_read timerefday %u %.1f %s",
		(unsigned)CINT16(buf,4),
		(double)timerefday/10, ctime( &t));
	}
#endif

	tmp->circum = CINT16(buf,6);
	recint = 10 * buf[8] / buf[9];
	bcnt = CINT16(buf,10);
	mcnt = CINT16(buf,12) +1;
	DPRINTF( "srmio_file_read bcnt=%u mcnt=%u,", bcnt, mcnt );

	/* "notes" is preceeded by length + zero padded. Ignore length... */
	if( NULL == (tmp->notes = malloc(71) )){
		ERRMSG("malloc failed: %s", strerror(errno));
		goto clean2;
	}
	/* TODO: iconv notes cp850 -> internal */
	memcpy( tmp->notes, (char*)&buf[16], 70 );
	tmp->notes[70] = 0;

	/* marker */
	if( NULL == (tmp->marker = malloc( (mcnt +1) * sizeof(srmio_marker_t *)))){
		ERRMSG("malloc failed: %s", strerror(errno));
		goto clean2;
	}
	*tmp->marker = NULL;
	tmp->mavail = mcnt;

	for( ; tmp->mused < mcnt; ++tmp->mused ){
		srmio_marker_t tm;

		if( 0 > _xread( fd, buf, mcmtlen + 15 ))
			goto clean2;
		DUMPHEX( "srmio_file_read marker", buf, mcmtlen + 15 );

		if( NULL == (tm = srmio_marker_new())){
			ERRMSG("srmio_marker_new failed: %s", strerror(errno));
			goto clean2;
		}

		tmp->marker[tmp->mused] = tm;
		tmp->marker[tmp->mused+1] = NULL;

		tm->first = CINT16(buf,mcmtlen +1)-1;
		tm->last = CINT16(buf,mcmtlen +3)-1;

		if( NULL == (tm->notes = malloc(mcmtlen +1))){
			ERRMSG("malloc failed: %s", strerror(errno));
			goto clean2;
		}
		/* TODO: iconv notes cp850 -> internal */
		memcpy( tm->notes, (char*)buf, mcmtlen );
		tm->notes[mcmtlen] = 0;

		DPRINTF( "srmio_file_read marker %u %u %s",
			tm->first,
			tm->last,
			tm->notes );
	}

	/* blocks */
	if( NULL == (blocks = malloc( (bcnt+2) * sizeof( struct _srm_block_t *)))){
		ERRMSG("malloc failed: %s", strerror(errno));
		goto clean2;
	}
	*blocks=NULL;

	for( i = 0; i < bcnt; ++i ){
		struct _srm_block_t *tb;

		if( 0 > _xread( fd, buf, 6 ))
			goto clean3;
		DUMPHEX( "srmio_file_read block", buf, 6 );

		if( NULL == (tb = malloc(sizeof(struct _srm_block_t)))){
			ERRMSG("malloc failed: %s", strerror(errno));
			goto clean3;
		}

		blocks[i] = tb;
		blocks[i+1] = NULL;;

		tb->daydelta = CINT32(buf,0) / 10;
		tb->chunks = CINT16(buf,4);

#ifdef DEBUG
		{
		time_t t = (timerefday + tb->daydelta) / 10;
		DPRINTF( "srmio_file_read block %.1f %u %s",
			(double)tb->daydelta/10,
			tb->chunks,
			ctime( &t) );
		}
#endif

	}

	/* calibration */
	if( 0 > _xread( fd, buf, 7 ))
		goto clean3;
	DUMPHEX( "srmio_file_read calibration", buf, 7 );

	tmp->zeropos = CINT16(buf, 0);
	tmp->slope = (double)(CINT16(buf, 2) * 140) / 42781;
	ckcnt = CINT16(buf, 4);
	DPRINTF( "srmio_file_read cal zpos=%d slope=%.1f, chunks=%u",
		tmp->zeropos, tmp->slope, ckcnt );

	/* synthesize block for SRM5 files */
	if( bcnt == 0 ){
		blocks[1] = NULL;;

		if( NULL == (blocks[0] = malloc(sizeof(struct _srm_block_t)))){
			ERRMSG("malloc failed: %s", strerror(errno));
			goto clean3;
		}

		blocks[0]->daydelta = recint;
		blocks[0]->chunks = ckcnt;
	}

	/* chunks */
	for( i = 0; blocks[i]; ++i ){
		unsigned ci;

		for( ci = 0; ci < blocks[i]->chunks; ++ci ){
			srmio_chunk_t ck;

			if( 0 > _xread( fd, buf, chunklen )){
				// TODO: log( "failed to read all chunks" );
				if( tmp->cused )
					goto clean4;
				else
					goto clean3;
			}

			if( NULL == (ck = (*cfunc)( buf ))){
				ERRMSG("failed to get chunk: %s", strerror(errno));
				goto clean3;
			}

			ck->time = timerefday + blocks[i]->daydelta +
				ci * recint;
			ck->dur = recint;
			if( ck->time < timerefday ){
				ERRMSG("srmio_file_read: timespan too large");
				errno = EOVERFLOW;
				goto clean3;
			}

			if( 0 > srmio_data_add_chunkp( tmp, ck ) ){
				ERRMSG("add chunk failed: %s", strerror(errno));
				goto clean3;
			}

		}

		free( blocks[i] );
	}
	free( blocks );


	close(fd);
	return tmp;

clean4:
	/* premature end of file, fix marker */
	ckcnt = tmp->cused -1;
	for( i = 0; i < tmp->mused; ++i ){
		srmio_marker_t mk = tmp->marker[i];

		if( mk->first > ckcnt )
			mk->first = ckcnt;
		if( mk->last > ckcnt )
			mk->last = ckcnt;
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
	srmio_data_free(tmp);
	return NULL;
}

static int _xwrite( int fd, unsigned char *buf, size_t len )
{
	int ret;

	ret = write( fd, buf, len );

	if( ret < 0){
		ERRMSG("write failed: %s", strerror(errno));
		return -1;
	}

	if( (size_t)ret < len ){
		ERRMSG("write was truncated %d/%d", ret, len );
		errno = EIO;
		return -1;
	}

	return ret;
}

/* TODO: srmio_file_srm6_write */

/*
 * write contents of data structure into specified file
 */
int srmio_file_srm7_write( srmio_data_t data, const char *fname )
{
	unsigned char buf[1024];
	int fd;
	srmio_marker_t *blocks;
	srmio_time_t timerefday;
	srmio_time_t recint;
	unsigned i;

	if( ! data ){
		ERRMSG( "no data to write" );
		errno = EINVAL;
		return -1;
	}

	if( data->notes && strlen(data->notes) > 255 ){
		ERRMSG( "notes are too long" );
		errno = EINVAL;
		return -1;
	}

	if( data->cused < 1 || data->cused > UINT16_MAX ){
		ERRMSG( "too few/many chunks" );
		errno = EINVAL;
		return -1;
	}

	if( data->mused < 1 || data->mused > UINT16_MAX ){
		ERRMSG( "too few/many marker" );
		errno = EINVAL;
		return -1;
	}

	if( 0 == ( recint = srmio_data_recint( data ) )){
		ERRMSG( "cannot identify recint" );
		errno = EINVAL;
		return -1;
	}

	if( NULL == (blocks = srmio_data_blocks( data )))
		return -1;

	/* header */
	{
		srmio_chunk_t *ck;
		srmio_time_t mintime = -1;
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
		if( (srmio_time_t)-1 == (timerefday = _srm_mktime( days  )))
			goto clean1;

		DPRINTF( "srmio_file_write mcnt=%u bcnt=%lu "
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
			ERRMSG("srmio_file_write: failed to determin time reference" );
			errno = EOVERFLOW;
			goto clean1;
		}

		if( 0 >= (fd = open( fname, O_WRONLY | O_CREAT | O_TRUNC,
			S_IRUSR | S_IWUSR |
			S_IRGRP | S_IWGRP |
			S_IROTH | S_IWOTH ))){

			ERRMSG("open failed: %s", strerror(errno));
			goto clean1;
		}

		/* header */
		memcpy( buf, "SRM7", 4 );
		if( 0 > _setuint16( buf, 4, days ) ){
			ERRMSG( "failed to convert date: %s", strerror(errno));
			goto clean1;
		}
		if( 0 > _setuint16( buf, 6, data->circum ) ){
			ERRMSG( "failed to convert circum: %s", strerror(errno));
			goto clean1;
		}
		if( recint < 10 ){
			buf[8] = (unsigned char)(recint % 10 );
			buf[9] = 10u;
		} else {
			if( 0 > _setuint8( buf, 8, recint / 10 ) ){
				ERRMSG( "failed to convert recint: %s", strerror(errno));
				goto clean1;
			}
			buf[9] = 1u;
		}
		if( 0 > _setuint16( buf, 10, bcnt ) ){
			ERRMSG( "failed to convert bcnt: %s", strerror(errno));
			goto clean1;
		}
		if( 0 > _setuint16( buf, 12, mcnt ) ){
			ERRMSG( "failed to convert mcnt: %s", strerror(errno));
			goto clean1;
		}
		buf[14] = 0;
		buf[15] = data->notes ? strlen( data->notes) : 0;
		/* TODO: iconv notes -> cp850 */
		strncpy( (char*)&buf[16], data->notes ? data->notes : "", 70 );

		if( 0 > _xwrite( fd, buf, 86 ))
			goto clean2;
	}


	/* marker */
	for( i = 0; i < data->mused; ++i ){
		srmio_marker_t mk = data->marker[i];
		unsigned first = mk->first+1;
		unsigned last = mk->last+1;

		/* TODO: iconv notes -> cp850 */
		strncpy( (char*)buf, mk->notes ? mk->notes : "", 255 );
		buf[255] = 1; /* active */
		if( 0 > _setuint16( buf, 256, first ) ){
			ERRMSG( "failed to convert marker index: %s", strerror(errno));
			goto clean1;
		}
		if( 0 > _setuint16( buf, 258, last ) ){
			ERRMSG( "failed to convert marker index: %s", strerror(errno));
			goto clean1;
		}
		memset( &buf[260], 0, 10 );

		DPRINTF( "srmio_file_write marker @%x %u %u %s",
			(int)lseek( fd, 0, SEEK_CUR),
			first,
			last,
			mk->notes );

		if( 0 > _xwrite( fd, buf, 270 ))
			goto clean2;
	}

	/* blocks */
	for( i = 0; blocks[i]; ++i ){
		srmio_marker_t bk = blocks[i];
		srmio_chunk_t ck = data->chunks[bk->first];
		unsigned blockdelta;
		unsigned len = bk->last - bk->first +1;

		blockdelta = ck->time - timerefday;
		if( blockdelta * 10 < blockdelta ){
			int j;
			ERRMSG("srmio_file_srm7_write: "
				"block %u ref=%.1f: "
				"timespan %u too large",
				i,
				(double)timerefday/10,
				blockdelta );
			for( j = 0; blocks[j]; ++j ){
				srmio_marker_t b = blocks[j];
				srmio_chunk_t c = data->chunks[b->first];
				unsigned l = b->last - b->first +1;

				ERRMSG(" block %d: time=%.1f len=%u",
					j, (double)c->time/10, l );
			}

			errno = EOVERFLOW;
			goto clean2;
		}
		blockdelta *= 10;

		DPRINTF( "srmio_file_write block @%x %.1f %u",
			(int)lseek( fd, 0, SEEK_CUR),
			(double)ck->time/10,
			blockdelta );
		if( 0 > _setuint32( buf, 0, blockdelta) ){
			ERRMSG( "failed to convert block time: %s", strerror(errno));
			goto clean1;
		}
		if( 0 > _setuint16( buf, 4, len) ){
			ERRMSG( "failed to convert block length: %s", strerror(errno));
			goto clean1;
		}

		if( 0 > _xwrite( fd, buf, 6 ))
			goto clean2;
	}


	/* calibration */
	DPRINTF( "srmio_file_write cal @%x",
		(int)lseek( fd, 0, SEEK_CUR) );
	{
		unsigned slope = 0.5 + ( data->slope * 42781) / 140;

		if( 0 > _setuint16( buf, 0, data->zeropos ) ){
			ERRMSG( "failed to convert zeropos: %s", strerror(errno));
			goto clean1;
		}
		if( 0 > _setuint16( buf, 2, slope ) ){
			ERRMSG( "failed to convert slope: %s", strerror(errno));
			goto clean1;
		}
		if( 0 > _setuint16( buf, 4, data->cused ) ){
			ERRMSG( "failed to convert chunk count: %s", strerror(errno));
			goto clean1;
		}
		buf[6] = 0;

		if( 0 > _xwrite( fd, buf, 7 ))
			goto clean2;

	}


	/* data */
	DPRINTF( "srmio_file_write data @%x",
		(int)lseek( fd, 0, SEEK_CUR) );
	for( i = 0; blocks[i]; ++i ){
		srmio_marker_t bk = blocks[i];
		unsigned ci;

		DPRINTF( "srmio_file_write block#%u from %u to %u",
			i,
			bk->first,
			bk->last );

		for( ci = bk->first; ci <= bk->last; ++ci ){
			srmio_chunk_t ck = data->chunks[ci];
			unsigned speed = 0.5 + ( ck->speed * 1000) / 3.6;
			int temp = 0.5 + ck->temp * 10;

			if( 0 > _setuint16( buf, 0, ck->pwr ) ){
				ERRMSG( "failed to convert power: %s", strerror(errno));
				goto clean1;
			}
			if( 0 > _setuint8( buf, 2, ck->cad ) ){
				ERRMSG( "failed to convert cadence: %s", strerror(errno));
				goto clean1;
			}
			if( 0 > _setuint8( buf, 3, ck->hr ) ){
				ERRMSG( "failed to convert hr: %s", strerror(errno));
				goto clean1;
			}
			if( 0 > _setuint32( buf, 4, speed ) ){
				ERRMSG( "failed to convert speed: %s", strerror(errno));
				goto clean1;
			}
			if( 0 > _setint32( buf, 8, ck->ele ) ){
				ERRMSG( "failed to convert ele: %s", strerror(errno));
				goto clean1;
			}
			if( 0 > _setint16( buf, 12, temp ) ){
				ERRMSG( "failed to convert temp: %s", strerror(errno));
				goto clean1;
			}

			if( 0 > _xwrite( fd, buf, 14 ))
				goto clean2;
		}

		srmio_marker_free(blocks[i]);
	}
	free( blocks );

	return 0;

clean2:
	close(fd);
clean1:
	return -1;
}


