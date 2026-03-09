include ../cpu.mk

CFLAGS := -O2 -march=native -I .
TARGETS=attacker
UTILS=util.o

all: $(TARGETS)

$(UTILS): %.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

%.o: %.c util.h
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGETS): %: %.o util.o
	$(CC) $(CFLAGS) $^ -o $@

run_attacker: attacker
	taskset -c $(RECEIVER_CPU) ./attacker

run_victim-4: victim-4
	taskset -c $(SENDER_CPU) ./victim-4

run_victim-3: victim-3
	taskset -c $(SENDER_CPU) ./victim-3

run_victim-2: victim-2
	taskset -c $(SENDER_CPU) ./victim-2

.PHONY: clean run_attacker run_victim-2 run_victim-3 run_victim-4

clean:
	$(RM) *.o $(TARGETS)
