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
	int opt_help = 0;
	int opt_pc = 5;
	int opt_version = 0;
	int needhelp = 0;
	struct option lopts[] = {
		{ "all", no_argument, NULL, 'a' },
		{ "baud", required_argument, NULL, 'b' },
		{ "help", no_argument, NULL, 'h' },
		{ "pc", required_argument, NULL, 'p' },
		{ "version", no_argument, NULL, 'V' },
	};
	char c;
	srmio_io_t io;
	srmio_pc_t srm;
	unsigned fw_version;
	struct _srmio_pc_xfer_block_t block;
	struct _srmio_chunk_t chunk;

	block.athlete = NULL;

	while( -1 != ( c = getopt_long( argc, argv, "b:g::hp:V", lopts, NULL ))){
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

	if( NULL == (io = srmio_ios_new( dev ))){
		fprintf( stderr, "srmio_io_new(%s) failed: %s\n",
			dev,
			strerror(errno) );
		return 1;
	}

	if( ! srmio_io_open( io )){
		fprintf( stderr, "srmio_io_open(%s) failed: %s\n",
			dev,
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
	printf( "powercontrol %d protocol is used\n", opt_pc );

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

	if( ! srmio_pc_get_version( srm, &fw_version ) ){
		fprintf( stderr, "srmio_pc_get_version failed: %s\n",
			strerror(errno) );
		return 1;
	}
	printf( "powercontrol firmware: 0x%04x\n", fw_version );

	if( opt_all )
		srmio_pc_set_xfer( srm, srmio_pc_xfer_type_all );

	if( ! srmio_pc_xfer_start( srm ) ){
		fprintf( stderr, "srmio_pc_xfer_start failed: %s\n",
			strerror(errno) );
		return 1;
	}

	while( srmio_pc_xfer_block_next( srm, &block ) ){
		printf( "block\n"
			" start=%.1lf\n"
			" recint=%.1lf\n"
			" max_progress=%u\n"
			" slope=%.1lf\n"
			" zeropos=%u\n"
			" circum=%u\n"
			" athlete=%s\n",
			0.1 * block.start,
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

		while( srmio_pc_xfer_chunk_next( srm, &chunk, NULL, NULL  ) ){
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
	}

	if( srmio_pc_xfer_state_success != srmio_pc_xfer_status( srm, NULL) ){
		fprintf( stderr, "srmio_pc_xfer failed\n" );
		return 1;
	}

	srmio_pc_xfer_finish( srm );

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
" --all               download data from SRM and dump it to stdout\n"
" --help|-h           this cruft\n"
" --pc=<type>|-p      power control version: 5, 6 or 7\n"
" --version|-V        show version number and exit\n"
, name );
}

