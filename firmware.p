.origin 0
.entrypoint START

#include "arm-interface.h"

#define PRU0_ARM_INTERRUPT 19

#define PIN_DRIVE_ENABLE_MOTOR  r30.t4 // P9.42
#define PIN_WRITE_DATA          r30.t6 // P9.41
#define PIN_WRITE_GATE          r30.t0 // P9.31
#define PIN_HEAD_DIR            r30.t2 // P9.30
#define PIN_HEAD_STEP           r30.t1 // P9.29
#define PIN_HEAD_SELECT         r30.t3 // P9.28

#define PIN_DEBUG               r30.t14 // P8.12

// Inputs
#define PIN_READY               r31.t5  // P9.27
#define PIN_TRACK_ZERO          r31.t7  // P9.25
#define PIN_READ_DATA           r31.t16 // P9.24

#define PIN_TEST                r31.t15 // P8.15
#define PIN_UNUSED              r31.t14 // P8.16


.struct REGS
        .u32  pruMem
.ends
.assign REGS, r0, r0, GLOBAL

START:
        lbco r0, C4, 4, 4    // Copy 4 bytes of memory from C4 + 4 (SYSCFG_REG)
        clr  r0.t4           // CLR Bit 4 -- Enable OCP master ports
        sbco r0, C4, 4, 4    // Store 4 bytes of memory to C4 + 4 (SYSCFG_REG)

        xor  GLOBAL.pruMem, GLOBAL.pruMem, GLOBAL.pruMem
        clr  PIN_DEBUG

        // Set all outputs HIGH
        set  PIN_WRITE_DATA
        set  PIN_WRITE_GATE
        set  PIN_HEAD_SELECT     // Set UPPER side || LOW - LOWER SIDE
        set  PIN_HEAD_DIR        // HIGH for decrease, LOW to increase
        set  PIN_HEAD_STEP
        set  PIN_DRIVE_ENABLE_MOTOR

        // Report that setup is done
        mov  r31.b0, PRU0_ARM_INTERRUPT+16

        clr  PIN_DRIVE_ENABLE_MOTOR

WAIT_FOR_COMMAND:
        lbbo interface, GLOBAL.pruMem, OFFSET(interface), \
                                       SIZE(interface)
        qbne WAIT_FOR_COMMAND, interface.command, COMMAND_QUIT

        set  PIN_DRIVE_ENABLE_MOTOR


END:       
        ldi  interface.command, COMMAND_QUIT_ACK
        sbbo interface.command, GLOBAL.pruMem, \
                                OFFSET(interface.command), \
                                SIZE(interface)
        mov  r31.b0, PRU0_ARM_INTERRUPT+16
        halt

