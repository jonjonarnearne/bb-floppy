#ifndef ARM_INTERFACE_H
#define ARM_INTERFACE_H

#define COMMAND_OFFSET 0
#define COMMAND_SIZE   1

#define COMMAND_QUIT      0xff
#define COMMAND_QUIT_ACK (0xff & 0x7f)

#ifndef __GNUC__
.struct ARM_IF
        .u8  command
.ends
.assign ARM_IF, r10.b0, r10.b0, interface
#else

#include <stdint.h>

struct ARM_IF {
	uint8_t command;
}__attribute__((packed));

#endif


#endif
