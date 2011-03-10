/*
 * Copyright (c) 2011 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include "common.h"

/*
 * write contents of data structure into specified file
 */
int srm_data_write_wkt( srm_data_t data, const char *fname )
{
	int fd;
	FILE *fh;
	unsigned i;

	if( ! data ){
		ERRMSG( "no data to write" );
		errno = EINVAL;
		return -1;
	}

	if( 0 >= (fd = open( fname, O_WRONLY | O_CREAT | O_TRUNC,
		S_IRUSR | S_IWUSR |
		S_IRGRP | S_IWGRP |
		S_IROTH | S_IWOTH ))){

		ERRMSG("open failed: %s", strerror(errno));
		return -1;
	}

	if( NULL == ( fh = fdopen( fd, "w" ))){
		ERRMSG("fdopen failed: %s", strerror(errno));
		goto clean1;
	}

	if( 0 > fprintf( fh,
		"[Params]\n"
		"Version=1\n"
		"Columns=time,dur,work,cad,hr,dist,ele,temp\n"
		) ){

		ERRMSG("fprintf failed: %s", strerror(errno));
		goto clean2;
	}

	if( data->notes && 0 > fprintf( fh, "Note=%s\n",
		data->notes ) ){

		ERRMSG("fprintf failed: %s", strerror(errno));
		goto clean2;
	}

	if( data->mused && data->marker[0]->notes
		&& 0 > fprintf( fh, "Athlete=%s\n",

		data->marker[0]->notes ) ){

		ERRMSG("fprintf failed: %s", strerror(errno));
		goto clean2;
	}

	if( 0 > fprintf( fh,
		"Circum=%u\n"
		"Slope=%.1lf\n"
		"zeropos=%u\n"
		"\n[Chunks]\n",
		data->circum,
		data->slope,
		data->zeropos
		) ){

		ERRMSG("fprintf failed: %s", strerror(errno));
		goto clean2;
	}

	for( i=0; i < data->cused; ++i ){
		srm_chunk_t ck = data->chunks[i];

		if( 0 > fprintf( fh,
			"%.1lf\t"	/* time */
			"%.1lf\t"	/* dur */
			"%.1lf\t"	/* work */
			"%u\t"		/* cad */
			"%u\t"		/* hr */
			"%.3lf\t"	/* dist */
			"%ld\t"		/* ele */
			"%.1lf\n",	/* temp */
			(double)(ck->time / 10),
			(double)(ck->dur / 10),
			(double)( (double)ck->pwr * ck->dur / 10 ),
			ck->cad,
			ck->hr,
			(double)( (double)ck->speed * ck->dur / 36 ),
			ck->ele,
			ck->temp
			) ){

			ERRMSG("fprintf failed: %s", strerror(errno));
			goto clean2;
		}
	}

	if( 0 > fprintf( fh,"\n[Markers]\n" ) ){
		ERRMSG("fprintf failed: %s", strerror(errno));
		goto clean2;
	}

	for( i=1; i < data->mused; ++i ){
		srm_marker_t mk = data->marker[i];
		srm_chunk_t first = data->chunks[mk->first];
		srm_chunk_t last = data->chunks[mk->last];

		if( 0 > fprintf( fh, "%.1lf\t%.1lf\t%s\n",
			(double)( (first->time - first->dur) / 10 ),
			(double)( last->time / 10 ),
			mk->notes ? mk->notes : "" ) ){

			ERRMSG("fprintf failed: %s", strerror(errno));
			goto clean2;
		}
	}

	if( 0 != fclose( fh ) ){
		ERRMSG("fclose failed: %s", strerror(errno));
		goto clean2;
	}

	return 0;

clean2:
	fclose( fh );
	return -1;

clean1:
	close( fd );
	return -1;
}

