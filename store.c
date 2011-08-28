/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include "common.h"

/************************************************************
 *
 * file
 *
 */

struct _store_file_t {
	char *fname;
	srmio_time_t start;
	srmio_time_t end;
};
typedef struct _store_file_t *store_file_t;

static store_file_t _store_file_new(
	const char *fname,
	srmio_time_t start,
	srmio_time_t end )
{
	store_file_t file;

	if( NULL == (file = malloc(sizeof(struct _store_file_t))))
		return NULL;

	if( NULL == (file->fname = strdup(fname )))
		goto clean1;

	file->start = start;
	file->end = end;

	return file;
clean1:
	free( file );
	return NULL;
}

static void _store_file_free( store_file_t file )
{
	assert( file );

	free(file->fname );
	free(file);
}

#define list_file_new( list ) srmio_list_new((srmio_list_closure)_store_file_free)
#define list_file_free( list ) srmio_list_free(list);
#define list_file( list ) (store_file_t *)(srmio_list(list))
#define list_file_add( list, file ) srmio_list_add(list,(void*)file)

/************************************************************
 *
 * athlete
 *
 */

struct _store_athlete_t {
	char *name;
	char *path;
	srmio_list_t file;
	bool scanned;
};
typedef struct _store_athlete_t *store_athlete_t;


#define NICK_SIZE 128

static store_athlete_t _store_athlete_new(
	const char *name,
	const char *path )
{
	store_athlete_t athlete;

	if( NULL == (athlete = malloc(sizeof(struct _store_athlete_t))))
		return NULL;

	if( NULL == (athlete->name = strdup(name )))
		goto clean1;

	if( NULL == (athlete->path = strdup(path )))
		goto clean2;

	if( NULL == (athlete->file = list_file_new()))
		goto clean3;

	athlete->scanned = false;

	return athlete;

clean3:
	free( athlete->path );
clean2:
	free( athlete->name );
clean1:
	free( athlete );
	return NULL;
}

static void _store_athlete_free( store_athlete_t athlete )
{
	assert( athlete );

	list_file_free( athlete->file );
	free( athlete->name );
	free( athlete->path );
	free( athlete );
}

#define list_athlete_new( list ) srmio_list_new((srmio_list_closure)_store_athlete_free)
#define list_athlete_free( list ) srmio_list_free(list);
#define list_athlete( list ) (store_athlete_t *)srmio_list(list)
#define list_athlete_add( list, athlete ) srmio_list_add(list,(void*)athlete)

/************************************************************
 *
 * store
 *
 */


struct _srmio_store_t {
	char	*path;
	srmio_list_t athlete;
};

static store_athlete_t _find_athlete( srmio_store_t store,
	const char *name )
{
	size_t i, used;
	store_athlete_t *list;

	assert( store );
	assert( name );

	list = list_athlete( store->athlete );
	used = srmio_list_used( store->athlete );
	for( i = 0; i < used; ++i ){
		if( 0 == strcasecmp( list[i]->name, name ) )
			return list[i];
	}

	return NULL;
}

static bool _scan_athletes( srmio_store_t store, srmio_error_t *err )
{
	DIR *dh;
	struct dirent *ent;

	if( NULL == (dh = opendir(store->path))){
		if( errno == ENOENT )
			return true;
		srmio_error_errno( err, "failed to open store %s",
			store->path );
		return false;
	}

	errno = 0;
	while( NULL != (ent = readdir(dh))){
		int len;
		char path[PATH_MAX];
		char nick[NICK_SIZE];
		struct stat st;
		store_athlete_t athlete;

		len = strlen( ent->d_name );

		// _racl.SRM
		if( len < 5)
			continue;

		if( len >= NICK_SIZE )
			continue;

		if( ent->d_name[0] != '_' )
			continue;

		if( 0 != strcasecmp( &ent->d_name[len -4], ".srm") )
			continue;

		if( PATH_MAX <= snprintf( path, PATH_MAX, "%s/%s",
			store->path, ent->d_name ) ){

			srmio_error_errno( err, "build athlete path failed" );
			goto clean1;
		}

		if( 0 != stat( path, &st )){
			srmio_error_errno( err, "stat athlete path failed" );
			goto clean1;
		}

		if( ! S_ISDIR(st.st_mode))
			continue;

		strcpy( nick, &ent->d_name[1] );
		nick[len-5] = 0;

		if( _find_athlete( store, nick ) )
			continue;

		DPRINTF("adding athlete: %s", nick );

		if( NULL == ( athlete = _store_athlete_new( nick, path ))){
			srmio_error_errno(err, "new store athlete" );
			goto clean1;
		}

		if( ! list_athlete_add( store->athlete, athlete ) ){
			srmio_error_errno( err, "store add athlete" );
			goto clean1;
		}

		errno = 0;
	}
	if( errno ){
		srmio_error_errno( err, "readdir %s", store->path );
		goto clean1;
	}

	closedir( dh );
	return true;
clean1:
	closedir( dh );
	return false;
}


