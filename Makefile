CFLAGS+= -Wall -O2 -mtune=cortex-a8 -march=armv7-a
LDFLAGS+= -lprussdrv
PASM=/usr/bin/pasm

all: bb-floppy.bin bb-floppy
cape: cape-bb-floppy-00A0.dtbo


%.bin: %.p
		$(PASM) -V3 -b $<
%.o: %.c
		gcc $(CFLAGS) -c -o $@ $<

%.dtbo: %.dts
	dtc -@ -O dtb -o $@ $<
