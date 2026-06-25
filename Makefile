SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

TARGET = unnamed
CC = clang
CFLAGS = -std=c99 -pedantic -Wall -Werror -O2
LDFLAGS = -lX11

all: $(TARGET)

debug: CFLAGS += -g -O0 -DDEBUG
debug: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm $(TARGET) $(OBJS)

run:
	./$(TARGET)
