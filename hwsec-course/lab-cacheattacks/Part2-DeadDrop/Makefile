include ../cpu.mk

CFLAGS := -O2 -march=native -I .
TARGETS=receiver sender
UTILS=util.o

all: $(TARGETS)

$(UTILS): %.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

%.o: %.c util.h
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGETS): %: %.o util.o
	$(CC) $(CFLAGS) $^ -o $@

run_sender: sender
	taskset -c $(SENDER_CPU) ./sender

run_receiver: receiver
	taskset -c $(RECEIVER_CPU) ./receiver

.PHONY: clean

clean:
	$(RM) *.o $(TARGETS)
