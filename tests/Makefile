CFLAGS+= -Wall -O2 -mtune=cortex-a8 -march=armv7-a
LDFLAGS+= -lprussdrv -ltinfo -lpanel -lmenu -lncurses
PASM=/usr/bin/pasm

all: test-motor.bin test-motor \
     test-read.bin test-read \
     test-move-head-up.bin test-move-head-up \
     test-move-head-down.bin test-move-head-down \
     test-reset-head.bin test-reset-head \
     test-ncurses

%.bin: %.p
		$(PASM) -V3 -b $<
%.o: %.c
		gcc $(CFLAGS) -c -o $@ $<

%.dtbo: %.dts
	dtc -@ -O dtb -o $@ $<
