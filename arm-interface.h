#ifndef ARM_INTERFACE_H
#define ARM_INTERFACE_H

#define COMMAND_OFFSET 0
#define COMMAND_SIZE   1

#define COMMAND_QUIT            (0xff | 0x80)
#define COMMAND_QUIT_ACK        (0xff & 0x7f)

#define COMMAND_START_MOTOR     (0x01 | 0x80)
#define COMMAND_START_MOTOR_ACK (0x01)
#define COMMAND_STOP_MOTOR      (0x02 | 0x80)
#define COMMAND_STOP_MOTOR_ACK  (0x02)

#ifndef __GNUC__
.struct ARM_IF
        .u8  command
.ends
.assign ARM_IF, r10.b0, r10.b0, interface
#else

#include <stdint.h>

struct ARM_IF {
	uint8_t volatile command;
}__attribute__((packed));

#endif


#endif