srmio_store_t srmio_store_new( const char *path, srmio_error_t *err )
{
	srmio_store_t store;

	if( NULL == (store = malloc(sizeof(struct _srmio_store_t)))){
		srmio_error_errno(err, "new store" );
		return NULL;
	}

	if( NULL == (store->path = strdup( path ))){
		srmio_error_errno( err, "new store path" );
		goto clean1;
	}

	if( NULL == (store->athlete = list_athlete_new() ) ){
		srmio_error_errno( err, "new store athletes" );
		goto clean1;
	}

	if( ! _scan_athletes( store, err ) )
		goto clean2;

	mkdir( path, 00777 ); // ignore failure
	return store;

clean2:
	list_athlete_free( store->athlete );
clean1:
	free(store);
	return NULL;
}

void srmio_store_free( srmio_store_t store )
{
	assert( store );

	list_athlete_free( store->athlete );
	free( store->path );
	free(store);
}

static store_file_t _find_file( store_athlete_t athlete,
	srmio_time_t start, srmio_time_t fuzz )
{
	size_t i, used;
	store_file_t *list;

	assert( athlete );

	list = list_file( athlete->file );
	used = srmio_list_used( athlete->file );
	for( i = 0; i < used; ++i ){
		if( start + fuzz >= list[i]->start
			&& start <= list[i]->end ){
			return list[i];
		}
	}

	return NULL;
}

static bool _scan_file( store_athlete_t athlete,
	const char *dir,
	const char *fname,
	srmio_error_t *err )
{
	char path[PATH_MAX];
	FILE *fh;
	srmio_data_t data;
	srmio_time_t start, end;

	assert( athlete );
	assert( dir );
	assert( fname );

	if( PATH_MAX <= snprintf( path, PATH_MAX, "%s/%s",
		dir, fname ) ){

		srmio_error_errno( err, "path too long");
		return false;
	}

	if( NULL == (fh = fopen(path, "r"))){
		srmio_error_errno( err, "failed to open %s", path);
		return false;
	}

	if( NULL == (data = srmio_file_srm_read( fh, err )))
		goto clean1;

	if( ! srmio_data_time_start( data, &start, err ))
		goto clean2;
	end = data->chunks[data->cused-1]->time;

	if( ! _find_file( athlete, start, 0 )){
		store_file_t file;

		if( NULL == (file = _store_file_new(fname, start, end ))){
			srmio_error_errno( err, "store file new" );
			goto clean2;
		}

		list_file_add( athlete->file, file );
		//DPRINTF( "added file %s", path );
	}

	srmio_data_free( data );
	fclose(fh);
	return true;

clean2:
	srmio_data_free( data );
clean1:
	fclose( fh );
	return false;
}

static bool _scan_month( store_athlete_t athlete,
	const char *dir, srmio_error_t *err )
{
	char dirpath[PATH_MAX];
	struct dirent *ent;
	DIR *dh;

	assert( athlete );
	assert( dir );

	if( PATH_MAX <= snprintf( dirpath, PATH_MAX, "%s/%s",
		athlete->path, dir ) ){

		srmio_error_errno( err, "path too long");
		return false;
	}

	DPRINTF( "scanning %s", dirpath );
	if( NULL == (dh = opendir(dirpath))){
		if( errno == ENOENT || errno == ENOTDIR )
			return true;
		srmio_error_errno( err, "failed to open dir %s",
			dirpath );
		return false;
	}

	errno = 0;
	while( NULL != (ent = readdir(dh))){
		size_t len;

		len = strlen(ent->d_name);

		// r300711A.srm
		if( len != 12 )
			continue;

		if( ! _scan_file( athlete, dirpath, ent->d_name, err ) )
			goto clean1;

		errno = 0;
	}
	if( errno ){
		srmio_error_errno( err, "readdir %s", dirpath );
		goto clean1;
	}

	closedir( dh );
	return true;
clean1:
	closedir( dh );
	return false;
}

static bool _scan_athlete(store_athlete_t athlete,
	srmio_error_t *err )
{
	struct dirent *ent;
	DIR *dh;

	assert( athlete );

	if( athlete->scanned )
		return true;

	DPRINTF( "scanning %s", athlete->path );

	if( NULL == (dh = opendir(athlete->path))){
		if( errno == ENOENT )
			return true;
		srmio_error_errno( err, "failed to open store %s",
			athlete->path );
		return false;
	}

	errno = 0;
	while( NULL != (ent = readdir(dh))){
		size_t len;

		len = strlen(ent->d_name);

		// 2011_07.SRM
		if( len != 11 )
			continue;

		if( ! _scan_month( athlete, ent->d_name, err ) )
			goto clean1;

		errno = 0;
	}
	if( errno ){
		srmio_error_errno( err, "readdir %s", athlete->path );
		goto clean1;
	}

	athlete->scanned = true;
	closedir( dh );
	return true;
clean1:
	closedir( dh );
	return false;
}

