#http://www.cs.colby.edu/maxwell/courses/tutorials/maketutor/

CFLAGS+= -Wall -O2 -mtune=cortex-a8 -march=armv7-a
LIBS+= -lprussdrv
PASM=/usr/bin/pasm

OBJECTS=bb-floppy.o pru-setup.o bb-floppy.bo
INCLUDES=pru-setup.h

all: bb-floppy.bin bb-floppy
cape: cape-bb-floppy-00A0.dtbo

bb-floppy: $(OBJECTS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

%.o: %.c $(INCLUDES)
	$(CC) -c -o $@ $< $(CFLAGS)

bb-floppy.bo: tests/test-motor.bin
	objcopy -B arm -I binary -O elf32-littlearm \
		--redefine-sym _binary_tests_test_motor_bin_start=_binary_motor_bin_start \
		--redefine-sym _binary_tests_test_motor_bin_end=_binary_motor_bin_end \
		--redefine-sym _binary_tests_test_motor_bin_size=_binary_motor_bin_size $< $@

%.bin: %.p
		$(PASM) -V3 -b $<

%.dtbo: %.dts
	dtc -@ -O dtb -o $@ $<

clean: rm -f *.o *.bin

.PHONY: all clean
