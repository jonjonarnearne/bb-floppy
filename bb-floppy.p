.origin 0
.entrypoint START

#define PRU0_ARM_INTERRUPT 19

#define PIN_DRIVE_ENABLE_MOTOR  r30.t4 // P9.42
#define PIN_WRITE_DATA          r30.t6 // P9.41
#define PIN_WRITE_GATE          r30.t0 // P9.31
#define PIN_HEAD_DIR            r30.t2 // P9.30
#define PIN_HEAD_STEP           r30.t1 // P9.29
#define PIN_HEAD_SELECT         r30.t3 // P9.28
#define PIN_DEBUG               r30.t5 // P9.27

// Inputs
#define PIN_TRACK_ZERO          r31.t7  // P9.25
#define PIN_READ_DATA           r31.t16 // P9.24
#define PIN_TEST                r31.t15 // P8.15
.macro INC
.mparam reg
        add reg, reg, 1
.endm
.macro DEC
.mparam reg
        sub reg, reg, 1
.endm

#define COUNTER r16
.macro RESET
        xor r16, r16, r16
.endm

        // Register r0 -> r16  == Fixed
        // Register r17 -> r29 == Temp


#define STATE_INITIAL           0
#define STATE_FIND_SYNC         1 << 0

.struct Data
        .u32 pruMem
        .u32 byteOffset 
        .u16 state
        .u8  bitCounter
        .u8  timer
.ends
.assign Data, r0, r2, RxData

#define STATUS_TRIGGER_QUIT        0xff
#define STATUS_SERVER_QUIT       0xaaaa
.struct Status
        .u8   trigger
        .u16  server
.ends
.assign Status, r3.b0, r3.b2, RxStatus

START:
        LBCO    r0, C4, 4, 4    // Copy 4 bytes of memory from C4 + 4 (SYSCFG_REG)
        CLR     r0.t4           // CLR Bit 4 -- Enable OCP master ports
        SBCO    r0, C4, 4, 4    // Store 4 bytes of memory to C4 + 4 (SYSCFG_REG)

        XOR r0, r0, r0

        CLR PIN_DEBUG

        SET PIN_WRITE_DATA
        SET PIN_WRITE_GATE
        SET PIN_HEAD_SELECT     // Set UPPER side || LOW - LOWER SIDE
        SET PIN_HEAD_DIR        // HIGH for decrease, LOW to increase
        SET PIN_HEAD_STEP
        SET PIN_DRIVE_ENABLE_MOTOR

        xor RxData.pruMem, RxData.pruMem, RxData.pruMem
        // Set state to INITIAL
        xor RxData.state, RxData.state, RxData.state

        CLR PIN_DRIVE_ENABLE_MOTOR

        jal r17, waitForHi
        jal r17, initWaitForLo
        ldi RxData.state, STATE_FIND_SYNC

main:
        QBEQ callFindSync, RxData.state, STATE_FIND_SYNC

        lbbo RxStatus.trigger, RxData.pruMem, OFFSET(RxStatus.trigger), SIZE(RxStatus.trigger)
        QBEQ END, RxStatus.trigger, STATUS_TRIGGER_QUIT
        jmp main

callFindSync:
        jal r17, findSync
        ldi RxData.state, 0
        jmp main
END:
        SET PIN_DRIVE_ENABLE_MOTOR

        ldi RxStatus.server, STATUS_SERVER_QUIT
        sbbo RxStatus.server, RxData.pruMem, OFFSET(RxStatus.server), SIZE(RxStatus.server)

        MOV r31.b0, PRU0_ARM_INTERRUPT+16

        HALT

waitForHi:
        lbbo RxStatus.trigger, RxData.pruMem, OFFSET(RxStatus.trigger), SIZE(RxStatus.trigger)
        QBEQ END, RxStatus.trigger, STATUS_TRIGGER_QUIT
        QBBC waitForHi, PIN_READ_DATA
        jmp r17

initWaitForLo:
        lbbo RxStatus.trigger, RxData.pruMem, OFFSET(RxStatus.trigger), SIZE(RxStatus.trigger)
        QBEQ END, RxStatus.trigger, STATUS_TRIGGER_QUIT
        QBBS initWaitForLo, PIN_READ_DATA
        jmp r17

