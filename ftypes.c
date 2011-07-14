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
static const char *type_names[srm_ftype_max] = {
	NULL,
	"srm5",
	"srm6",
	"srm7",
	"wkt",
};

static srm_data_t (*rfunc[srm_ftype_max])( const char *fname) = {
	NULL,
	srm_data_read_srm,
	srm_data_read_srm,
	srm_data_read_srm,
	NULL, /* TODO: srm_data_read_wkt, */
};

static int (*wfunc[srm_ftype_max])(srm_data_t data, const char *fname) = {
	NULL,
	NULL, /* TODO: srm_data_write_srm5 */
	NULL, /* TODO: srm_data_write_srm6 */
	srm_data_write_srm7,
	srm_data_write_wkt,
};

/*
 * return numeric file type from textual name
 */
srm_ftype_t srm_ftype_from_string( const char *type )
{
	srm_ftype_t i;

	for( i = srm_ftype_unknown +1; i < srm_ftype_max; ++i ){
		if( strcmp(type, type_names[i] ) == 0 ){
			return i;
		}
	}

	return srm_ftype_unknown;
}

/*
 * read file of specified type
 */
srm_data_t srm_data_read_ftype( srm_ftype_t ftype, const char *fname )
{
	if( rfunc[ftype] == NULL ){
		ERRMSG("reading %s files is not supported",
			type_names[ftype] );
		errno = ENOTSUP;
		return NULL;
	}

	return (rfunc[ftype])( fname );
}

/*
 * write file of specified type
 */
int srm_data_write_ftype( srm_data_t data, srm_ftype_t ftype, const char *fname )
{
	if( wfunc[ftype] == NULL ){
		ERRMSG("writing %s files is not supported",
			type_names[ftype] );
		errno = ENOTSUP;
		return -1;
	}

	return (wfunc[ftype])( data, fname );
}

