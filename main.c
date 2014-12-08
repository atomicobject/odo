/* 
 * Copyright (c) 2014 Scott Vokes <scott.vokes@atomicobject.com>
 *  
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *  
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <ctype.h>

#include "types.h"

#define ODO_VERSION_MAJOR 0
#define ODO_VERSION_MINOR 2
#define ODO_VERSION_PATCH 2
#define ODO_AUTHOR "Scott Vokes <scott.vokes@atomicobject.com>"

/* Forward references */
static void open_counter_file(config_t *cfg);
static void close_counter_file(config_t *cfg);
static int create_new_counter_file(const char *path);
static bool check_format(const char *file);
static void increment_counter(counter_t *pc, bool print);
static void set_counter(counter_t *pc, counter_t nv, bool print);
static void print_as_decimal(counter_t c);
static void format_counter(counter_t *pc, counter_t v);

static const char *progname = NULL;

static void usage(void) {
    if (progname == NULL) { progname = "odo"; }

    fprintf(stderr, "odometer version %u.%u.%u by %s\n",
        ODO_VERSION_MAJOR, ODO_VERSION_MINOR, ODO_VERSION_PATCH,
        ODO_AUTHOR);
    fprintf(stderr,
        "usage: %s [-c | -i | -r | -s COUNT] [-p] FILE\n"
        "    -h:         print this help\n"
        "    -c:         print current counter without incrementing\n"
        "    -i:         increment the counter (default)\n"
        "    -p:         print count after update\n"
        "    -r:         reset counter to 0\n"
        "    -s COUNT:   set counter to a specific value\n",
        progname);
    exit(1);
}

