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

.macro INC
.mparam reg
        add reg, reg, 1
.endm
.macro DEC
.mparam reg
        sub reg, reg, 1
.endm


        // Register r0 -> r16  == Fixed
        // Register r17 -> r29 == Temp


#define STATE_INITIAL           0
#define STATE_FIND_SYNC         1 << 0
#define STATE_READ_TRACK        1 << 1
#define STATE_DONE              1 << 2

.struct Data
        .u32 pruMem
        .u32 sharedMem
        .u32 byteOffset 
        .u32 dword
        .u16 dwordIndex
        .u16 state
        .u16 timer
        .u8  bitRemain
        .u8  bitCount
.ends
.assign Data, r0, r5, RxData

#define STATUS_TRIGGER_QUIT     0xff
#define STATUS_SERVER_GOT_TRACK 0x0001
#define STATUS_SERVER_QUIT      0xaaaa
.struct Status
        .u8   trigger           //0
        .u8   status            //1
        .u16  server            //2
        .u32  trackLen          //4
        .u16  timer             //8
.ends
.assign Status, r6, r8.w0, RxStatus

#define SYNC_WORD               0x4489

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

        // pruMem addr = 0x00
        xor RxData.pruMem, RxData.pruMem, RxData.pruMem
        // pruShared addr = 0x10000
        ldi RxData.sharedMem.w0, 0x0000
        ldi RxData.sharedMem.w2, 0x0001
        // Set state to INITIAL
        xor RxData.state, RxData.state, RxData.state

        // PIN_READY is low
        //qbbc END, PIN_READY

        CLR PIN_DRIVE_ENABLE_MOTOR
/*
        SET PIN_HEAD_DIR

        ldi RxStatus.timer, #500
del5000ns:
        sub     RxStatus.timer, RxStatus.timer, #1      // 5ns
        qbne    del5000ns, RxStatus.timer, #0            // 5ns

        CLR PIN_HEAD_STEP
        ldi r20.w0, #0x86a0
        ldi r20.w2, #0x0001
del5000ns1:
        sub     r20, r20, #1      // 5ns
        qbne    del5000ns1, r20, #0            // 5ns
        SET PIN_HEAD_STEP


        // Delay 150000ns = 150us
        // 1 ms = 1000us = 1000000n
        // 1 000 000 = 100 000
        //ldi r20.w0, #0x86a0
        //ldi r20.w2, #0x0001
*/

        //QBBC END, PIN_TRACK_ZERO

        jal r17, fn_waitForHi
        // READ == HI
        jal r17, fn_initWaitForLo
        // READ == LOW
        ldi RxData.state, STATE_FIND_SYNC

main:
        QBEQ callFindSync, RxData.state, STATE_FIND_SYNC
        QBEQ callReadTrack, RxData.state, STATE_READ_TRACK
        QBEQ callDone, RxData.state, STATE_DONE

        lbbo RxStatus.trigger, RxData.pruMem, OFFSET(RxStatus.trigger), SIZE(RxStatus.trigger)
        QBEQ END, RxStatus.trigger, STATUS_TRIGGER_QUIT

        jmp main

callFindSync:
        jal r17, fn_findSync
        ldi r18.b0, 0x01
        sbbo r18, RxData.pruMem, OFFSET(RxStatus.status), SIZE(RxStatus.status) 
        jmp main
callReadTrack:
        SET PIN_DEBUG
        jal r17, fn_readTrack
        CLR PIN_DEBUG
        ldi RxData.state, STATE_FIND_SYNC
        jmp main
callDone:
        sbbo RxData.bitRemain, RxData.pruMem, 100, SIZE(RxData.bitRemain)

END:
        SET PIN_DRIVE_ENABLE_MOTOR

        ldi RxStatus.server, STATUS_SERVER_QUIT
        sbbo RxStatus.server, RxData.pruMem, OFFSET(RxStatus.server), SIZE(RxStatus.server)

        MOV r31.b0, PRU0_ARM_INTERRUPT+16

        HALT

fn_waitForHi:
waitForHi:
        lbbo RxStatus.trigger, RxData.pruMem, OFFSET(RxStatus.trigger), SIZE(RxStatus.trigger)
        QBEQ END, RxStatus.trigger, STATUS_TRIGGER_QUIT
        QBBC waitForHi, PIN_READ_DATA
        jmp r17

fn_initWaitForLo:
initWaitForLo:
        lbbo RxStatus.trigger, RxData.pruMem, OFFSET(RxStatus.trigger), SIZE(RxStatus.trigger)
        QBEQ END, RxStatus.trigger, STATUS_TRIGGER_QUIT
        QBBS initWaitForLo, PIN_READ_DATA
        jmp r17

/****************************************************************************************************************/
/*      fn_findSync                                                                                             */
/*                                                                                                              */
/****************************************************************************************************************/
fn_findSync:
        xor RxData.dword, RxData.dword, RxData.dword
        xor RxData.bitRemain, RxData.bitRemain, RxData.bitRemain
        xor r18, r18, r18
        ldi r18.w0, SYNC_WORD
        ldi r18.w2, SYNC_WORD

