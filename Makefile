
NAME=dir_mon
OBJS=src/dir_mon.o
CFLAGS=-Wall -Wextra -I. -g -O0 -std=gnu99

$(NAME): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

dir_mon.o: src/dir_mon.c src/dir_mon.h

clean:
	rm -rf src/*.o
	rm -f $(NAME)

PHONY: run
