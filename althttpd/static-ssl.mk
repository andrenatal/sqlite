# 2022-02-16:
# This makefile is used by the author (drh) to build versions of
# althttpd that are statically linked against OpenSSL.  The resulting
# binaries power sqlite.org, fossil-scm.org, and other machines.
#
# This is not a general-purpose makefile.  But perhaps you can adapt
# it to your specific needs.
#

default: althttpd
OPENSSLDIR = /home/drh/fossil/release-build/compat/openssl
OPENSSLLIB = -L$(OPENSSLDIR) -lssl -lcrypto -ldl
CC=gcc
CFLAGS = -I$(OPENSSLDIR)/include -I. -DENABLE_TLS
CFLAGS += -Os -Wall -Wextra
LIBS = $(OPENSSLLIB)

VERSION.h:	VERSION manifest manifest.uuid mkversion.c
	$(CC) -o mkversion mkversion.c
	./mkversion manifest.uuid manifest VERSION >VERSION.h

althttpd:	althttpd.c VERSION.h
	$(CC) $(CFLAGS) -o althttpd althttpd.c $(LIBS)

sqlite3.o:	sqlite3.c
	$(CC) $(CFLAGS) -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -c -o sqlite3.o sqlite3.c

logtodb:	logtodb.c sqlite3.o VERSION.h
	$(CC) $(CFLAGS) -static -o logtodb logtodb.c sqlite3.o

clean:
	rm -f althttpd VERSION.h sqlite3.o logtodb
