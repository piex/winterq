.PHONY: quickjs libuv clean test_threadpool

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


test_threadpool:
	$(CC) $(CFLAGS) -o test_threadpool ./tests/test_threadpool.c $(LDFLAGS) &&  \
	./test_threadpool \
		./tests/test1.js \
		./tests/test2.js \
		./tests/test3.js \
		./tests/test4.js \
		./tests/test5.js \
		./tests/test6.js 10 && \
	rm -rf ./test_threadpool
