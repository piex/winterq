CC = gcc
QUICKJS_PATH = ./quickjs
CFLAGS = -I$(QUICKJS_PATH) -Wall
LDFLAGS = $(QUICKJS_PATH)/libquickjs.a

main: main.c $(QUICKJS_PATH)/libquickjs.a
	$(CC) $(CFLAGS) -lcurl -o main main.c $(LDFLAGS)

clean:
	rm -f main
