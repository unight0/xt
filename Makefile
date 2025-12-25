
CXX = gcc
LD = gcc
CFLAGS = -g -Wall -Wextra
LDFLAGS =
TARGET = xt

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

$(OBJS): $(SRCS)
	$(CXX) $(CFLAGS) -c $<

clean:
	rm $(OBJS)

