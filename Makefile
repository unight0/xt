
CC = gcc
LD = gcc
CFLAGS = -Wall -Wextra #-g -fsanitize=address
LDFLAGS = #-fsanitize=address
TARGET = xt

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm $(OBJS)

