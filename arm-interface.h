#ifndef ARM_INTERFACE_H
#define ARM_INTERFACE_H

#define COMMAND_OFFSET 0
#define COMMAND_SIZE   1

#define COMMAND_QUIT            (0xff)
#define COMMAND_QUIT_ACK        (0x7f)

#define COMMAND_START_MOTOR     (0x01 | 0x80)
#define COMMAND_START_MOTOR_ACK (0x01)
#define COMMAND_STOP_MOTOR      (0x02 | 0x80)
#define COMMAND_STOP_MOTOR_ACK  (0x02)
#define COMMAND_READ_SECTOR     (0x04 | 0x80)
#define COMMAND_READ_SECTOR_ACK (0x04)

#ifndef __GNUC__
.struct ARM_IF
        .u8  command
        .u8  unused 
        .u16 argument
        .u32 tmp
.ends
// can use r10 - r18
.assign ARM_IF, r10, r11, interface

#else

#include <stdint.h>

struct ARM_IF {
	uint8_t  volatile command;
	uint8_t  volatile unused;
	uint16_t volatile argument;
        uint32_t volatile tmp;
}__attribute__((packed));

#endif


#endif
