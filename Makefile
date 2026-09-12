CC       = ~/arosbuilds/toolchain-core-x86_64/x86_64-aros-gcc
SYSROOT  = --sysroot=/home/sheffler/arosbuilds/core-linux-x86_64-d/bin/linux-x86_64/AROS/Development

PROGS    = midi-info midi-send midi-recv

all: $(PROGS)

% : %.c
	$(CC) $(SYSROOT) -Wall -Wno-pointer-sign $< -o $@

clean:
	rm -f $(PROGS)

.PHONY: all clean
