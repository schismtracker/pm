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
