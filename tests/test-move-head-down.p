.origin 0
.entrypoint START

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

#include "../floppy-interface.h"

START:
        LBCO r0, C4, 4, 4    // Copy 4 bytes of memory from C4 + 4 (SYSCFG_REG)
        CLR  r0.t4           // CLR Bit 4 -- Enable OCP master ports
        SBCO r0, C4, 4, 4    // Store 4 bytes of memory to C4 + 4 (SYSCFG_REG)

        CLR  PIN_DEBUG
        xor  r0, r0, r0

        // Break if PIN_READY is LOW
        // qbbc END, PIN_READY

        // Set all outputs HIGH
        SET  PIN_WRITE_DATA
        SET  PIN_WRITE_GATE
        SET  PIN_HEAD_SELECT     // Set UPPER side || LOW - LOWER SIDE
        SET  PIN_HEAD_DIR        // HIGH for decrease, LOW to increase
        SET  PIN_HEAD_STEP
        SET  PIN_DRIVE_ENABLE_MOTOR

        // Start motor
        CLR  PIN_DRIVE_ENABLE_MOTOR

        // Wait until drive is ready
        ldi  r20.w0, #500
        ldi  r20.w2, #0x0000
del5us:
        sub  r20, r20, #1
        qbne del5us, r20, #0

        // We must send two pulses, with 3 ms delay between
        ldi  r1, #2

        // STEP OUT
        SET  PIN_HEAD_DIR

do:
        CLR  PIN_HEAD_STEP

        // Min 0.8us = 800ns
        ldi  r20.w0, #80
        ldi  r20.w2, #0x0000
del800ns:
        sub  r20, r20, #1
        qbne del800ns, r20, #0

        SET  PIN_HEAD_STEP

        // Min 3ms = 3 000 000ns = 300 000 = 0x 00 04 93 e0
        ldi  r20.w0, #0x93e0
        ldi  r20.w2, #0x0004
del4ms:
        sub  r20, r20, #1
        qbne del4ms, r20, #0

        sub  r1, r1, #1
        qbne do, r1, #0


/*
        // 1 800 000 = 0x 00 1b 77 40
        ldi  r20.w0, #0x7740
        ldi  r20.w2, #0x001b
del18ms:
        sub  r20, r20, #1
        qbne del18ms, r20, #0
*/

END:
        SET  PIN_DRIVE_ENABLE_MOTOR
        MOV  r31.b0, PRU0_ARM_INTERRUPT+16
        HALT

