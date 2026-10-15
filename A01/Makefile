CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic
LDLIBS = -pthread

pix: pix.c
	$(CC) $(CFLAGS) pix.c -o pix $(LDLIBS)

clean:
	rm -f pix

.PHONY: clean
