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
	rm -f $(TARGET) $(OBJS)

run:
	Xephyr -screen 1260x720 :99 & \
	XEPHYR_PID=$$!; \
	trap 'kill $$XEPHYR_PID 2>/dev/null' EXIT INT TERM; \
	sleep 0.2; \
	DISPLAY=:99 ./$(TARGET)
