#http://www.cs.colby.edu/maxwell/courses/tutorials/maketutor/

CFLAGS+= -Wall -O2 -mtune=cortex-a8 -march=armv7-a
LIBS+= -lprussdrv
PASM=/usr/bin/pasm

BIN=bb-floppy
BUILD_DIR=./build
SRCS=main.c pru-setup.c read_track_timing.c list.c

FIRMWARE=$(BUILD_DIR)/firmware.bin
OBJ=$(BUILD_DIR)/firmware.o
OBJ+=$(SRCS:%.c=$(BUILD_DIR)/%.o)

$(info $$OBJ id [${OBJ}])

DEP=$(OBJ:%.o=%.d)

all: $(BIN)
cape: $(BUILD_DIR)/cape-bb-floppy-00A0.dtbo


# Default target
$(BIN) : $(BUILD_DIR)/$(BIN)

# Actual target - depends on all .o files
$(BUILD_DIR)/$(BIN) : $(OBJ)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

# Include all .d files . Created by gcc
-include $(DEP)
$(BUILD_DIR)/%.o : %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -c $< -o $@ 
	
$(BUILD_DIR)/%.bin: %.p
	$(PASM) -V3 -b $< $(patsubst %.bin,%,$@)

$(BUILD_DIR)/%.dtbo: %.dts
	dtc -@ -O dtb -o $@ $<

.PHONY: clean all

clean:
	-rm $(BUILD_DIR)/$(BIN) $(OBJ) $(DEP) $(FIRMWARE)

$(BUILD_DIR)/firmware.o: $(FIRMWARE)
	touch $(patsubst %.o,%.d,$@)			# Just to make clean stop complaining
	objcopy -B arm -I binary -O elf32-littlearm \
		--redefine-sym _binary_build_firmware_bin_start=_binary_firmware_bin_start \
		--redefine-sym _binary_build_firmware_bin_end=_binary_firmware_bin_end \
		--redefine-sym _binary_build_firmware_bin_size=_binary_firmware_bin_size $< $@


