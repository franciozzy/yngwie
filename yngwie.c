/*
 * -----------------------------------------
 *  Random I/O Performance Measurement Tool 
 * -----------------------------------------
 *  yngwie.c
 * -----------
 *  Copyright 2011 (c) Felipe Franciosi <felipe@paradoxo.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Read the README file for the changelog and information on how to
 * compile and use this program.
 */

// Global definitions (don't mess with those)
#define	_GNU_SOURCE
#define	_FILE_OFFSET_BITS	64
#define	IO_OP_READ		1
#define	IO_OP_WRITE		2
#define	IO_OP_INFO		3
#define	IO_OP_DEFAULT		IO_OP_INFO
#define MT_PROGNAME		"Random I/O Performance Measurement Tool"
#define MT_PROGNAME_LEN		strlen(MT_PROGNAME)
#ifdef	CLOCK_MONOTONIC_RAW
#define	MT_CLOCK		CLOCK_MONOTONIC_RAW
#else
#define	MT_CLOCK		CLOCK_MONOTONIC
#endif

// Header files
#include <stdio.h>			// printf, snprintf, fprintf, perror
#include <unistd.h>			// getopt, lseek64, close, getpagesize
#include <string.h>			// strlen, memset
#include <sys/types.h>			// open, lseek64
#include <sys/stat.h>			// open
#include <fcntl.h>			// open
#include <stdlib.h>			// posix_memalign, free, strtou*
#include <inttypes.h>			// PRId64
#include <time.h>			// clock_gettime

// Global default definitions
#define	MT_OFFSET	0		// Default offset at start
#define	MT_BUFSIZE	1024*1024	// Default I/O operations to 1MiB
#define	MT_COUNT	1		// Default counter
#define	MT_WZERO	1		// Default write behaviour
#define	MT_SEEKC	0		// Default seek iteration behaviour
#define	MT_OSYNC	0		// Default usage of O_SYNC flag upon open()

// Auxiliary functions
void usage(char *argv0){
	// Local variables
	int		i;		// Temporary integer

	// Print usage
	for (i=0; i<MT_PROGNAME_LEN; i++) fprintf(stderr, "-");
	fprintf(stderr, "\n%s\n", MT_PROGNAME);
	for (i=0; i<MT_PROGNAME_LEN; i++) fprintf(stderr, "-");
	fprintf(stderr, "\nUsage: %s < -r | -w | -i > < -d dev_name > [ -hysv[v] ] [ -b buf_size ] [ -c count ] [ -o offset ]\n", argv0);
	fprintf(stderr, "       -r | -w | -i     Read from, write to or print info of the device. (default is -i)\n");
	fprintf(stderr, "       -d dev_name      Specify block device to operate on.\n");
	fprintf(stderr, "                        !!WARNING!! Be careful when using -w, as the device will be overwritten.\n");
	fprintf(stderr, "       -h               Print this help message and quit.\n");
//	fprintf(stderr, "       -z               When writing, write only zeros instead of random data (default is random).\n");
	fprintf(stderr, "       -v               Increase verbose level (may be used multiple times).\n");
	fprintf(stderr, "       -b buf_size      Specify different buffer size (in bytes, default=%d).\n", MT_BUFSIZE);
	fprintf(stderr, "       -c count         Repeat the operation <count> times (default=1).\n");
	fprintf(stderr, "       -s               Seek back to offset before next count, averaging results.\n");
	fprintf(stderr, "       -y               Open device with O_SYNC (see open(2) man page).\n");
	fprintf(stderr, "       -o offset        Starts to operate on device at <offset> (default=%d).\n", MT_OFFSET);
}

