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


START:
        LBCO    r0, C4, 4, 4    // Copy 4 bytes of memory from C4 + 4 (SYSCFG_REG)
        CLR     r0.t4           // CLR Bit 4 -- Enable OCP master ports
        SBCO    r0, C4, 4, 4    // Store 4 bytes of memory to C4 + 4 (SYSCFG_REG)

        CLR PIN_DEBUG

        // Set all outputs HIGH
        SET PIN_WRITE_DATA
        SET PIN_WRITE_GATE
        SET PIN_HEAD_SELECT     // Set UPPER side || LOW - LOWER SIDE
        SET PIN_HEAD_DIR        // HIGH for decrease, LOW to increase
        SET PIN_HEAD_STEP
        SET PIN_DRIVE_ENABLE_MOTOR

        // Start motor
        CLR PIN_DRIVE_ENABLE_MOTOR

        // Delay 1ms -    r20 =     100 000
        // Delay 10ms -   r20 =   1 000 000 - 0x 00 0F 42 40
        // Delay 100ms -  r20 =  10 000 000 - 0x 00 98 96 80
        // Delay 1000ms - r20 = 100 000 000 - 0x 05 F5 E1 00
        ldi r20.w0, #0xE100
        ldi r20.w2, #0x05F5
del5000ns1:
        sub     r20, r20, #1      // 5ns
        qbne    del5000ns1, r20, #0            // 5ns

END:
        SET PIN_DRIVE_ENABLE_MOTOR
        MOV r31.b0, PRU0_ARM_INTERRUPT+16
        HALT

