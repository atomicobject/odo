#ifndef TYPES_H
#define TYPES_H

/* If not defined, determine a  platform counter size */
#ifndef COUNTER_SIZE
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8)
#define COUNTER_SIZE 8
#elif defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4)
#define COUNTER_SIZE 4
#else
/* Use 4 by default */
#define COUNTER_SIZE 4
#endif
#endif

#define ATOMIC_BOOL_COMPARE_AND_SWAP(PTR, OLD, NEW)     \
    (__sync_bool_compare_and_swap(PTR, OLD, NEW))

#if (COUNTER_SIZE == 8)
typedef uint64_t counter_t;
#define COUNTER_FORMAT "%08llu"
#define COUNTER_FORMAT_NO_PAD "%llu"
#define STRTOC strtoll
#define NAME_OF_STRTOC "strtoll"
#elif (COUNTER_SIZE == 4)
typedef uint32_t counter_t;
#define COUNTER_FORMAT "%04u"
#define COUNTER_FORMAT_NO_PAD "%u"
#define STRTOC strtol
#define NAME_OF_STRTOC "strtol"
#endif

typedef enum { OP_INC, OP_SET, OP_CAT, } op_t;

typedef struct {
    const char *path;
    op_t op;                    /* operation */
    counter_t new_value;        /* value to set */
    bool print;
    int fd;
    uint8_t *p;                 /* raw memory */
} config_t;

#endif
