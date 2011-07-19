/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

/************************************************************
 *
 *        !!!!! WARNING !!!!!
 *
 * USE THIS ONLY AT YOUR OWN RISK!
 * THIS MIGHT DESTROY YOUR SRM POWERCONTROL.
 *
 * FYI: If any the powercontrol has only lax input checking.
 * So by sending a garbled/misformed command you might turn
 * it into a brick. So far srmwin could fix things for me, but
 * you might be the first without this luck.
 *
 ************************************************************/

#include "srmio.h"

#include "config.h"

#include <errno.h>
#include <stdio.h>

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif

#ifdef HAVE_STRING_H
# if !defined STDC_HEADERS && defined HAVE_MEMORY_H
#  include <memory.h>
# endif
# include <string.h>
#endif

#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif



static void csvdump( srmio_data_t data )
{
	srmio_chunk_t *ck;
	srmio_time_t recint;

	if( ! srmio_data_recint( data, &recint ) )
		return;

	printf(
		"time\t"
		"dur\t"
		"temp\t"
		"pwr\t"
		"speed\t"
		"cad\t"
		"hr\t"
		"ele"
		"\n");
	for( ck=data->chunks; *ck; ++ck ){
		printf(
			"%.1f\t"	/* "time\t" */
			"%.1f\t"	/* "dur\t" */
			"%.1f\t"	/* "temp\t" */
			"%u\t"		/* "pwr\t" */
			"%.2f\t"	/* "speed\t" */
			"%u\t"		/* "cad\t" */
			"%u\t"		/* "hr\t" */
			"%ld"		/* "ele\t" */
			"\n",
			(double)(*ck)->time / 10,
			(double)recint / 10,
			(*ck)->temp,
			(*ck)->pwr,
			(*ck)->speed,
			(*ck)->cad,
			(*ck)->hr,
			(*ck)->ele
			);
	}
}

static void progress( size_t total, size_t done, void *data )
{
	(void)data;

	fprintf( stderr, "progress: %u/%u\n", done, total );
}

bool do_fixup( srmio_data_t *srmdata, bool fixup )
{
	srmio_data_t fixed;

	if( ! fixup )
		return true;

	if( NULL == ( fixed = srmio_data_fixup( *srmdata ) )){
		fprintf( stderr, "srmio_data_fixup failed: %s\n",
			strerror(errno));
		return false;
	}

	srmio_data_free( *srmdata );
	*srmdata = fixed;
	return true;
}

bool write_files( srmio_data_t srmdata, bool fixup, char *fname,
	srmio_ftype_t type, srmio_time_t split )
{

	if( ! srmdata->cused ){
		fprintf( stderr, "no data available\n" );
		return false;
	}

	if( split == 0 ){
		FILE *fh;

		if( ! do_fixup( &srmdata, fixup ))
			return false;

		if( NULL == ( fh = fopen( fname, "w" ) )){
			fprintf( stderr, "fopen(%s) failed: %s\n",
				fname, strerror(errno) );
			return false;
		}

		if( ! srmio_file_ftype_write( srmdata, type, fh ) ){
			fprintf( stderr, "srmio_file_write(%s) failed: %s\n",
				fname, strerror(errno) );
			return false;
		}

		fclose( fh );
		return true;

	} else {
		srmio_data_t *dat, *list;
		char *match;
		int suffixlen;
		char *nfname;

		if( NULL == (nfname = strdup(fname )))
			return false;

		if( NULL == (match = strrchr( fname, 'X' ) )){
			errno = EINVAL;
			return false;
		}
		suffixlen = strlen(fname);
		suffixlen -= (match - fname) +1;

		if( NULL == ( list = srmio_data_split( srmdata, split, 500)))
			return false;

		for( dat = list; *dat; ++dat ){
			int fd;
			FILE *fh;

			/* TODO: make min chunks per file configurable */
			if( (*dat)->cused < 5 ){
				srmio_data_free( *dat );
				continue;
			}

			if( ! do_fixup( dat, fixup ))
				return false;

			strcpy( nfname, fname );
			if( 0 > ( fd = mkstemps( nfname, suffixlen ))){
				fprintf( stderr, "mkstemps(%s) failed: %s\n",
					nfname, strerror(errno) );
				return false;
			}

			if( NULL == ( fh = fdopen( fd, "w" ) )){
				fprintf( stderr, "fdopen failed: %s\n",
					strerror(errno) );
				return false;
			}

			if( ! srmio_file_ftype_write( *dat, type, fh ) ){
				fprintf( stderr, "srmio_file_write(%s) failed: %s\n",
					nfname, strerror(errno) );
				return false;
			}

			fclose( fh );

			printf( "%s\n", nfname );
			srmio_data_free( *dat );
		}

		free( nfname );
		free( list );
	}

	return true;
}


static void usage( char *name );

