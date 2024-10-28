#ifndef ERR_H
#define ERR_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>

/*
    Assert that expression doesn't evaluate to -1 (as almost every system
    function does in case of error).

    Use as a function, with a semicolon, like: ASSERT_SYS_OK(close(fd));
    (This is implemented with a 'do { ... } while(0)' block so that it can be
    used between if () and else.)

    Sample output (to stderr):
    ERROR: system command failed: close(fd), in function main() in file.c line 42, errno: (9; Bad file descriptor)
*/
#define ASSERT_SYS_OK(expr)                                                    \
    do {                                                                       \
        if ((expr) < 0) {                                                      \
            syserr("system command failed: %s, in function %s() in %s line "   \
                   "%d, errno: ",                                              \
                   #expr,                                                      \
                   __func__,                                                   \
                   __FILE__,                                                   \
                   __LINE__);                                                  \
        }                                                                      \
    } while (0)

#define ASSERT_MALLOC_OK(expr)                                                 \
    do {                                                                       \
        if ((expr) == NULL) {                                                  \
            syserr("memory allocation failed: %s, in function %s() in %s "     \
                   "line %d, errno: ",                                         \
                   #expr,                                                      \
                   __func__,                                                   \
                   __FILE__,                                                   \
                   __LINE__);                                                  \
        }                                                                      \
    } while (0)

// Custom error codes.
typedef enum {
    ERRCONN,
    ERRSIZE,
    ERRTYPE,
    ERRSESSION,
    ERRPROTOCOL,
    ERRPACKETNO,
    ERRPACKETCOUNT,
    ERRIO,
    ERROLD,
    ERRTIMEOUT,
    NOERR
} error_t;

extern error_t current_error;

// Printf information about a system error and quit.
noreturn void syserr(const char* fmt, ...);

// Printf information about an error and quit.
noreturn void fatal(const char* fmt, ...);

// Printf information about an error and return.
void error(const char* fmt, ...);

// Printf debug information and return (only if DEBUG is defined).
void debug(const char* fmt, ...);

#endif
