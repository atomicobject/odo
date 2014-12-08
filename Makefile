PROJECT =	odo
OPTIMIZE =	-O3
WARN =		-Wall -pedantic -Wextra

# Uncomment this to use 8-byte counters, if supported.
#CDEFS +=	-DCOUNTER_SIZE=8

# This is written in C99 and expects POSIX getopt.
CSTD +=		-std=c99 -D_POSIX_C_SOURCE=200112L -D_C99_SOURCE

CFLAGS +=	${CSTD} -g ${WARN} ${CDEFS} ${CINCS} ${OPTIMIZE}

all: ${PROJECT}

# Basic targets
${PROJECT}: main.o
	${CC} -o $@ main.o ${LDFLAGS}

test: ${PROJECT}
	./test_${PROJECT}

clean:
	rm -f ${PROJECT} *.o *.a *.core

main.o: types.h Makefile

# Regenerate documentation (requires ronn)
docs: man/odo.1 man/odo.1.html

man/odo.1: man/odo.1.ronn
	ronn --roff $<

man/odo.1.html: man/odo.1.ronn
	ronn --html $<

# Installation
PREFIX ?=	/usr/local
INSTALL ?=	install
RM ?=		rm
MAN_DEST ?=	${PREFIX}/share/man

install:
	${INSTALL} -c ${PROJECT} ${PREFIX}/bin
	${INSTALL} -c man/${PROJECT}.1 ${MAN_DEST}/man1/

uninstall:
	${RM} -f ${PREFIX}/bin/${PROJECT}
	${RM} -f ${MAN_DEST}/man1/${PROJECT}.1
