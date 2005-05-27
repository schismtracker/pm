# -*- Makefile -*-

SHELL := /bin/sh
CC := gcc
RM := rm -f

CFLAGS := -Wall -W -Werror -Iinclude

#CFLAGS += -O3 -fomit-frame-pointer -funroll-loops -frerun-cse-after-loop -fno-strength-reduce
CFLAGS += -g3

all: pm
clean:
	$(RM) core.* pm *.o *~ fmt/*.o fmt/*~
pm: file.o player.o mixer.o misc.o tables.o main.o itsex.o \
fmt/669.o fmt/imf.o fmt/it.o fmt/mod.o fmt/mtm.o fmt/s3m.o fmt/sfx.o
	$(CC) -o $@ $^ -lao

# ----------------------------------------------------------------------------

file.o: file.c include/pm.h Makefile
itsex.o: itsex.c Makefile
main.o: main.c include/pm.h Makefile
misc.o: misc.c include/pm.h Makefile
mixer.o: mixer.c include/pm.h Makefile
player.o: player.c include/pm.h Makefile
tables.o: tables.c include/pm.h Makefile
