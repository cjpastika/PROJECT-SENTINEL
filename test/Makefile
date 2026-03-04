# PROJECT-SENTINEL Host Unit Tests
#
# Compiles application source files once with mocked dependencies,
# then links test suites against them.
#
# Usage: make && ./run_tests

CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -g -O0
CFLAGS  += -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
LDFLAGS  = -lm

# Test mock headers take priority, then app/hal sources
INCLUDES = -I. -I../src/app -I../src/hal -I../src

OBJS     = test_main.o \
           mock_flight_sm.o \
           app_ekf.o \
           app_ccsds.o \
           test_tmr.o \
           test_crc.o \
           test_ekf.o \
           test_fsm.o
TARGET   = run_tests

.PHONY: all clean test

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) *.o