bool srmio_store_have( srmio_store_t store,
	const char *nick, srmio_time_t start,
	srmio_time_t fuzz, bool *have,
	srmio_error_t *err )
{
	store_athlete_t athlete;

	assert( store );
	assert( nick );
	assert( have );

	*have = false;
	if( NULL == ( athlete = _find_athlete( store, nick )))
		return true;

	if( ! _scan_athlete( athlete, err ) )
		return false;

	if( _find_file( athlete, start, fuzz )){
		*have = true;
		return true;
	}

	return true;
}

bool srmio_store_have_data( srmio_store_t store, srmio_data_t data,
	srmio_time_t fuzz, bool *have,
	srmio_error_t *err )
{
	srmio_time_t start;

	assert( store );
	assert( data );
	assert( data->cused );
	assert( have );

	if( ! srmio_data_time_start( data, &start, err ) )
		return false;

	return srmio_store_have( store, data->athlete, start, fuzz, have, err );
}

static bool srmio_store_fname( store_athlete_t athlete,
	srmio_time_t start,
	char **fname, srmio_error_t *err )
{
	char path[PATH_MAX];
	time_t time;
	struct tm stm;
	char i;

	assert( athlete );
	assert( fname );


	time = 0.1 * start;
#ifdef HAVE_LOCALTIME_R
	if( NULL == localtime_r( &time, &stm )){
		srmio_error_errno( err, "localtime" );
		return false;
	}
#else
	{ struct tm *tmp;
	if( NULL == ( tmp = localtime( &time ))){
		srmio_error_errno( err, "localtime" );
		return false;
	}
	memcpy( &stm, tmp, sizeof(struct tm));
	}
#endif

	// find/add month dir
	if( PATH_MAX <= snprintf( path, PATH_MAX,
		"%s/%04u_%02u.SRM",
		athlete->path,
		stm.tm_year + 1900,
		stm.tm_mon +1 )){

		srmio_error_errno( err, "path too long" );
		return false;
	}
	mkdir( path, 00777 ); // ignore error

	// build file name
	for( i = 'A'; i <= 'Z'; ++i ){
		struct stat st;

		if( PATH_MAX <= snprintf( path, PATH_MAX,
			"%s/%04u_%02u.SRM/%c%02u%02u%02u%c.srm",
			athlete->path,
			stm.tm_year + 1900,
			stm.tm_mon +1,
			athlete->name[0],
			stm.tm_mday,
			stm.tm_mon +1,
			stm.tm_year % 100,
			i )){

			srmio_error_errno( err, "path too long" );
			return false;
		}

		if( 0 != stat( path, &st )){
			if( errno != ENOENT ){
				srmio_error_errno( err, "stat %s", path );
				return false;
			}

			DPRINTF("build filename: %s", path );
			*fname = strdup(path);
			return true;
		}
		DPRINTF("file exists: %s", path );
	}

	srmio_error_set(err, "failed to build unique name" );
	return false;
}


static store_athlete_t _athlete_add( srmio_store_t store, const char *nick, srmio_error_t *err)
{
	char path[PATH_MAX];
	store_athlete_t athlete;

	if( PATH_MAX <= snprintf( path, PATH_MAX, "%s/_%s.SRM",
		store->path, nick ) ){

		srmio_error_errno( err, "path too long");
		return NULL;
	}

	if( 0 != mkdir( path, 00777 ) ){
		srmio_error_errno( err, "mkdir %s", path );
		return NULL;
	}

	if( NULL == (athlete = _store_athlete_new( nick, path ))){
		srmio_error_errno( err, "new athlete" );
		return NULL;
	}

	list_athlete_add( store->athlete, athlete );
	return athlete;
}

bool srmio_store_add( srmio_store_t store, srmio_data_t data,
	char **rfname, srmio_error_t *err )
{
	srmio_time_t start;
	store_athlete_t athlete;
	char *fname;
	FILE *fh;

	assert( store );
	assert( data );

	if( ! srmio_data_time_start( data, &start, err ) )
		return false;

	if( NULL == ( athlete = _find_athlete( store, data->athlete ))){

		if( NULL == ( athlete = _athlete_add( store, data->athlete, err )))
			return false;

	}

	if( ! srmio_store_fname( athlete, start, &fname, err))
		return false;

	if( NULL == (fh = fopen( fname, "w"))){
		srmio_error_errno( err, "fopen(%s)", fname );
		goto clean1;
	}

	if( ! srmio_file_srm7_write( data, fh, err ))
		goto clean2;

	fclose( fh );
	if( rfname )
		*rfname = fname;
	else
		free( fname );
	return true;

clean2:
	fclose( fh );
clean1:
	free(fname);
	return false;
}


