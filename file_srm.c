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

static bool _xread( FILE *fh, unsigned char *buf, size_t len, srmio_error_t *err )
{
	size_t ret;

	assert( fh );
	assert( buf );
	assert( len );

	ret = fread( buf, 1, len, fh );

	if( ret == 0){
		srmio_error_errno( err, "read" );
		return false;
	}

	if( ret < len ){
		srmio_error_set( err, "incomplete read %u/%u", ret, len);
		return false;
	}

	return true;
}

static bool _xwrite( FILE *fh, unsigned char *buf, size_t len, srmio_error_t *err )
{
	size_t ret;

	assert( fh );
	assert( buf );
	assert( len );

	ret = fwrite( buf, 1, len, fh );

	if( ret == 0){
		srmio_error_errno( err, "write" );
		return false;
	}

	if( ret < len ){
		srmio_error_set( err, "incomplete write %d/%d", ret, len );
		return false;
	}

	return true;
}



/* days since 0000-01-01 */
#define DAYS_SRM 686656u
#define DAYS_EPOCH 719528u
#define SRM2EPOCH (DAYS_EPOCH - DAYS_SRM)

/* convert "days since 1880-01-01" to srmio_time_t */
static srmio_time_t _srm_mktime( unsigned days, srmio_error_t *err )
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
		srmio_error_set( err, "date is before supported range" );
		return (srmio_time_t)-1;
	}
	t.tm_mday += days - SRM2EPOCH;

	ret = mktime( &t );
	if( ret == (time_t) -1 ){
		srmio_error_errno( err, "mktime" );
		return (srmio_time_t)-1;
	}

	DPRINTF( "%u days -> %lu", days, (unsigned long)ret );
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
static unsigned _srm_mkdays( srmio_time_t input, srmio_error_t *err )
{
	time_t tstamp;
	struct tm tm;
	unsigned year;
	unsigned days;


	tstamp = input / 10;
#ifdef HAVE_LOCALTIME_R
	if( NULL == localtime_r( &tstamp, &tm ) ){
		srmio_error_errno( err, "localtime" );
		return -1;
	}
#else
	{ struct tm *tmp;
	if( NULL == ( tmp = localtime( &tstamp ))){
		srmio_error_errno( err, "localtime" );
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
		srmio_error_set( err, "resulting date is outside supported range");
		return -1;
	}
	days -= DAYS_SRM;

	DPRINTF( "%.1f -> %u",
		(double)input/10,
		days );
	return days;
}

typedef srmio_chunk_t (*srmio_file_read_cfunc)( const unsigned char *buf,
	srmio_error_t *err );

static srmio_chunk_t _srmio_data_chunk_srm6( const unsigned char *buf,
	srmio_error_t *err )
{
	srmio_chunk_t ck;
	uint8_t c0, c1, c2;

	if( NULL == (ck = srmio_chunk_new(err) ))
		return NULL;

	c0 = buf_get_uint8( buf, 0 );
	c1 = buf_get_uint8( buf, 1 );
	c2 = buf_get_uint8( buf, 2 );

	ck->pwr = ( c1 & 0x0f) | ( c2 << 4 );
	ck->speed = (double)( ((c1 & 0xf0) << 3)
		| (c0 & 0x7f) ) * 3 / 26;
	ck->cad = buf_get_uint8( buf, 5 );
	ck->hr = buf_get_uint8( buf, 4 );

	return ck;
}


static srmio_chunk_t _srmio_data_chunk_srm7( const unsigned char *buf,
	srmio_error_t *err )
{
	srmio_chunk_t ck;

	if( NULL == (ck = srmio_chunk_new(err) ))
		return NULL;

	ck->pwr = buf_get_luint16( buf, 0 );
	ck->cad = buf_get_uint8( buf, 2 );
	ck->hr = buf_get_uint8( buf, 3 );
	ck->speed = ( (double)buf_get_lint32( buf, 4 ) * 3.6) / 1000;
	if( ck->speed < 0 )
		ck->speed = 0;
	ck->ele = buf_get_lint32( buf, 8 );
	ck->temp = 0.1 * buf_get_lint16( buf, 12 );

	return ck;
}

/*
 * read SRM5/6/7 files, fill newly allocated data structure.
 *
 * on success data pointer is returned.
 * returns NULL and sets errno on failure.
 */
srmio_data_t srmio_file_srm_read( FILE *fh, srmio_error_t *err )
{
	srmio_data_t tmp;
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

	if( NULL == (tmp = srmio_data_new(err)))
		return NULL;


	/* header */

	if( ! _xread( fh, buf, 86, err ) )
		goto clean2;
	DUMPHEX( "head", buf, 86 );

	if( 0 != strncmp( (char*)buf, "SRM", 3 )){
		srmio_error_set( err, "unrecognized file format");
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
		srmio_error_set( err, "unsupported file format version: %c", buf[3] );
		goto clean2;
	}

	if( (srmio_time_t)-1 == (timerefday = _srm_mktime( buf_get_luint16( buf, 4), err )))
		goto clean2;
#ifdef DEBUG
	{
	time_t t = timerefday / 10;
	DPRINTF( "timerefday %u %.1f %s",
		(unsigned)buf_get_luint16( buf, 4),
		(double)timerefday/10, ctime( &t));
	}
#endif

	tmp->circum = buf_get_luint16( buf, 6);
	recint = 10 * buf_get_uint8( buf, 8 )
		/ buf_get_uint8( buf, 9 );
	bcnt = buf_get_luint16( buf, 10);
	mcnt = buf_get_luint16( buf, 12);
	DPRINTF( "bcnt=%u mcnt=%u(+1)", bcnt, mcnt );

	/* "notes" is preceeded by length + zero padded */
	/* TODO: iconv notes cp850 -> internal */
	if( NULL == (tmp->notes = buf_get_string( buf, 16, 70 ))){
		srmio_error_errno( err, "get notes" );
		goto clean2;
	}

	/* marker */
	if( NULL == (tmp->marker = malloc( (mcnt +1) * sizeof(srmio_marker_t *)))){
		srmio_error_errno( err, "alloc marker" );
		goto clean2;
	}
	*tmp->marker = NULL;
	tmp->mavail = mcnt;

	/* first marker is just used for the athlete name */
	if( ! _xread( fh, buf, mcmtlen + 15, err ))
		goto clean2;

	if( NULL == (tmp->athlete = buf_get_string( buf, 0, mcmtlen ))){
		srmio_error_errno( err, "get athlete" );
		goto clean2;
	}
	/* TODO: iconv athlete cp850 -> internal */

	/* remaining marker */
	for( ; tmp->mused < mcnt; ++tmp->mused ){
		srmio_marker_t tm;

		if( ! _xread( fh, buf, mcmtlen + 15, err ))
			goto clean2;

		if( NULL == (tm = srmio_marker_new(err)))
			goto clean2;

		tmp->marker[tmp->mused] = tm;
		tmp->marker[tmp->mused+1] = NULL;

		tm->first = buf_get_luint16( buf, mcmtlen +1)-1;
		tm->last = buf_get_luint16( buf, mcmtlen +3)-1;

		if( NULL == (tm->notes = buf_get_string( buf, 0, mcmtlen))){
			srmio_error_errno( err, "get notes");
			goto clean2;
		}
		/* TODO: iconv notes cp850 -> internal */

		DPRINTF( "marker %u %u %s",
			tm->first,
			tm->last,
			tm->notes );
	}

	/* blocks */
	if( NULL == (blocks = malloc( (bcnt+2) * sizeof( struct _srm_block_t *)))){
		srmio_error_errno( err, "alloc blocks" );
		goto clean2;
	}
	*blocks=NULL;

	for( i = 0; i < bcnt; ++i ){
		struct _srm_block_t *tb;

		if( ! _xread( fh, buf, 6, err ))
			goto clean3;

		if( NULL == (tb = malloc(sizeof(struct _srm_block_t)))){
			srmio_error_errno( err, "alloc block" );
			goto clean3;
		}

		blocks[i] = tb;
		blocks[i+1] = NULL;;

		tb->daydelta = buf_get_luint32( buf, 0) / 10;
		tb->chunks = buf_get_luint16( buf, 4);

#ifdef DEBUG
		{
		time_t t = (timerefday + tb->daydelta) / 10;
		DPRINTF( "block %.1f %u %s",
			(double)tb->daydelta/10,
			tb->chunks,
			ctime( &t) );
		}
#endif

	}

	/* calibration */
	if( ! _xread( fh, buf, 7, err ))
		goto clean3;
	DUMPHEX( "calibration", buf, 7 );

	tmp->zeropos = buf_get_luint16( buf, 0);
	tmp->slope = (double)(buf_get_luint16( buf, 2) * 140) / 42781;
	ckcnt = buf_get_luint16( buf, 4);
	DPRINTF( "cal zpos=%d slope=%.1f, chunks=%u",
		tmp->zeropos, tmp->slope, ckcnt );

	/* synthesize block for SRM5 files */
	if( bcnt == 0 ){
		blocks[1] = NULL;;

		if( NULL == (blocks[0] = malloc(sizeof(struct _srm_block_t)))){
			srmio_error_errno( err, "alloc block" );
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

			if( ! _xread( fh, buf, chunklen, err )){
				if( ! tmp->cused )
					goto clean3;
				STATMSG( "failed to read all chunks" );
				goto premature_end;
			}

			if( NULL == (ck = (*cfunc)( buf, err )))
				goto clean3;

			ck->time = timerefday + blocks[i]->daydelta +
				ci * recint;
			ck->dur = recint;
			if( ck->time < timerefday ){
				srmio_error_set( err, "time overflow in block %u", i );
				goto clean3;
			}

			DPRINTF( "chunk "
				"time=%.1f, "
				"temp=%.1f, "
				"pwr=%u, "
				"spd=%.3f, "
				"cad=%u, "
				"hr=%u ",
				(double)ck->time/10,
				ck->temp,
				ck->pwr,
				ck->speed,
				ck->cad,
				ck->hr );

			if( ! srmio_data_add_chunkp( tmp, ck, err ) )
				goto clean3;

		}

		free( blocks[i] );
	}
	free( blocks );


	return tmp;

premature_end:
	/* premature end of file, fix marker */
	ckcnt = tmp->cused -1;
	for( i = 0; i < tmp->mused; ++i ){
		srmio_marker_t mk = tmp->marker[i];

		if( mk->first > ckcnt )
			mk->first = ckcnt;
		if( mk->last > ckcnt )
			mk->last = ckcnt;
	}

	return tmp;

clean3:
	for( i = 0; blocks[i]; ++i )
		free( blocks[i] );
	free( blocks );

clean2:
	srmio_data_free(tmp);
	return NULL;
}

/* TODO: srmio_file_srm6_write */

bool set_marker( unsigned char *buf, char *note, unsigned first,
	unsigned last, srmio_error_t *err )
{
	/* TODO: iconv notes -> cp850 */
	buf_set_string( buf, 0, note, 255 );
	buf_set_uint8( buf, 255, 1 ); /* active */
	if( ! buf_set_luint16( buf, 256, first+1 ) ){
		srmio_error_errno( err, "set marker" );
		return false;
	}
	if( ! buf_set_luint16( buf, 258, last+1 ) ){
		srmio_error_errno( err, "set marker" );
		return false;
	}
	memset( &buf[260], 0, 10 );

	return true;
}


/*
 * write contents of data structure into specified file
 */
bool srmio_file_srm7_write( srmio_data_t data, FILE *fh, srmio_error_t *err )
{
	unsigned char buf[1024];
	srmio_marker_t *blocks;
	srmio_time_t timerefday;
	srmio_time_t recint;
	unsigned i;

	if( ! data ){
		srmio_error_set( err, "no data to write" );
		return false;
	}

	if( data->notes && strlen(data->notes) > 255 ){
		srmio_error_set( err, "notes are too long" );
		return false;
	}

	if( data->athlete && strlen(data->athlete) > 255 ){
		srmio_error_set( err, "athlete name ist too long" );
		return false;
	}

	if( data->cused < 1 || data->cused > UINT16_MAX ){
		srmio_error_set( err, "too few/many chunks" );
		return false;
	}

	if( data->mused > UINT16_MAX ){
		srmio_error_set( err, "too many marker" );
		return false;
	}

	if( ! srmio_data_recint( data, &recint, err ) )
		return false;

	if( NULL == (blocks = srmio_data_blocks( data, err )))
		return false;

	/* header */
	{
		srmio_chunk_t *ck;
		srmio_time_t mintime = -1;
		unsigned long bcnt;
		unsigned mcnt = data->mused; /* +1 for all_workout marker */
		unsigned days;

		/* count blocks */
		for( bcnt = 0; blocks[bcnt] && bcnt <= ULONG_MAX; ++bcnt );

		/* time-jumps in PCV lead to nonlinear timestamps, find
		 * lowest one: */
		for( ck = data->chunks; *ck; ++ck ){
			if( mintime > (*ck)->time )
				mintime = (*ck)->time;
		}

		if( (unsigned)-1 == ( days = _srm_mkdays( mintime, err )))
			goto clean1;
		if( (srmio_time_t)-1 == (timerefday = _srm_mktime( days, err )))
			goto clean1;

		DPRINTF( "mcnt=%u bcnt=%lu "
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
			srmio_error_set( err, "start time is big" );
			goto clean1;
		}

		/* header */
		memcpy( buf, "SRM7", 4 );
		if( ! buf_set_luint16( buf, 4, days ) ){
			srmio_error_errno( err, "set days" );
			goto clean1;
		}
		if( ! buf_set_luint16( buf, 6, data->circum ) ){
			srmio_error_errno( err, "set circum" );
			goto clean1;
		}
		if( recint < 10 ){
			buf_set_uint8( buf, 8, (unsigned char)(recint % 10) );
			buf_set_uint8( buf, 9, 10u );
		} else {
			if( ! buf_set_uint8( buf, 8, recint / 10 ) ){
				srmio_error_errno( err, "set recint" );
				goto clean1;
			}
			buf_set_uint8( buf, 9, 1u );
		}
		if( ! buf_set_luint16( buf, 10, bcnt ) ){
			srmio_error_errno( err, "set block count" );
			goto clean1;
		}
		if( ! buf_set_luint16( buf, 12, mcnt ) ){
			srmio_error_errno( err, "set marker count" );
			goto clean1;
		}
		buf_set_uint8( buf, 14, 0 );

		{
			int len = 0;

			if( data->notes )
				len = strlen( data->notes );

			if( len > 70 ){
				DPRINTF( "notes are too long, truncating" );
				len = 70;
			}

			buf_set_uint8( buf, 15, len );
		}

		/* TODO: iconv notes -> cp850 */
		buf_set_string( buf, 16, data->notes, 70 );

		if( ! _xwrite( fh, buf, 86, err ))
			goto clean2;
	}

	/* first "marker" for athlete name */
	if( ! set_marker( buf, data->athlete, 1, data->cused, err ) )
		goto clean2;

	if( ! _xwrite( fh, buf, 270, err ))
		goto clean2;

	/* other markers */
	for( i = 0; i < data->mused; ++i ){
		srmio_marker_t mk = data->marker[i];
		unsigned first = mk->first+1;
		unsigned last = mk->last+1;

		if( ! set_marker( buf, mk->notes, first, last, err ))
			goto clean2;

		DPRINTF( "marker @0x%lx %u %u %s",
			(unsigned long)ftell(fh),
			first,
			last,
			mk->notes );

		if( ! _xwrite( fh, buf, 270, err ))
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
			srmio_error_set(err, "block %u ref=%.1f: "
				"timespan %u too large, "
				"data covers too much time",
				i,
				(double)timerefday/10,
				blockdelta );

			goto clean2;
		}
		blockdelta *= 10;

		DPRINTF( "block @0x%lx %.1f %u",
			(unsigned long)ftell( fh ),
			(double)ck->time/10,
			blockdelta );
		if( ! buf_set_luint32( buf, 0, blockdelta) ){
			srmio_error_errno( err, "set block time" );
			goto clean1;
		}
		if( ! buf_set_luint16( buf, 4, len) ){
			srmio_error_errno( err, "set block chunks" );
			goto clean1;
		}

		if( ! _xwrite( fh, buf, 6, err ))
			goto clean2;
	}


	/* calibration */
	DPRINTF( "cal @0x%lx", (unsigned long)ftell( fh ) );
	{
		/* TODO: check overflow: slope */
		unsigned slope = 0.5 + ( data->slope * 42781) / 140;

		if( ! buf_set_luint16( buf, 0, data->zeropos ) ){
			srmio_error_errno( err, "set zeropos" );
			goto clean1;
		}
		if( ! buf_set_luint16( buf, 2, slope ) ){
			srmio_error_errno( err, "set slope" );
			goto clean1;
		}
		if( ! buf_set_luint16( buf, 4, data->cused ) ){
			srmio_error_errno( err, "set chunk count" );
			goto clean1;
		}
		buf_set_uint8( buf, 6, 0 );

		if( ! _xwrite( fh, buf, 7, err ))
			goto clean2;

	}


	/* data */
	DPRINTF( "data @0x%lx", (unsigned long)ftell( fh ) );
	for( i = 0; blocks[i]; ++i ){
		srmio_marker_t bk = blocks[i];
		unsigned ci;

		DPRINTF( "block#%u from %u to %u",
			i,
			bk->first,
			bk->last );

		for( ci = bk->first; ci <= bk->last; ++ci ){
			srmio_chunk_t ck = data->chunks[ci];
			/* TODO: check overflow: speed, temp */
			int32_t speed = 0.5 + ( ck->speed * 1000) / 3.6;
			int16_t temp = 0.5 + ck->temp * 10;

			if( ! buf_set_luint16( buf, 0, ck->pwr ) ){
				srmio_error_errno( err, "set power" );
				goto clean1;
			}
			if( ! buf_set_uint8( buf, 2, ck->cad ) ){
				srmio_error_errno( err, "set cadence" );
				goto clean1;
			}
			if( ! buf_set_uint8( buf, 3, ck->hr ) ){
				srmio_error_errno( err, "set heartrate" );
				goto clean1;
			}
			if( ! buf_set_lint32( buf, 4, speed ) ){
				srmio_error_errno( err, "set speed" );
				goto clean1;
			}
			if( ! buf_set_lint32( buf, 8, ck->ele ) ){
				srmio_error_errno( err, "set elevation" );
				goto clean1;
			}
			if( ! buf_set_lint16( buf, 12, temp ) ){
				srmio_error_errno( err, "set temperature" );
				goto clean1;
			}

			if( ! _xwrite( fh, buf, 14, err ))
				goto clean2;
		}

		srmio_marker_free(blocks[i]);
	}
	free( blocks );

	return true;

clean2:
clean1:
	return false;
}


