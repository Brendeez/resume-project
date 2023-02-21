CC = gcc
CFLAGS = -Wall -pedantic -Wextra -Werror -lm
LDFLAGS =

all: carPark

carPark: manager.o simulator.o firealarm.o

main.o: manager.c

simulator.o: simulator.c

firealarm.o: firealarm.c

clean:
	rm -f carPark *.o

.PHONY: all clean
