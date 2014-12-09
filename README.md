odo - an atomic odometer for the command line

# Atomic Odometer? What?

odo atomically updates a count in a file, which will be created if not
present. The count is text-formatted (e.g. "00012345\n"), and will be
accurately incremented or reset even when multiple processes attempt to
change the counter at the same time. (It uses [memory mapping and atomic
compare-and-swap operations][1] to eliminate race conditions.)

[1]: https://spin.atomicobject.com/2014/11/24/odo-atomic-counters-from-the-command-line/


## Use cases

This could be used to track some intermittent event, like services being
restarted. (This was the [original inspiration][2].) Since the counter
is just a number in a text file, it's easy to compose odo with other
tools.

[2]: https://twitter.com/nrr/status/529016501421240322


## Dependencies

odo depends on atomic compare-and-swap functionality (e.g.
`__sync_bool_compare_and_swap`), which is available on most common
platforms. The build is currently tested on Linux, OpenBSD, and OSX on
x86 and x86-64 systems, as well as on a Raspberry Pi (32-bit ARM).

If the gcc-specific feature defines in `types.h` are not recognized by
your C99 compiler, you may need to set `COUNTER_SIZE` in the Makefile
yourself: `-DCOUNTER_SIZE=4` for 32-bit systems and `-DCOUNTER_SIZE=8`
for 64-bit systems.


## Getting started

To build it, just type:

    $ make

To install it:

    $ make install

To run the tests:

    $ make test


## Example Use

This atomically increments a counter in /log/restarts. If the counter
file does not exist, it is created as 0 and incremented to 1.

    $ odo /log/restarts
    
Same, but print the updated count:

    $ odo -p /log/restarts
    
Reset the count to 0:

    $ odo -r /log/restarts
    
Set the count to a number (for testing notifications, perhaps):

    $ odo -s 12345 /log/restarts

Print the current counter value without incrementing:

    $ odo -c /log/restarts

Print usage / help:

    $ odo -h


## Note

odo's atomicity is only as reliable as the underlying filesystem's.
Inconsistencies may still occur if used on a non-local filesystems
such as nfs.