time_to_low:
        xor RxData.timer, RxData.timer, RxData.timer

        and r19, r17, r17
        jal r17, fn_waitForHi
        and r17, r19, r19

timer:
        add  RxData.timer, RxData.timer, #1
        lbbo RxStatus.trigger, RxData.pruMem, OFFSET(RxStatus.trigger), SIZE(RxStatus.trigger)
        QBEQ END, RxStatus.trigger, STATUS_TRIGGER_QUIT
        QBBS timer, PIN_READ_DATA

        QBGT short, RxData.timer, #140   // timer < 140
        QBGT med,   RxData.timer, #190   // timer < 190 
long:
        // 0b0001 - 4 bits
        ldi RxData.bitRemain, #3
        lsl  RxData.dword, RxData.dword, 1
        QBEQ found_it, RxData.dword, r18 
med:
        // 0b001 - 3 bits
        ldi RxData.bitRemain, #2
        lsl  RxData.dword, RxData.dword, 1
        QBEQ found_it, RxData.dword, r18 
short:
        // 0b01 - 2 bits
        ldi RxData.bitRemain, #1
        lsl  RxData.dword, RxData.dword, 1
        QBEQ found_it, RxData.dword, r18 

        ldi RxData.bitRemain, #0
        lsl  RxData.dword, RxData.dword, 1
        or   RxData.dword, RxData.dword, 1
        QBEQ found_it, RxData.dword, r18 
        
        jmp time_to_low

found_it:
        ldi RxData.state, STATE_READ_TRACK
        jmp r17

/****************************************************************************************************************/
/*      fn_readTrack                                                                                            */
/*                                                                                                              */
/****************************************************************************************************************/
fn_readTrack:
        lbbo RxStatus.trackLen, RxData.pruMem, OFFSET(RxStatus.trackLen), SIZE(RxStatus.trackLen)
        lsr  RxStatus.trackLen, RxStatus.trackLen, #2      // trackLen / 4 = Number of DWORDS
        xor  RxData.dword, RxData.dword, RxData.dword
        xor  RxData.dwordIndex, RxData.dwordIndex, RxData.dwordIndex
        xor  RxData.bitCount, RxData.bitCount, RxData.bitCount
        xor  r20, r20, r20                              // Temp DWORD counter

rd_time_to_low:
        xor RxData.timer, RxData.timer, RxData.timer

        and  r19, r17, r17
        jal  r17, fn_waitForHi
        and  r17, r19, r19

rd_timer:
        add  RxData.timer, RxData.timer, #1
        lbbo RxStatus.trigger, RxData.pruMem, OFFSET(RxStatus.trigger), SIZE(RxStatus.trigger)
        QBEQ END, RxStatus.trigger, STATUS_TRIGGER_QUIT
        QBBS rd_timer, PIN_READ_DATA

        QBGT rd_short, RxData.timer, #140   // timer < 140
        QBGT rd_med,   RxData.timer, #190   // timer < 190 
rd_long:
        // 0b0001 - 4 bits
        ldi  RxData.bitRemain, #3
        lsl  RxData.dword, RxData.dword, 1
        add  RxData.bitCount, RxData.bitCount, #1
        QBEQ got_dword, RxData.bitCount, 32
rd_med:
        // 0b001 - 3 bits
        ldi  RxData.bitRemain, #2
        lsl  RxData.dword, RxData.dword, 1
        add  RxData.bitCount, RxData.bitCount, #1
        QBEQ got_dword, RxData.bitCount, 32
rd_short:
        // 0b01 - 2 bits
        ldi  RxData.bitRemain, #1
        lsl  RxData.dword, RxData.dword, 1
        add  RxData.bitCount, RxData.bitCount, #1
        QBEQ got_dword, RxData.bitCount, 32

        ldi  RxData.bitRemain, #0
        lsl  RxData.dword, RxData.dword, 1
        or   RxData.dword, RxData.dword, 1
        add  RxData.bitCount, RxData.bitCount, #1
        QBEQ got_dword, RxData.bitCount, 32

        jmp  rd_time_to_low

got_dword:
        sbbo RxData.dword, RxData.sharedMem, RxData.dwordIndex, SIZE(RxData.dword)
        add  RxData.dwordIndex, RxData.dwordIndex, SIZE(RxData.dword)
        add  r20, r20, #1                // Inc dword counter

        xor  RxData.dword, RxData.dword, RxData.dword
        xor  RxData.bitCount, RxData.bitCount, RxData.bitCount
        
        // Pass remaining bits on to next DWORD
        qbeq finalize, RxData.bitRemain, #0
        and  RxData.bitCount, RxData.bitRemain, RxData.bitRemain
        or   RxData.dword, RxData.dword, #1
        
finalize: 
        // Check if we have received all bytes
        qble rd_time_to_low, RxStatus.trackLen, r20   // branch if r20 <= RxStatus.trackLen

        // Inform the client that we have all bytes
        ldi RxStatus.server, STATUS_SERVER_GOT_TRACK
        sbbo RxStatus.server, RxData.pruMem, OFFSET(RxStatus.server), SIZE(RxStatus.server)
        MOV r31.b0, PRU0_ARM_INTERRUPT+16
        jmp r17