findSync:
        jmp r17

/*       
        ldi r10.w0, #0x200      // Byte-storage offset
        ldi r10.w2, #0          //

//        ldi r15.w0, 0x1900     // Read one full track!
//        ldi r15.w2, 0
        ldi r25.w0, 0x0000
        ldi r25.w2, 0x0001      // 0x10000
        lbbo r15, r25, 0, 4     // Get readlen from host!

        xor r14, r14, r14       // BitCounter
        //ldi r15.w0, 1024+56     // Raw Bytes in sector Excl. SyncWords (0x5555 0x5555 0x4489 0x4489)
        xor r16, r16, r16       // Counter
        xor r19, r19, r19       // State
        xor r20, r20, r20       // Curent haystack
        xor r27, r27, r27
        xor r28, r28, r28
        ldi r29.w0, 0x0000
        ldi r29.w2, 0x8000
        ldi r2.w0, 0x0000
        ldi r2.w2, 0xc000

        ldi r19.b0, 1
state:
        // Find 1st
        ldi r21.w0, 0x5555
        ldi r21.w2, 0x5555
        QBGT wait_for_lo, r19, #1 // r19 < 1
        
        // Find 2nd
        xor r21, r21, r21
        ldi r21.w0, 0x4489
        ldi r21.w2, 0x4489
        QBGT wait_for_lo, r19, #2 // r19 < 2

        // Find Data
        xor r21, r21, r21

        // READ BITS FROM DRIVE
wait_for_lo:
        // Inc Counter
        INC r16
        lbbo r1.b0, r0, 0, 1
        QBEQ END, r1.b0, #0xff
        QBBS wait_for_lo, PIN_READ_DATA

        //sbbo r16, r0, 4, 4
        
        QBGT short, r16, #140   // r16 < 140
        QBGT med, r16, #190     // r16 < 190 
long:
        // 0b0001 - 4 bits
        and r27, r29, r20       // r27 = 0x80000000 & r20
        lsr r27, r27, 31        // r27 >>= 31
        lsl r28, r28, 1
        or  r28, r28, r27
        
        lsl r20, r20, 1
        add r14, r14, 1
med:
        // 0b001 - 3 bits
        and r27, r29, r20       // r27 = 0x80000000 & r20
        lsr r27, r27, 31        // r27 >>= 31
        lsl r28, r28, 1
        or  r28, r28, r27

        lsl r20, r20, 1
        add r14, r14, 1
short:
        // 0b01 - 2 bits
        and r27, r2, r20       // r27 = 0xc0000000 & r20
        lsr r27, r27, 30        // r27 >>= 30
        lsl r28, r28, 2
        or  r28, r28, r27

        lsl r20, r20, 2
        add r14, r14, 2

        or r20.b0, r20.b0, 0x01
        

wait_for_hi:
        lbbo r1.b0, r0, 0, 1
        QBEQ END, r1.b0, #0xff
        QBBC wait_for_hi, PIN_READ_DATA

        // Reset the counter
        RESET

        // CHECK THE BITS WE HAVE READ

        QBEQ count, r21, r0              // Goto Bit Count check!
        // We are comparing 32bit unsigned ints
        
        QBNE wait_for_lo, r20, r21      // r21 != r20

        // Found match, give match to app, to show to user
        sbbo r28, r0, 8, 4
        sbbo r20, r0, 12, 4
        MOV r31.b0, PRU0_ARM_INTERRUPT+16

        SET PIN_DEBUG
        SET PIN_DEBUG
        CLR PIN_DEBUG
        CLR PIN_DEBUG

        // Search for next match
        INC r19
        QBGT state, r19, 3     // r19 < 3

count:   // Count bits
        QBGT wait_for_lo, r14, #16   // Bit counter = r14, if r14 >= 16, continue (Branch on r14 < 16 ie. no branch r14 >= 16)


        xor r14, r14, r14       // Reset Bit Counter

        sbbo r20.w0, r0, r10, 2  // Store one word
        add r10, r10, 2         // Inc buffer index
        sub r15, r15, 2         // Sub Byte Counter
        QBLT wait_for_lo, r15, #0       // ByteCounter r15 > 0


*/
