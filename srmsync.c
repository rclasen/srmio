/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

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


char *dev = NULL;
int opt_all = 0;
int opt_dup = 0;
srmio_io_baudrate_t opt_baud = srmio_io_baud_max;
int opt_fixup = 0;
int opt_ftdi = 0;
srmio_time_t opt_fuzz = 1200;
int opt_help = 0;
int opt_pc = 5;
int opt_split = 72000;
char *opt_store = NULL;
int opt_verbose = 0;
int opt_version = 0;
char *opt_write = NULL;
struct option lopts[] = {
	{ "all", no_argument, NULL, 'a' },
	{ "baud", required_argument, NULL, 'b' },
	{ "dup", no_argument, NULL, 'd' },
	{ "fixup", no_argument, NULL, 'x' },
	{ "ftdi", no_argument, NULL, 'f' },
	{ "help", no_argument, NULL, 'h' },
	{ "pc", required_argument, NULL, 'p' },
	{ "split", required_argument, NULL, 's' },
	{ "store", required_argument, NULL, 'S' },
	{ "verbose", no_argument, NULL, 'v' },
	{ "version", no_argument, NULL, 'V' },
	{ "write", required_argument, NULL, 'w' },
};

static void usage( char *name )
{
	printf(
"usage: %s [options] <device>\n"
"downloads from PowerControl or reads SRM files\n"
"\n"
"options:\n"
" --all|-a            download 'deleted' data from SRM, as well\n"
" --baud=<rate>|-b    use fixed baud rate instead of auto-probing\n"
" --dup|-d            add data to store without duplicate check\n"
" --fixup|-x          try to fix time-glitches in retrieved data\n"
" --ftdi|-f           use ftdi driver\n"
" --help|-h           this cruft\n"
" --pc=<type>|-p      power control version: 5, 6 or 7\n"
" --split=<gap>|-s    split data on gaps of specified length\n"
" --store=<dir>|-S    srmwin data directory\n"
" --verbose|-v        increase verbosity\n"
" --version|-V        show version number and exit\n"
" --write=<fname>|-w  save unsplit data as specified .wkt file\n"
, name );
}


srmio_error_t err;
srmio_io_t io;
srmio_pc_t srm;
srmio_store_t store;
srmio_data_t data;

struct _srmio_pc_xfer_block_t block;
size_t block_cnt=0, block_num = 0;
bool *want_block = NULL;
size_t prog_sum = 0, prog_prev = 0;

bool do_open()
{
	unsigned fw_version;

	if( opt_ftdi ){
#ifdef SRMIO_HAVE_D2XX
		if( NULL == (io = srmio_d2xx_description_new( dev, &err ))){
			fprintf( stderr, "srmio_d2xx_new(%s) failed: %s\n",
				dev, err.message );
			return false;
		}
#else
		fprintf( stderr, "ftdi support is not enabled\n" );
		return false;
#endif

	} else {
#ifdef SRMIO_HAVE_TERMIOS
		if( NULL == (io = srmio_ios_new( dev, &err ))){
			fprintf( stderr, "srmio_ios_new(%s) failed: %s\n",
				dev, err.message );
			return false;
		}
#else
		fprintf( stderr, "termios support is not enabled\n" );
		return false;
#endif
	}

	if( ! srmio_io_open( io, &err )){
		fprintf( stderr, "srmio_io_open(%s) failed: %s\n",
			dev, err.message );
		return false;
	}

	switch( opt_pc ){
	  case 5:
		if( NULL == (srm = srmio_pc5_new( &err ) )){
			fprintf( stderr, "srmio_pc5_new failed: %s\n",
				err.message );
			return false;
		}
		break;

	  case 6:
	  case 7:
		if( NULL == (srm = srmio_pc7_new( &err ) )){
			fprintf( stderr, "srmio_pc7_new failed: %s\n",
				err.message );
			return false;
		}
		break;

	  default:
		fprintf( stderr, "invalid power control type: %d\n", opt_pc);
		return false;
	}
	if( opt_verbose )
		fprintf( stderr, "powercontrol %d protocol is used\n", opt_pc );

	if( ! srmio_pc_set_device( srm, io, &err )){
		fprintf( stderr, "srmio_pc_set_device failed: %s\n",
			err.message );
		return false;
	}

	if( ! srmio_pc_set_baudrate( srm, opt_baud, &err )){
		fprintf( stderr, "srmio_pc_set_baudrate failed: %s\n",
			err.message );
		return false;
	}

	if( ! srmio_pc_open( srm, &err ) ){
		fprintf( stderr, "srmio_pc_new failed: %s\n",
			err.message );
		return false;
	}

	if( ! srmio_pc_get_version( srm, &fw_version, &err ) ){
		fprintf( stderr, "srmio_pc_get_version failed: %s\n",
			err.message );
		return false;
	}
	if( opt_verbose )
		fprintf( stderr, "powercontrol firmware: 0x%04x\n", fw_version );

	return true;
}

