CC=gcc
CFLAGS=-Wall -Wextra -g
TARGET=city_manager

all: $(TARGET)

$(TARGET): city_manager.c
	$(CC) $(CFLAGS) -O $(TARGET) city_manager.c

clean:
	rm -f $(TARGET)
	rm -f active_reports-*

.PHONY: all clean