static void parse_args(config_t *cfg, int argc, char **argv) {
    int a = 0;

    while ((a = getopt(argc, argv, "chprs:")) != -1) {
        switch (a) {
        case 'c':
            cfg->op = OP_CAT;       /* print current w/out update */
            break;
        case 'i':                   /* increment */
            cfg->op = OP_INC;
            break;
        case 'p':                   /* print */
            cfg->print = true;
            break;
        case 'r':                   /* reset */
            cfg->op = OP_SET;
            cfg->new_value = 0;
            break;
        case 's':                   /* set */
            cfg->op = OP_SET;
            cfg->new_value = atol(optarg);

            break;
        case 'h':                   /* fall through */
        case '?':                   /* help / bad arg */
            usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        usage();
    } else {
        cfg->path = argv[0];
    }
}

/* Open and mmap a counter file, so it can be updated atomically. */
static void open_counter_file(config_t *cfg) {
    int fd = -1;

    /* Retry until successful safe open/create. It shouldn't be possible
     * to get into an infinite loop of "Doesn't exist" / "Already
     * exists", and any other errors will exit. */
    while (fd == -1) {
        /* Try to open it as an existing file. */
        fd = open(cfg->path, O_RDWR);
        if (fd == -1) {
            if (errno == ENOENT) {  /* does not exist */
                errno = 0;
                /* Attempt to atomically create it. */
                fd = create_new_counter_file(cfg->path);
            } else {
                err(EXIT_FAILURE, "open");
            }
        }
    }
    
    /* Check file size: should match counter and '\n', or 0. */
    struct stat sbuf;
    if (fstat(fd, &sbuf) == -1) { err(EXIT_FAILURE, "stat"); }
    off_t exp_size = sizeof(counter_t) + sizeof(char);
    
    if (sbuf.st_size == 0) {    /* race on counter file initialization */
        /* monotonically ftruncate, expanding to the right size */
        if (0 != ftruncate(fd, exp_size)) {
            err(1, "ftruncate");
        }
    } else if (sbuf.st_size == (exp_size)) {
        /* size is as expected */
    } else if (sbuf.st_size != (exp_size)) {
        fprintf(stderr,
            "Unexpected size %zd, not a valid counter file.\n",
            (size_t)sbuf.st_size);
        exit(EXIT_FAILURE);
    }

    uint8_t *p = mmap(NULL, sizeof(counter_t), PROT_READ | PROT_WRITE,
        MAP_SHARED, fd, 0);
        
    if (p == MAP_FAILED) {
        err(EXIT_FAILURE, "mmap");
    } else {
        /* Set last byte to '\n'; idempotent. */
        p[exp_size - 1] = '\n';

        counter_t *cur = (counter_t *)&p[0];

        /* If not yet initialized, attempt to CAS from 0 to "0000".
         * It's fine if another process beats us to it. */
        while (*cur == 0x0) {
            counter_t new_string = 0;
            format_counter(&new_string, 0);
            if (ATOMIC_BOOL_COMPARE_AND_SWAP(cur, 0, new_string)) {
                break;
            }
        }
        
        cfg->fd = fd;
        cfg->p = p;
    }
}

static void close_counter_file(config_t *cfg) {
    int res = munmap(cfg->p, sizeof(counter_t));
    if (res == -1) {
        err(EXIT_FAILURE, "munmap");
    }
    close(cfg->fd);
}

/* Create a new counter file, initialized to the appropriate
 * number of '0's (platform-specific) and a newline.. */
static int create_new_counter_file(const char *path) {
    /* Create and open, but only if it doesn't already exist. */
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (fd == -1) {
        if (errno == EEXIST) {  /* already exists */
            errno = 0;
            return -1;          /* retry as existing file */
        }
        err(EXIT_FAILURE, "open");
    }

    return fd;
}

/* Read the current counter, which is a numeric string such as "00001230",
 * and convert it to a numeric counter. */
static void read_current_counter(counter_t *pc, counter_t *val) {
    char buf[sizeof(counter_t) + 1];
    buf[sizeof(counter_t)] = '\0';
    memcpy(buf, pc, sizeof(counter_t));

    counter_t v = (counter_t) STRTOC((const char *)pc, NULL, 10);

    /* If the byte array at *pc isn't readable as a number, abort.
     * We're prabably not reading a counter file as expected. */
    if (v == 0 && (errno == EINVAL || errno == ERANGE)) {
        err(EXIT_FAILURE, NAME_OF_STRTOC);
    }
    *val = v;
}

static bool check_format(const char *buf) {
    if (buf[sizeof(counter_t)] != '\n') {
        return false;
    }
    for (uint8_t i = 0; i < sizeof(counter_t); i++) {
        if (!isdigit(buf[i])) {
            return false;
        }
    }
    return true;
}

/* Format the counter as a padded decimal number string. */
static void format_counter(counter_t *pc, counter_t v) {
    char buf[sizeof(counter_t) + 1];
    buf[sizeof(counter_t)] = '\0';

    if ((int)sizeof(buf) < snprintf(buf, sizeof(buf), COUNTER_FORMAT, v)) {
        fprintf(stderr, "snprintf error\n");
        exit(1);
    } else {
        memcpy(pc, buf, sizeof(counter_t));
    }
}

/* Atomically increment a decimal numeric string. */
static void increment_counter(counter_t *pc, bool print) {
    for (;;) {
        counter_t c = *pc;
        counter_t old = 0;
        read_current_counter(pc, &old);
        counter_t new = old + 1;
        counter_t new_string = 0;
        format_counter(&new_string, new);

        if (ATOMIC_BOOL_COMPARE_AND_SWAP(pc, c, new_string)) {
            if (print) { print_as_decimal(new); }
            break;
        }
    }
}

/* Atomically cat the current value without updating it. */
static void cat_counter(counter_t *pc) {
    counter_t cur = 0;
    read_current_counter(pc, &cur);
    print_as_decimal(cur);
}

/* Atomically change the decimal numeric string. */
static void set_counter(counter_t *pc, counter_t nv, bool print) {
    for (;;) {
        counter_t c = *pc;
        counter_t old = 0;
        read_current_counter(pc, &old);
        counter_t new_string = 0;
        format_counter(&new_string, nv);

        if (ATOMIC_BOOL_COMPARE_AND_SWAP(pc, c, new_string)) {
            if (print) { print_as_decimal(nv); }
            break;
        }
    }
}

/* Print a counter value, which is also a decimal byte string
 * (e.g. "00001234") as a string. */
static void print_as_decimal(counter_t c) {
    char buf[sizeof(counter_t) + 1];
    memset(buf, 0, sizeof(buf));
    snprintf(buf, sizeof(buf), COUNTER_FORMAT_NO_PAD, c);
    printf("%s\n", buf);
}

int main(int argc, char **argv) {
    config_t cfg = { .op = OP_INC, };

    /* Grab program name before getopt discards it. */
    if (argc > 0) { progname = argv[0]; }

    parse_args(&cfg, argc, argv);
    open_counter_file(&cfg);
    
    if (!check_format((char *)cfg.p)) {
        fprintf(stderr, "Bad format, not a valid counter file.\n");
        exit(EXIT_FAILURE);
    }

    switch (cfg.op) {
    case OP_INC:
        increment_counter((counter_t *)cfg.p, cfg.print);
        break;
    case OP_CAT:
        cat_counter((counter_t *)cfg.p);
        break;
    case OP_SET:
        set_counter((counter_t *)cfg.p, cfg.new_value, cfg.print);
        break;
    default:
        return EXIT_FAILURE;
    }
    close_counter_file(&cfg);
    return EXIT_SUCCESS;
}
