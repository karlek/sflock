# sflock version
VERSION = 0.1
# slock version
SLOCK_VERSION = 0.9

# includes and libs
INCS = -I. -I/usr/include
LIBS = -L/usr/lib -lc -lcrypt -lX11 -lXext

# flags
CPPFLAGS = -DVERSION=\"${VERSION}\" -DHAVE_SHADOW_H
CFLAGS = -std=c99 -pedantic -Wall -Os ${INCS} ${CPPFLAGS}
LDFLAGS = -s ${LIBS}

# compiler and linker
CC = cc