int main( int argc, char **argv )
{
	char *fname = NULL;
	int opt_all = 0;
	srmio_io_baudrate_t opt_baud = srmio_io_baud_max;
	int opt_clear = 0;
	int opt_date = 0;
	int opt_fixup = 0;
	int opt_get = 0;
	int opt_help = 0;
	int opt_int = 0;
	int opt_name = 0;
	int opt_pc = 5;
	int opt_read = 0;
	srmio_time_t opt_split = 0;
	srmio_ftype_t opt_rtype = srmio_ftype_srm7;
	int opt_time = 0;
	int opt_verb = 0;
	int opt_version = 0;
	char *opt_write = NULL;
	srmio_ftype_t opt_wtype = srmio_ftype_srm7;
	int needhelp = 0;
	struct option lopts[] = {
		{ "baud", required_argument, NULL, 'b' },
		{ "clear", no_argument, NULL, 'c' },
		{ "date", no_argument, NULL, 'd' },
		{ "fixup", no_argument, NULL, 'x' },
		{ "get", optional_argument, NULL, 'g' },
		{ "help", no_argument, NULL, 'h' },
		{ "int", required_argument, NULL, 'i' },
		{ "name", no_argument, NULL, 'n' },
		{ "pc", required_argument, NULL, 'p' },
		{ "read", no_argument, NULL, 'r' },
		{ "read-type", required_argument, NULL, 'R' },
		{ "split", required_argument, NULL, 's' },
		{ "time", no_argument, NULL, 't' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "version", no_argument, NULL, 'V' },
		{ "write", required_argument, NULL, 'w' },
		{ "write-type", required_argument, NULL, 'W' },
	};
	char c;
	srmio_io_t io;
	srmio_pc_t srm;

	/* TODO: option to filter data by timerange */
	while( -1 != ( c = getopt_long( argc, argv, "b:cdg::hi:np:rR:s:tVvw:W:x", lopts, NULL ))){
		switch(c){
		  case 'b':
			if( ! srmio_io_name2baud( atoi(optarg), &opt_baud)){
				fprintf( stderr, "invalid baud rate: : %s\n", optarg );
				++needhelp;
			}
			break;

		  case 'c':
			++opt_clear;
			break;

		  case 'd':
			++opt_date;
			break;

		  case 'g':
			++opt_get;
			if( optarg && 0 == strcmp( optarg, "all" ))
				++opt_all;
			break;

		  case 'h':
			++opt_help;
			break;

		  case 'i':
			opt_int = atoi(optarg);
			break;

		  case 'n':
			++opt_name;
			break;

		  case 'p':
			opt_pc = atoi(optarg);
			break;

		  case 'r':
			++opt_read;
			break;

		  case 'R':
			if( srmio_ftype_unknown == (
				opt_rtype = srmio_ftype_from_string( optarg)) ){

				fprintf( stderr, "invalid read file type: %s\n", optarg );
				++needhelp;
			}
			break;

		  case 's':
			opt_split = atoi(optarg);
			break;

		  case 't':
			++opt_time;
			break;

		  case 'v':
			++opt_verb;
			break;

		  case 'V':
			++opt_version;
			break;

		  case 'w':
			opt_write = optarg;
			break;

		  case 'W':
			if( srmio_ftype_unknown == (
				opt_wtype = srmio_ftype_from_string( optarg)) ){

				fprintf( stderr, "invalid write file type: %s\n", optarg );
				++needhelp;
			}
			break;

		  case 'x':
			++opt_fixup;
			break;

		  default:
			++needhelp;
		}
	}

	if( opt_help ){
		usage( argv[0] );
		exit( 0 );
	}

	if( opt_version ){
		printf( "srmcmd %s\n", PACKAGE_VERSION );
		return 0;
	}

	if( optind >= argc ){
		fprintf( stderr, "missing device/file name\n" );
		++needhelp;
	}
	fname = argv[optind];

	if( needhelp ){
		fprintf( stderr, "use %s --help for usage info\n", argv[0] );
		exit(1);
	}


	if( opt_read ){
		FILE *fh;
		srmio_data_t srmdata;

		if( NULL == (fh = fopen( fname, "r" ))){
			fprintf( stderr, "fopen(%s) failed: %s\n",
				fname, strerror(errno) );
			return 1;
		}

		if( NULL == (srmdata = srmio_file_ftype_read( opt_rtype, fh ))){
			fprintf( stderr, "srmio_file_read(%s) failed: %s\n",
				fname, strerror(errno) );
			return 1;
		}

		fclose( fh );

		if( opt_name ){
			if( ! srmdata->mused ){
				fprintf( stderr, "no data available\n" );
				return 1;
			}
			printf( "%s\n", srmdata->athlete
				? srmdata->athlete
				: "" );

		} else if( opt_date ){
			srmio_time_t start;

			if( ! srmdata->cused ){
				fprintf( stderr, "no data available\n" );
				return 1;
			}

			if( ! srmio_data_time_start( srmdata, &start ) )
				return 1;

			printf( "%.0f\n", (double)start / 10 );


		} else if( opt_write ){
			if( ! write_files( srmdata, opt_fixup, opt_write, opt_wtype, opt_split ))
				return 1;

		} else {
			if( ! do_fixup( &srmdata, opt_fixup ) )
				return 1;
			csvdump( srmdata );
		}

		srmio_data_free(srmdata);

		return 0;
	}

	if( NULL == (io = srmio_ios_new( fname ))){
		fprintf( stderr, "srmio_io_new(%s) failed: %s\n",
			fname,
			strerror(errno) );
		return 1;
	}

	if( ! srmio_io_open( io )){
		fprintf( stderr, "srmio_io_open(%s) failed: %s\n",
			fname,
			strerror(errno) );
		return 1;
	}

	switch( opt_pc ){
	  case 5:
		if( NULL == (srm = srmio_pc5_new() )){
			fprintf( stderr, "srmio_pc5_new failed: %s\n",
				strerror(errno) );
			return 1;
		}
		break;

	  case 6:
	  case 7:
		if( NULL == (srm = srmio_pc7_new() )){
			fprintf( stderr, "srmio_pc7_new failed: %s\n",
				strerror(errno) );
			return 1;
		}
		break;

	  default:
		fprintf( stderr, "invalid power control type: %d", opt_pc);
		return 1;
	}

	if( ! srmio_pc_set_device( srm, io )){
		fprintf( stderr, "srmio_pc_set_device failed: %s\n",
			strerror(errno) );
		return -1;
	}

	if( ! srmio_pc_set_baudrate( srm, opt_baud )){
		fprintf( stderr, "srmio_pc_set_baudrate failed: %s\n",
			strerror(errno) );
		return -1;
	}

	if( ! srmio_pc_open( srm ) ){
		fprintf( stderr, "srmio_pc_new failed: %s\n",
			strerror(errno) );
		return 1;
	}

	if( opt_name ){
		char *name;

		if( ! srmio_pc_cmd_get_athlete( srm, &name ) ){
			perror("srmio_pc_cmd_get_athlete failed");
			return 1;
		}
		printf( "%s\n", name );
		free( name );

	} else if( opt_get || opt_date ){
		srmio_data_t srmdata;

		if( NULL == (srmdata = srmio_data_new() ) ){
			perror( "srmio_data_new failed" );
			return 1;
		}

		if( opt_all )
			srmio_pc_set_xfer( srm, srmio_pc_xfer_type_all );

		/* get new/all chunks */
		if( ! srmio_pc_xfer_all( srm, srmdata, progress, NULL )){
			perror( "srmio_pc_xfer_all failed" );
			return 1;
		}

		if( opt_date ){
			srmio_time_t start;

			if( ! srmdata->cused ){
				fprintf( stderr, "no data available\n" );
				return 1;
			}

			if( ! srmio_data_time_start( srmdata, &start ) )
				return 1;

			printf( "%.0f\n", (double)start / 10 );

		} else if( opt_write ){
			if( ! write_files( srmdata, opt_fixup, opt_write, opt_wtype, opt_split ))
				return 1;

		} else {
			if( ! do_fixup( &srmdata, opt_fixup ) )
				return 1;
			csvdump( srmdata );
		}
		srmio_data_free( srmdata );

	}

	if( opt_clear ){
		if( ! srmio_pc_cmd_clear( srm ) ){
			perror( "srmio_pc_cmd_clear failed" );
			return 1;
		}
	}

	if( opt_int ){
		if( ! srmio_pc_cmd_set_recint( srm, opt_int ) ){
			perror( "srmio_pc_cmd_set_recint failed" );
			return 1;
		}
	}

	if( opt_time ){
		time_t now;
		struct tm *nows;

		time( &now );
		nows = localtime( &now );
		if( ! srmio_pc_cmd_set_time( srm, nows ) ){
			perror( "srmio_pc_cmd_set_time failed" );
			return 1;
		}
	}

	srmio_pc_free( srm );
	srmio_io_free( io );

	return 0;
}

static void usage( char *name )
{
	printf(
"usage: %s [options] <device_or_fname>\n"
"downloads from PowerControl or reads SRM files\n"
"\n"
"options:\n"
" --baud=<rate>|-b    use fixed baud rate instead of auto-probing\n"
" --clear|-c          clear data on SRM\n"
" --date|-d           print date of workout\n"
" --fixup|-x          try to fix time-glitches in retrieved data\n"
" --get[=all]|-g      download data from SRM and dump it to stdout\n"
" --help|-h           this cruft\n"
" --int=<interval>|-i set recording interval, 10 -> 1sec\n"
" --name              get athlete name\n"
" --read|-r           read from speciefied file instead of device\n"
" --read-type=<t>|-R  read data as specified file format\n"
" --pc=<type>|-p      power control version: 5, 6 or 7\n"
" --split=<gap>|-s    split data on gaps of specified length\n"
" --time|-t           set current time\n"
" --verbose|-v        increase verbosity\n"
" --version|-V        show version number and exit\n"
" --write=<fname>|-w  save data as specified .srm file\n"
" --write-type=<t>|-W save data with specified file format\n"
, name );
}

