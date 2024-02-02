CC = clang
CFLAGS = -Wall -Werror -Wextra -Wpedantic -Wno-int-to-void-pointer-cast

all: httpserver

httpserver: httpserver.o bind.o queue.o
	$(CC) -o $@ $^ -g -pthread

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f httpserver *.o

format:
	clang-format -i -style=file *.[ch]

