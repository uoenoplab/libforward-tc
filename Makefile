CC := gcc
CFLAGS ?= -W -Wall -g
INCLUDE_DIRS := -I./include -I/root/Programs/iproute2/include
LIB_DIRS := -L/root/Programs/iproute2/lib -Wl,-rpath=/root/Programs/lproute2/lib -L$(shell pwd) -Wl,-rpath=$(shell pwd)
LIBS := -lnetlink 
DEFS := -DDEV

all: libforward-tc.so main.out

libforward-tc.so: netlink_forward.c
	$(CC) -shared -fPIC $^ $(DEFS) $(INCLUDE_DIRS) $(CLANG_FLAGS) $(LIB_DIRS) $(LIBS) -std=c11 -g -o $@

main.out: main.c libforward-tc.so
	$(CC) main.c $(INCLUDE_DIRS) $(CLANG_FLAGS) $(LIB_DIRS) -lforward-tc -o $@

#insertion_benchmark.out: insertion_benchmark.cc libforward.so
#	clang++ insertion_benchmark.cc $(INCLUDE_DIRS) $(CLANG_FLAGS) $(LIB_DIRS) -lforward -o $@

clean:
	rm -f *.o
	rm -f *.so

.PHONY: all clean
