#ifndef ARM_INTERFACE_H
#define ARM_INTERFACE_H

#define COMMAND_OFFSET 0
#define COMMAND_SIZE   1

#define COMMAND_QUIT            (0xff)
#define COMMAND_START_MOTOR     (0x01 | 0x80)
#define COMMAND_STOP_MOTOR      (0x02 | 0x80)
#define COMMAND_READ_SECTOR     (0x03 | 0x80)
#define COMMAND_SET_HEAD_DIR    (0x04 | 0x80)
#define COMMAND_SET_HEAD_SIDE   (0x05 | 0x80)
#define COMMAND_STEP_HEAD       (0x06 | 0x80)
#define COMMAND_RESET_DRIVE     (0x07 | 0x80)
#define COMMAND_ERASE_TRACK     (0x08 | 0x80)

#ifndef __GNUC__
.struct ARM_IF
        .u8  command
        .u8  unused
        .u16 argument
        .u32 read_count
.ends
.assign ARM_IF, r28, r29, interface

#else

#include <stdint.h>

struct ARM_IF {
	uint8_t  volatile command;
	uint8_t  volatile unused;
        uint16_t volatile argument;
        uint32_t volatile read_count;
}__attribute__((packed));

#endif


#endif
