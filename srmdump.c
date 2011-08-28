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



static void usage( char *name );

int main( int argc, char **argv )
{
	char *dev = NULL;
	int opt_all = 0;
	srmio_io_baudrate_t opt_baud = srmio_io_baud_max;
	int opt_ftdi = 0;
	int opt_help = 0;
	int opt_pc = 5;
	int opt_version = 0;
	int needhelp = 0;
	struct option lopts[] = {
		{ "all", no_argument, NULL, 'a' },
		{ "baud", required_argument, NULL, 'b' },
		{ "ftdi", no_argument, NULL, 'f' },
		{ "help", no_argument, NULL, 'h' },
		{ "pc", required_argument, NULL, 'p' },
		{ "version", no_argument, NULL, 'V' },
	};
	char c;
	srmio_error_t err;
	srmio_io_t io;
	srmio_pc_t srm;
	unsigned fw_version;
	struct _srmio_pc_xfer_block_t block;
	struct _srmio_chunk_t chunk;

	while( -1 != ( c = getopt_long( argc, argv, "ab:fhp:V", lopts, NULL ))){
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

		  case 'f':
			++opt_ftdi;
			break;

		  case 'h':
			++opt_help;
			break;

		  case 'p':
			opt_pc = atoi(optarg);
			break;

		  case 'V':
			++opt_version;
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

	if( optind >= argc ){
		fprintf( stderr, "missing device/file name\n" );
		++needhelp;
	}
	dev = argv[optind];

	if( needhelp ){
		fprintf( stderr, "use %s --help for usage info\n", argv[0] );
		exit(1);
	}

	if( opt_ftdi ){
#ifdef SRMIO_HAVE_D2XX
		if( NULL == (io = srmio_d2xx_description_new( dev, &err ))){
			fprintf( stderr, "srmio_d2xx_new(%s) failed: %s\n",
				dev, err.message );
			return 1;
		}
#else
		fprintf( stderr, "ftdi support is not enabled\n" );
		return 1;
#endif

	} else {
#ifdef SRMIO_HAVE_TERMIOS
		if( NULL == (io = srmio_ios_new( dev, &err ))){
			fprintf( stderr, "srmio_ios_new(%s) failed: %s\n",
				dev, err.message );
			return 1;
		}
#else
		fprintf( stderr, "termios support is not enabled\n" );
		return 1;
#endif
	}

	if( ! srmio_io_open( io, &err )){
		fprintf( stderr, "srmio_io_open(%s) failed: %s\n",
			dev, err.message );
		return 1;
	}

	switch( opt_pc ){
	  case 5:
		if( NULL == (srm = srmio_pc5_new( &err ) )){
			fprintf( stderr, "srmio_pc5_new failed: %s\n",
				err.message );
			return 1;
		}
		break;

	  case 6:
	  case 7:
		if( NULL == (srm = srmio_pc7_new( &err ) )){
			fprintf( stderr, "srmio_pc7_new failed: %s\n",
				err.message );
			return 1;
		}
		break;

	  default:
		fprintf( stderr, "invalid power control type: %d\n", opt_pc);
		return 1;
	}
	printf( "powercontrol %d protocol is used\n", opt_pc );

	if( ! srmio_pc_set_device( srm, io, &err )){
		fprintf( stderr, "srmio_pc_set_device failed: %s\n",
			err.message );
		return 1;
	}

	if( ! srmio_pc_set_baudrate( srm, opt_baud, &err )){
		fprintf( stderr, "srmio_pc_set_baudrate failed: %s\n",
			err.message );
		return 1;
	}

	if( ! srmio_pc_open( srm, &err ) ){
		fprintf( stderr, "srmio_pc_new failed: %s\n",
			err.message );
		return 1;
	}

	if( ! srmio_pc_get_version( srm, &fw_version, &err ) ){
		fprintf( stderr, "srmio_pc_get_version failed: %s\n",
			err.message );
		return 1;
	}
	printf( "powercontrol firmware: 0x%04x\n", fw_version );

	if( opt_all )
		srmio_pc_set_xfer( srm, srmio_pc_xfer_type_all, NULL );

	if( ! srmio_pc_xfer_start( srm, &err ) ){
		fprintf( stderr, "srmio_pc_xfer_start failed: %s\n",
			err.message );
		return 1;
	}

	while( srmio_pc_xfer_block_next( srm, &block ) ){
		printf( "block\n"
			" start=%.1lf\n"
			" end=%.1lf\n"
			" recint=%.1lf\n"
			" max_progress=%u\n"
			" slope=%.1lf\n"
			" zeropos=%u\n"
			" circum=%u\n"
			" athlete=%s\n",
			0.1 * block.start,
			0.1 * block.end,
			0.1 * block.recint,
			block.total,
			block.slope,
			block.zeropos,
			block.circum,
			block.athlete );

		printf( "chunks:\n"
			"time\t"
			"dur\t"
			"temp\t"
			"pwr\t"
			"speed\t"
			"cad\t"
			"hr\t"
			"ele\n");

		while( srmio_pc_xfer_chunk_next( srm, &chunk, NULL, NULL ) ){
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
				0.1 * chunk.time,
				0.1 * chunk.dur,
				chunk.temp,
				chunk.pwr,
				chunk.speed,
				chunk.cad,
				chunk.hr,
				chunk.ele);

		}

		free( block.athlete );
		block.athlete = NULL;
	}

	if( srmio_pc_xfer_state_success != srmio_pc_xfer_status( srm, &err ) ){
		fprintf( stderr, "srmio_pc_xfer failed: %s\n",
			err.message );
		return 1;
	}

	srmio_pc_xfer_finish( srm, NULL );

	srmio_pc_free( srm );
	srmio_io_free( io );

	return 0;
}

static void usage( char *name )
{
	printf(
"usage: %s [options] <device>\n"
"downloads from PowerControl or reads SRM files\n"
"\n"
"options:\n"
" --all|-a            download 'deleted' data from SRM, as well\n"
" --baud=<rate>|-b    use fixed baud rate instead of auto-probing\n"
" --ftdi|-f           use ftdi driver\n"
" --help|-h           this cruft\n"
" --pc=<type>|-p      power control version: 5, 6 or 7\n"
" --version|-V        show version number and exit\n"
, name );
}

