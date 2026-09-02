CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -fPIC -I../src -I..
LDFLAGS ?= -L.. -lutilipc -Wl,-rpath,.. -Wl,-rpath,. -lpthread -lm

TARGET = pythont-1.0v
SRC = pythont-realese-1.0v/pythont-1.0v.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
