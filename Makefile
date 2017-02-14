all:segvdump

SRC := main.c \
	segv_dump.c

CFLAGS := -Wall -finstrument-functions
LIBS := -lpthread

segvdump:$(SRC)
	gcc $(CFLAGS) $^ -o $@ $(LIBS)

.PHONY:all