bool do_preview( void )
{
	if( NULL == ( want_block = malloc( (block_cnt +1)*sizeof(bool)))){
		fprintf( stderr, "malloc blocks failed: %s\n",
			strerror(errno));
		return false;
	}

	while( srmio_pc_xfer_block_next( srm, &block ) ){
		bool skip = false;
		time_t t = 0.1 * block.start;

		if( block_num >= block_cnt ){
			fprintf( stderr, "srm misreported number of blocks, exiting\n");
			return false;
		}

		if( ! opt_dup )
			if( ! srmio_store_have( store, block.athlete,
				block.start, opt_fuzz, &skip, &err ) ){

				fprintf( stderr, "failed to check store: %s\n",
					err.message );
				return false;
			}

		if( skip ){
			if( opt_verbose )
				fprintf( stderr, "skip block #%u - in store: %s",
					block_num, ctime(&t) );
			want_block[block_num] = false;
		} else {
			if( opt_verbose )
				fprintf( stderr, "get block #%u - new: %s",
					block_num, ctime(&t) );
			prog_sum += block.total;
			want_block[block_num] = true;
		}

		block_num++;
		free( block.athlete );
		block.athlete = NULL;
	}

	srmio_pc_xfer_finish( srm, NULL );

	if( ! srmio_pc_xfer_start( srm, &err ) ){
		fprintf( stderr, "srmio_pc_xfer_start failed: %s\n",
			err.message );
		return false;
	}

	return true;
}

bool do_fixup( srmio_data_t *srmdata )
{
	srmio_data_t fixed;

	if( ! opt_fixup )
		return true;

	if( NULL == ( fixed = srmio_data_fixup( *srmdata, &err ) )){
		fprintf( stderr, "srmio_data_fixup failed: %s\n",
			err.message );
		return false;
	}

	srmio_data_free( *srmdata );
	*srmdata = fixed;
	return true;
}

bool save_data( void )
{
	srmio_data_t *list, *dat;

	if( ! data->cused )
		return true;

	if( opt_write ){
		FILE *fh;

		if( NULL == (fh = fopen( opt_write, "w" ))){
			fprintf( stderr, "fopen %s failed: %s\n", opt_write,
				strerror(errno));
			return false;
		}

		if( ! srmio_file_wkt_write( data, fh, &err ) ){
			fprintf( stderr, "srmio_file_wkt_write: %s\n",
				err.message );
			return false;
		}
		fclose( fh );
	}

	if( NULL == ( list = srmio_data_split( data, opt_split, 500, &err))){
		fprintf( stderr, "split failed: %s\n",
			err.message );
		return false;
	}

	for( dat = list; *dat; ++dat ){
		srmio_time_t start;
		bool skip = false;

		/* TODO: make min chunks per file configurable */
		if( (*dat)->cused < 5 ){
			srmio_data_free( *dat );
			continue;
		}

		if( ! do_fixup( dat ))
			return false;

		if( ! srmio_data_time_start( *dat, &start, &err)){
			fprintf( stderr, "data_time_start failed: %s\n",
				err.message );
			return false;
		}

		if( ! opt_dup ){
			if( ! srmio_store_have( store, (*dat)->athlete,
				start, opt_fuzz, &skip, &err ) ){

				fprintf( stderr, "store_have failed: %s\n",
					err.message );
				return false;
			}
		}

		if( skip ){
			if( opt_verbose ){
				time_t t = 0.1 * start;

				fprintf( stderr, "skip data - in store: %s",
					ctime( &t ) );
			}

		} else {
			char *fname = NULL;

			if( ! srmio_store_add( store, *dat, &fname, &err )){
				fprintf( stderr, "store_add failed: %s\n",
					err.message );
				return false;
			}

			printf( "%s\n", fname );
			free( fname );
		}

		srmio_data_free( *dat );
	}

	free( list );

	return true;
}



