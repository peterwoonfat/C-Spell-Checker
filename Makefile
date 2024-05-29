CC = gcc
CFLAGS = -Wall -std=c11 -g

A3checker: spellchecker.c
	$(CC) -o A3checker spellchecker.c -lpthread $(CFLAGS)

clean:
	rm A3checker