// Main function
int main(int argc, char **argv){
	// Local variables

	// IO related
	char		*buf = NULL;	// IO Buffer
	off_t		bufsize = -1;	// IO Buffer size
	unsigned long	count = -1;	// Number of times to exec operation
	unsigned long	count_i;	// Count iterator
	off_t		offset = -1;	// IO Offset
	char		io_op = -1;	// IO Operation
	char		wzero = -1;	// Write zeros instead of random data
	char		seekc = -1;	// Seek back to offset before next count
	char		osync = -1;	// Open device with O_SYNC

	// Block device related
	char		*bdevfn = NULL;	// Block device file name
	int		bdevfd = -1;	// Block device file descriptor
	int		bdflags;	// Block device open flags
	off_t		bdevsize;	// Block device size

	// System related
	int		psize;		// System pagesize
	int		err = 0;	// Return code

	// Summary related
	ssize_t		bytesioed;	// Number of bytes io'ed (each io)
	u_int64_t	timeacc;	// Time accumulator for grouping
	struct timespec	st1;		// Start time
	struct timespec	st2;		// Finish time

	// General
	int		verbose = 0;	// Verbose level
	int		i,j;		// Temporary integers

	// Fetch arguments
	while ((i = getopt(argc, argv, "hvd:b:c:o:rwizys")) != -1){
		switch (i){
			case 'h':
				// Print help
				usage(argv[0]);
				goto out;

			case 'r':
				// Set io_op to read, if unset
				if (io_op != -1){
					fprintf(stderr, "%s: Invalid argument \"-r\", operation mode already set.\n", argv[0]);
					err = 1;
					goto out;
				}
				io_op = IO_OP_READ;
				break;

			case 'w':
				// Set io_op to write, if unset
				if (io_op != -1){
					fprintf(stderr, "%s: Invalid argument \"-w\", operation mode already set.\n", argv[0]);
					err = 1;
					goto out;
				}
				io_op = IO_OP_WRITE;
				break;

			case 'i':
				// Set io_op to info, if unset
				if (io_op != -1){
					fprintf(stderr, "%s: Invalid argument \"-i\", operation mode already set.\n", argv[0]);
					err = 1;
					goto out;
				}
				io_op = IO_OP_INFO;
				break;

			case 'z':
				// Set write to zeros
				if (wzero != -1){
					fprintf(stderr, "%s: Invalid argument \"-z\", already set to write zeros.\n", argv[0]);
					err = 1;
					goto out;
				}
				wzero = 1;
				break;

			case 's':
				// Set to seek back before next count
				if (seekc != -1){
					fprintf(stderr, "%s: Invalid argument \"-s\", already specified.\n", argv[0]);
					err = 1;
					goto out;
				}
				seekc = 1;
				break;

			case 'v':
				// Increase verbose level
				verbose++;
				break;

			case 'd':
				// Set a different device
				if (bdevfn){
					fprintf(stderr, "%s: Invalid argument \"-d\", block device already set.\n", argv[0]);
					err = 1;
					goto out;
				}
				if ((bdevfn = (char *)malloc(strlen(optarg)+1)) == NULL){
					perror("malloc");
					fprintf(stderr, "%s: Error allocating memory for block device file name.\n", argv[0]);
					err = 1;
					goto out;
				}
				sprintf(bdevfn, "%s", optarg);
				break;

			case 'b':
				// Set a different buffer size
				if (bufsize != -1){
					fprintf(stderr, "%s: Invalid argument \"-b\", buffer size already set to %"PRId64" bytes.\n", argv[0], bufsize);
					err = 1;
					goto out;
				}
#if __WORDSIZE == 64
				bufsize = strtol(optarg, NULL, 10);
#elif __WORDSIZE == 32
				bufsize = strtoll(optarg, NULL, 10);
#else
#error __WORDSIZE NOT DEFINED
#endif
				if (bufsize < 1){
					fprintf(stderr, "%s: Invalid buffer size specified with \"-b\" (%"PRId64"). Must be a positive integer.\n", argv[0], bufsize);
					err = 1;
					goto out;
				}
				break;

			case 'c':
				// Set counter
				if (count != -1){
					fprintf(stderr, "%s: Invalid argument \"-c\", counter already specified to %lu times.\n", argv[0], count);
					err = 1;
					goto out;
				}
				count = strtoul(optarg, NULL, 10);

				if (count == 0){
					fprintf(stderr, "%s: Invalid counter, must be greater than zero.\n", argv[0]);
					err = 1;
					goto out;
				}
				break;

			case 'o':
				// Set offset
				if (offset != -1){
					fprintf(stderr, "%s: Invalid argument \"-o\", offset already set to %"PRId64" bytes.\n", argv[0], offset);
					err = 1;
					goto out;
				}
#if __WORDSIZE == 64
				offset = strtol(optarg, NULL, 10);
#elif __WORDSIZE == 32
				offset = strtoll(optarg, NULL, 10);
#else
#error __WORDSIZE NOT DEFINED
#endif
				if (offset < 0){
					fprintf(stderr, "%s: Invalid offset specified with \"-o\" (%"PRId64"). Cannot be a negative number.\n", argv[0], offset);
					err = 1;
					goto out;
				}
				break;

			case 'y':
				// Set usage of O_SYNC
				if (osync != -1){
					fprintf(stderr, "%s: Invalid argument \"-y\", usage of O_SYNC already set.\n", argv[0]);
					err = 1;
					goto out;
				}
				osync = 1;
				break;
		}
	}

	// If wzero has been turned on, check if writing
	if ((wzero == 1) && (io_op != IO_OP_WRITE)){
		fprintf(stderr, "%s: Error, \"-z\" can only be used with \"-w\". Use -h for further details.\n", argv[0]);
		err = 1;
		goto out;
	}

	// Set defaults
	if (bufsize == -1){
		bufsize = MT_BUFSIZE;
	}
	if (count == -1){
		count = MT_COUNT;
	}
	if (offset == -1){
		offset = MT_OFFSET;
	}
	if (io_op == -1){
		io_op = IO_OP_DEFAULT;
	}
	if (wzero == -1){
		wzero = MT_WZERO;
	}
	if (seekc == -1){
		seekc = MT_SEEKC;
	}
	if (osync == -1){
		osync = MT_OSYNC;
	}

	// Make sure a block device has been specified
	if (bdevfn == NULL){
		usage(argv[0]);
		err = 1;
		goto out;
	}

	// Open block device
	bdflags = O_RDWR | O_DIRECT | O_LARGEFILE;
	if (osync){
		bdflags |= O_SYNC;
	}
	if ((bdevfd = open(bdevfn, bdflags, 0)) < 0){
		perror("open");
		fprintf(stderr, "%s: Error opening block device \"%s\".\n", argv[0], bdevfn);
		err = 1;
		goto out;
	}

	// Move the pointer to the end of the block device (fetchs block device size)
	if ((bdevsize = lseek(bdevfd, 0, SEEK_END)) < 0){
		perror("lseek");
		fprintf(stderr, "%s: Error repositioning offset to eof.\n", argv[0]);
		err = 1;
		goto out;
	}

	// Move the pointer back to the beginning of the block device
	if (lseek(bdevfd, 0, SEEK_SET) < 0){
		perror("lseek");
		fprintf(stderr, "%s: Error repositioning offset to start.\n", argv[0]);
		err = 1;
		goto out;
	}

	// Fetch system pagesize
	psize = getpagesize();

	// Allocates rbuf according to the systems page size
	if (posix_memalign((void **)&(buf), psize, bufsize) != 0){
		//perror("posix_memalign"); // posix_memalign doesn't set errno.
		fprintf(stderr, "%s: Error malloc'ing aligned buf, %"PRId64" bytes long.\n", argv[0], bufsize);
		err = 1;
		goto out;
	}

	// Print verbose stuff
	if (verbose || io_op==IO_OP_INFO){
		for (j=0; j<MT_PROGNAME_LEN; j++) fprintf(stderr, "-");
		fprintf(stderr, "\n%s\n", MT_PROGNAME);
		for (j=0; j<MT_PROGNAME_LEN; j++) fprintf(stderr, "-");
		fprintf(stderr, "\nDevice \"%s\" has %"PRId64" bytes\n", bdevfn, bdevsize);
		fprintf(stderr, "System pagesize is %d bytes long.\n", psize);
		fprintf(stderr, "Got aligned buffer at %p.\n", buf);
		fprintf(stderr, "Buffer is %"PRId64" bytes long.\n", bufsize);
		fprintf(stderr, "Offsetting to %"PRId64" bytes.\n", offset);
		for (j=0; j<MT_PROGNAME_LEN; j++) fprintf(stderr, "-");
		fprintf(stderr, "\n");
		fflush(stderr);
	}

	// If we are just outputting info, finish here
	if (io_op == IO_OP_INFO){
		goto out;
	}

	// Initialize loop
	timeacc = 0;
	bytesioed = 0;

	// Loop
	for (count_i=0; count_i<count; count_i++){

		// Prepare buffer
		memset(buf, 0, bufsize);
		if ((io_op == IO_OP_WRITE) && (wzero == 0)){
			// Write random stuff
			// FIXME: I'm just doing zeros here for now.
		}

		// Offset
		if (((count_i == 0) || (seekc == 1)) && (lseek(bdevfd, offset, SEEK_SET) < 0)){
			perror("lseek");
			fprintf(stderr, "Error offsetting to %"PRId64" bytes.\n", offset);
			err = 1;
			goto out;
		}

		// Execute IO operation
		switch(io_op){
			case IO_OP_WRITE:
				// Start the clock
				clock_gettime(MT_CLOCK, &st1);

				// Write
				bytesioed = write(bdevfd, buf, bufsize);

				// Stop the clock
				clock_gettime(MT_CLOCK, &st2);

				// Accumulate time difference
				timeacc += (((long long unsigned)st2.tv_sec*1000000)+((long long unsigned)st2.tv_nsec/1000))-
					   (((long long unsigned)st1.tv_sec*1000000)+((long long unsigned)st1.tv_nsec/1000));
				break;

			case IO_OP_READ:
				// Start the clock
				clock_gettime(MT_CLOCK, &st1);

				// Read
				bytesioed = read(bdevfd, buf, bufsize);

				// Stop the clock
				clock_gettime(MT_CLOCK, &st2);

				// Accumulate time difference
				timeacc += (((long long unsigned)st2.tv_sec*1000000)+((long long unsigned)st2.tv_nsec/1000))-
					   (((long long unsigned)st1.tv_sec*1000000)+((long long unsigned)st1.tv_nsec/1000));

				break;
		}
	}

	// Average results if seeking back prior to count iteractions
	if (seekc == 1){
		timeacc /= count;
	}

	// Print time difference
	fprintf(stdout, "%013"PRIu64" %013lu %013zd %"PRIu64"\n", offset, (seekc==1)?1:count, bytesioed, timeacc);
	fflush(stdout);

out:
	// Free buffer
	if (buf){
		free(buf);
	}

	// Close block device
	if (bdevfd != -1){
		close(bdevfd);
	}

	// Return
	return(err);
}
