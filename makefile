CC=gcc
CFLAGS=-Wall -pthread
LIBS=-lpthread

all: initksocket libksocket.a user1 user2

initksocket: initksocket.c ksocket.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

libksocket.a: ksocket.o
	ar rcs $@ $^

ksocket.o: ksocket.c ksocket.h
	$(CC) $(CFLAGS) -c $<

user1: user1.c libksocket.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lksocket

user2: user2.c libksocket.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lksocket

clean:
	rm -f *.o *.a initksocket user1 user2