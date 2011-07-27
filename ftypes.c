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
 * local lists for mapping file types
 */
static const char *type_names[srmio_ftype_max] = {
	NULL,
	"srm5",
	"srm6",
	"srm7",
	"wkt",
};

static srmio_data_t (*rfunc[srmio_ftype_max])( FILE *fh, srmio_error_t *err ) = {
	NULL,
	srmio_file_srm_read,
	srmio_file_srm_read,
	srmio_file_srm_read,
	NULL, /* TODO: srmio_file_wkt_read, */
};

static bool (*wfunc[srmio_ftype_max])(srmio_data_t data, FILE *fh, srmio_error_t *err ) = {
	NULL,
	NULL, /* TODO: srmio_file_srm5_write */
	NULL, /* TODO: srmio_file_srm6_write */
	srmio_file_srm7_write,
	srmio_file_wkt_write,
};

/*
 * return numeric file type from textual name
 */
srmio_ftype_t srmio_ftype_from_string( const char *type )
{
	srmio_ftype_t i;

	for( i = srmio_ftype_unknown +1; i < srmio_ftype_max; ++i ){
		if( strcmp(type, type_names[i] ) == 0 ){
			return i;
		}
	}

	return srmio_ftype_unknown;
}

/*
 * read file of specified type
 */
srmio_data_t srmio_file_ftype_read( srmio_ftype_t ftype, FILE *fh, srmio_error_t *err )
{
	if( rfunc[ftype] == NULL ){
		srmio_error_set( err, "reading %s files is not supported",
			type_names[ftype] );
		return NULL;
	}

	return (rfunc[ftype])( fh, err );
}

/*
 * write file of specified type
 */
bool srmio_file_ftype_write( srmio_data_t data, srmio_ftype_t ftype, FILE *fh, srmio_error_t *err )
{
	if( wfunc[ftype] == NULL ){
		srmio_error_set(err, "writing %s files is not supported",
			type_names[ftype] );
		return false;
	}

	return (wfunc[ftype])( data, fh, err );
}