int main( int argc, char **argv )
{
	int needhelp = 0;
	char c;
	size_t done_chunks = 0;
	int mfirst = -1;

	struct _srmio_chunk_t chunk;

	while( -1 != ( c = getopt_long( argc, argv, "ab:df:hp:S:s:Vvw:x", lopts, NULL ))){
		switch(c){
		  case 'a':
			++opt_all;
			break;

		  case 'b':
			if( ! srmio_io_name2baud( atoi(optarg), &opt_baud)){
				fprintf( stderr, "invalid baud rate: : %s\n", optarg );
				++needhelp;
			}
			break;

		  case 'd':
			++opt_dup;
			break;

		  case 'f':
			++opt_ftdi;
			break;

		  case 'h':
			++opt_help;
			break;

		  case 'p':
			opt_pc = atoi(optarg);
			break;

		  case 'S':
			opt_store = optarg;
			break;

		  case 's':
			opt_split = atoi(optarg);
			break;

		  case 'V':
			++opt_version;
			break;

		  case 'v':
			++opt_verbose;
			break;

		  case 'w':
			opt_write = optarg;
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
		printf( "srmdump %s\n", PACKAGE_VERSION );
		return 0;
	}

	if( ! opt_store ){
		fprintf( stderr, "missing store path\n" );
		++needhelp;
	}

	if( optind >= argc ){
		fprintf( stderr, "missing device/file name\n" );
		++needhelp;
	}
	dev = argv[optind];

	if( needhelp ){
		fprintf( stderr, "use %s --help for usage info\n", argv[0] );
		exit(1);
	}

	if( NULL == ( store = srmio_store_new( opt_store, &err ))){
		fprintf( stderr, "srmio_store_new(%s) failed: %s\n",
			opt_store, err.message );
		return 1;
	}

	if( NULL == ( data = srmio_data_new( &err ))){
		fprintf( stderr, "srmio_data_new failed: %s\n",
			err.message );
		return 1;
	}

	if( ! do_open() )
		return 1;

	if( opt_all )
		srmio_pc_set_xfer( srm, srmio_pc_xfer_type_all, NULL );

	if( ! srmio_pc_xfer_start( srm, &err ) ){
		fprintf( stderr, "srmio_pc_xfer_start failed: %s\n",
			err.message );
		return 1;
	}

	if( ! srmio_pc_xfer_get_blocks( srm, &block_cnt, &err )){
		fprintf( stderr, "srmio_pc_xfer_get_blocks failed: %s\n",
			err.message );
		return 1;
	}

	/* check which data to download */

	if( srmio_pc_can_preview( srm ) ){
		if( ! do_preview() )
			return 1;

	}

	/* download data */

	block_num = 0;
	while( srmio_pc_xfer_block_next( srm, &block ) ){
		bool is_first, is_int;
		size_t prog_total;

		if( prog_sum ){
			prog_total = prog_sum;

		} else if( block_cnt == 1 ){
			prog_total = block.total +1;

		} else {
			prog_total = block_cnt * 1000;
		}

		if( ! want_block || block_num > block_cnt
			|| want_block[block_num] ){

			data->slope = block.slope;
			data->zeropos = block.zeropos;
			data->circum = block.circum;
			if( block.athlete ){
				if( data->athlete )
					free( data->athlete );
				data->athlete = strdup( block.athlete );
			}

			while( srmio_pc_xfer_chunk_next( srm, &chunk, &is_int, &is_first ) ){
				if( opt_verbose && 0 == done_chunks % 16 ){
					size_t block_done = 0;

					srmio_pc_xfer_block_progress( srm, &block_done );
					if( prog_sum ){
						block_done += prog_prev;

					} else if( block_cnt == 1 ){
						/* unchanged */

					} else {
						block_done = (double)block_num * 1000 + 1000 *
							block.total / block_done;
					}

					fprintf( stderr, "progress: %u/%u\r",
						block_done, prog_total);

				}

				if( ! srmio_data_add_chunk( data, &chunk, &err ) ){
					fprintf( stderr, "add chunk failed: %s",
						err.message );
					return 1;
				}

				++done_chunks;

				/* finish previous marker */
				if( mfirst >= 0 && ( ! is_int || is_first) ){
					if( ! srmio_data_add_marker( data, mfirst,
						data->cused -1, &err ) ){

						fprintf( stderr, "failed to add marker: %s",
							err.message );
						return 1;
					}
				}

				/* start marker */
				if( is_first ){
					mfirst = (int)data->cused;

				} else if( ! is_int ){
					mfirst = -1;

				}

			}

			/* finalize marker at block end */
			if( mfirst >= 0 ){
				if( ! srmio_data_add_marker( data, mfirst,
					data->cused -1, &err ) ){

					fprintf( stderr, "failed to add marker: %s",
						err.message );
					return 1;
				}

				mfirst = -1;
			}

			if( prog_sum )
				prog_prev += block.total;
			else
				prog_prev += 1000;

		}

		block_num++;
		free( block.athlete );
		block.athlete = NULL;
	}

	if( srmio_pc_xfer_state_success != srmio_pc_xfer_status( srm, &err ) ){
		fprintf( stderr, "srmio_pc_xfer failed: %s\n",
			err.message );
		return 1;
	}

	srmio_pc_xfer_finish( srm, NULL );

	/* split data, save */
	if( ! save_data() )
		return 1;

	srmio_data_free( data );
	srmio_pc_free( srm );
	srmio_io_free( io );
	srmio_store_free( store );

	return 0;
}

