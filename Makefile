CC=gcc
CFLAGS=-Wall
SRC=$(wildcard *.c)
LIBS=-ldl

tesh: $(SRC)
	$(CC) -g -o $@ $^ $(CFLAGS) $(LIBS)
