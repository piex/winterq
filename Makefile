.PHONY: quickjs libuv clean

CC = gcc
PWD = $(shell pwd)
QUICKJS_PATH = $(PWD)/deps/quickjs
LIBUV_PATH = $(PWD)/deps/uv

CFLAGS = -I$(QUICKJS_PATH) -I$(LIBUV_PATH)/include -Wall -O2
LDFLAGS = $(QUICKJS_PATH)/libquickjs.a $(LIBUV_PATH)/.libs/libuv.a 

# target
TARGET = winterq

# source
SRCS = main.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

quickjs:
	cd ${QUICKJS_PATH} && make

libuv:
	cd ${LIBUV_PATH} && ./autogen.sh && ./configure && make

clean:
	rm -f $(TARGET)
	cd ${QUICKJS_PATH} && make clean
	cd ${LIBUV_PATH} && make clean
