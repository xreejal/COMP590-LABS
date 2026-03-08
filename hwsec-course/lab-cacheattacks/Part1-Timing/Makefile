include ../cpu.mk

CFLAGS := -O2 -march=native -I .

TARGETS = main utility.o

all: main

main: main.c utility.o
	$(CC) $(CFLAGS) $^ -o $@

utility.o: utility.c utility.h
	$(CC) $(CFLAGS) -c utility.c -o utility.o

run: main
	@taskset -c $(SENDER_CPU) ./main

clean:
	rm -f main utility.o
