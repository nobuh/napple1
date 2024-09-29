# napple1 makefile 
# Nobu Hatano <nobuhatano@gmail.com>
CC = gcc
CFLAGS = -Wall
# CFLAGS = -Wall -ansi
THREAD = -lpthread

# Support Linux and Cygwin
OS = ${shell uname -o}
ifeq ($(OS), Cygwin)
	CURSES = -lncurses -I/usr/include/ncurses
else
	CURSES = -lncurses 
endif

all: napple1

napple1: m6502.o memory.o pia6820.o screen.o keyboard.o main.o msgbuf.o
	$(CC) $(CFLAGS) -o ../napple1 m6502.o memory.o pia6820.o screen.o msgbuf.o keyboard.o main.o $(THREAD) $(CURSES)
m6502.o: m6502.c m6502.h pia6820.h memory.h
	$(CC) $(CFLAGS) -o m6502.o -c m6502.c $(THREAD)
memory.o: memory.c pia6820.h memory.h pia6820.h screen.h 
	$(CC) $(CFLAGS) -o memory.o -c memory.c
pia6820.o: pia6820.c pia6820.h screen.h
	$(CC) $(CFLAGS) -o pia6820.o -c pia6820.c
screen.o: screen.c 
	$(CC) $(CFLAGS) -o screen.o -c screen.c $(CURSES)
msgbuf.o: msgbuf.c screen.h memory.h
	$(CC) $(CFLAGS) -o msgbuf.o -c msgbuf.c $(CURSES)
keyboard.o: keyboard.c keyboard.h m6502.h pia6820.h memory.h screen.h
	$(CC) $(CFLAGS) -o keyboard.o -c keyboard.c 
main.o: main.c keyboard.h m6502.h screen.h msgbuf.h memory.h
	$(CC) $(CFLAGS) -o main.o  -c main.c $(CURSES)
clean: 
	-rm *.o
	-rm ../napple1

