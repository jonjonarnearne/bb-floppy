.origin 0
.entrypoint START

#include "arm-interface.h"

#define PRU0_ARM_INTERRUPT 19

#define PIN_WRITE_GATE          r30.t0 // P9.31
#define PIN_HEAD_STEP           r30.t1 // P9.29
#define PIN_HEAD_DIR            r30.t2 // P9.30
#define PIN_HEAD_SELECT         r30.t3 // P9.28
#define PIN_DRIVE_ENABLE_MOTOR  r30.t4 // P9.42
#define PIN_WRITE_DATA          r30.t6 // P9.41
#define ALL_CTRL_HI             0x5f   // Set all these pins hi

#define PIN_DEBUG               r30.t14 // P8.12

// Inputs
#define PIN_READY               r31.t5  // P9.27
#define PIN_TRACK_ZERO          r31.t7  // P9.25
#define PIN_READ_DATA           r31.t16 // P9.24

#define PIN_TEST                r31.t15 // P8.15
#define PIN_UNUSED              r31.t14 // P8.16

#define SYNC_WORD               0x4489
#define SECTOR_LEN_WORD         0x1900

.macro  rclr
.mparam reg
        xor reg, reg, reg
.endm

.macro  rcp
.mparam dst, src
        and dst, src, src
.endm

.macro  inc
.mparam reg
        add reg, reg, #1
.endm

.macro  M_CHECK_ABORT
        lbbo interface.command, GLOBAL.pruMem, OFFSET(interface.command), \
                                               SIZE(interface.command)
        qbeq QUIT, interface.command, COMMAND_QUIT
.endm
.struct GREGS
        .u32  pruMem
        .u32  sharedMem
.ends
.assign GREGS, r0, r1, GLOBAL
.struct SREGS
        .u32  ret_addr
.ends
.assign SREGS, r19, r19, STACK

START:
        lbco r0, C4, 4, 4    // Copy 4 bytes of memory from C4 + 4 (SYSCFG_REG)
        clr  r0.t4           // CLR Bit 4 -- Enable OCP master ports
        sbco r0, C4, 4, 4    // Store 4 bytes of memory to C4 + 4 (SYSCFG_REG)

        // pruMem addr = 0x00000000
        rclr GLOBAL.pruMem
        // sharedMem addr = 0x00010000
        rclr GLOBAL.sharedMem
        ldi  GLOBAL.sharedMem.w2, 0x0001

        // SET_DEFAULT_PIN_STATE - All OUTPUTS HI, DEBUG PIN LO
        ldi  r30.w0, 0x005f

        // Report that setup is done
        mov  r31.b0, PRU0_ARM_INTERRUPT+16


WAIT_FOR_COMMAND:
        lbbo interface, GLOBAL.pruMem, OFFSET(interface), \
                                       SIZE(interface)
        qbeq QUIT, interface.command, COMMAND_QUIT
        qbeq START_MOTOR, interface.command, COMMAND_START_MOTOR
        qbeq STOP_MOTOR, interface.command, COMMAND_STOP_MOTOR
        qbeq READ_SECTOR, interface.command, COMMAND_READ_SECTOR

        jmp  WAIT_FOR_COMMAND

QUIT:       
        // SET_DEFAULT_PIN_STATE - All OUTPUTS HI, DEBUG PIN LO
        ldi  r30.w0, 0x005f

        ldi  interface.command, COMMAND_QUIT_ACK
        sbbo interface.command, GLOBAL.pruMem, \
                                OFFSET(interface.command), \
                                SIZE(interface)
        mov  r31.b0, PRU0_ARM_INTERRUPT+16
        halt

START_MOTOR:
        clr  PIN_DRIVE_ENABLE_MOTOR
        ldi  interface.command, COMMAND_START_MOTOR_ACK
        sbbo interface.command, GLOBAL.pruMem, \
                                OFFSET(interface.command), \
                                SIZE(interface)
        mov  r31.b0, PRU0_ARM_INTERRUPT+16
        jmp  WAIT_FOR_COMMAND

STOP_MOTOR:
        set  PIN_DRIVE_ENABLE_MOTOR
        ldi  interface.command, COMMAND_STOP_MOTOR_ACK
        sbbo interface.command, GLOBAL.pruMem, \
                                OFFSET(interface.command), \
                                SIZE(interface)
        mov  r31.b0, PRU0_ARM_INTERRUPT+16
        jmp  WAIT_FOR_COMMAND

READ_SECTOR:
        clr  PIN_DRIVE_ENABLE_MOTOR
        jal  STACK.ret_addr, fnWait_For_Hi
        jal  STACK.ret_addr, fnWait_For_Lo
        jal  STACK.ret_addr, fnFind_Sync
        jal  STACK.ret_addr, fnRead_Sector
        set  PIN_DRIVE_ENABLE_MOTOR

        and  interface.command, interface.command, 0x7f
        sbbo interface.command, GLOBAL.pruMem, \
                                OFFSET(interface.command), \
                                SIZE(interface.command)

        mov  r31.b0, PRU0_ARM_INTERRUPT+16
        jmp  WAIT_FOR_COMMAND
        
        
fnWait_For_Hi:
        M_CHECK_ABORT
        qbbc fnWait_For_Hi, PIN_READ_DATA
        jmp  STACK.ret_addr

fnWait_For_Lo:
        M_CHECK_ABORT
        qbbs fnWait_For_Lo, PIN_READ_DATA
        jmp  STACK.ret_addr

.struct Find_Sync
        .u8   bit_remain
        .u8   unused
        .u16  timer
        .u32  cur_dword
        .u32  sync_word
        .u32  ret_addr
.ends
.enter find_sync_scope
.assign Find_Sync, r20, r23, find_sync
fnFind_Sync:
        rcp  find_sync.ret_addr, STACK.ret_addr

        rclr find_sync.cur_dword
        rclr find_sync.bit_remain
        ldi  find_sync.sync_word.w0, SYNC_WORD
        ldi  find_sync.sync_word.w2, SYNC_WORD

time_to_low:
        rclr find_sync.timer
        jal  STACK.ret_addr, fnWait_For_Hi
timer:
        M_CHECK_ABORT
        inc  find_sync.timer
        qbbs timer, PIN_READ_DATA 

        qbgt short, find_sync.timer, #140       // timer < 140
        qbgt med, find_sync.timer, #190       // timer < 190
long:
        ldi  find_sync.bit_remain, #3
        lsl  find_sync.cur_dword, find_sync.cur_dword, #1
        // cur_dword is 0bxxxxxxxxxxxxxxx0
        qbeq found_sync, find_sync.cur_dword, find_sync.sync_word
med:
        ldi  find_sync.bit_remain, #2
        lsl  find_sync.cur_dword, find_sync.cur_dword, #1
        // cur_dword is 0bxxxxxxxxxxxxxx00
        qbeq found_sync, find_sync.cur_dword, find_sync.sync_word
short:
        ldi  find_sync.bit_remain, #1
        lsl  find_sync.cur_dword, find_sync.cur_dword, #1
        // cur_dword is 0bxxxxxxxxxxxxx000
        qbeq found_sync, find_sync.cur_dword, find_sync.sync_word

        // Shift one left, and set bit 0
        ldi  find_sync.bit_remain, #0
        lsl  find_sync.cur_dword, find_sync.cur_dword, #1
        or   find_sync.cur_dword, find_sync.cur_dword, #1
        // cur_dword is 0bxxxxxxxxxxxx0001
        qbeq found_sync, find_sync.cur_dword, find_sync.sync_word

        // We have accumulated 1, 2, 3 or 4 bits, didn't find the sync_word.
        // Keep going
        jmp  time_to_low
        
found_sync:
        rcp  STACK.ret_addr, find_sync.ret_addr
        jmp  STACK.ret_addr
.leave find_sync_scope

.struct Read_Sector
        .u8   bit_remain
        .u8   bit_count
        .u16  timer
        .u16  dword_count
        .u16  ram_offset
        .u32  cur_dword
        .u32  ret_addr
        .u32  sector_len
.ends
.enter read_sector_scope
.assign Read_Sector, r20, r24, read_sector
fnRead_Sector:
        rcp  read_sector.ret_addr, STACK.ret_addr

        rclr read_sector.bit_count
        rclr read_sector.dword_count
        rclr read_sector.cur_dword
        rclr read_sector.ram_offset
        ldi  read_sector.sector_len, SECTOR_LEN_WORD
        lsr  read_sector.sector_len, read_sector.sector_len, #2  // sector_len / 4 == Convert from bytes to DWORDS

rs_time_to_lo:
        rclr read_sector.timer
        jal  STACK.ret_addr, fnWait_For_Hi
rs_timer:
        M_CHECK_ABORT
        inc  read_sector.timer
        qbbs rs_timer, PIN_READ_DATA 

        qbgt rs_short, read_sector.timer, #140       // timer < 140
        qbgt rs_med, read_sector.timer, #190       // timer < 190

rs_long:
        ldi  read_sector.bit_remain, #3
        lsl  read_sector.cur_dword, read_sector.cur_dword, #1
        inc  read_sector.bit_count
        qbeq got_dword, read_sector.bit_count, (SIZE(read_sector.cur_dword) * 8)
rs_med:
        ldi  read_sector.bit_remain, #2
        lsl  read_sector.cur_dword, read_sector.cur_dword, #1
        inc  read_sector.bit_count
        qbeq got_dword, read_sector.bit_count, (SIZE(read_sector.cur_dword) * 8)
rs_short:
        ldi  read_sector.bit_remain, #1
        lsl  read_sector.cur_dword, read_sector.cur_dword, #1
        inc  read_sector.bit_count
        qbeq got_dword, read_sector.bit_count, (SIZE(read_sector.cur_dword) * 8)

        ldi  read_sector.bit_remain, #0
        lsl  read_sector.cur_dword, read_sector.cur_dword, #1
        or   read_sector.cur_dword, read_sector.cur_dword, #1
        inc  read_sector.bit_count
        qbeq got_dword, read_sector.bit_count, (SIZE(read_sector.cur_dword) * 8)

        // We haven't got a complete DWORD yet
        jmp  rs_time_to_lo

got_dword:
        sbbo read_sector.cur_dword, GLOBAL.sharedMem, read_sector.ram_offset, SIZE(read_sector.cur_dword)
        add  read_sector.ram_offset, read_sector.ram_offset, SIZE(read_sector.cur_dword)
        inc  read_sector.dword_count

        rclr read_sector.cur_dword
        rclr read_sector.bit_count

        qbeq no_bits_remain, read_sector.bit_remain, #0
        // Pass remaining bits to next DWORD
        rcp  read_sector.bit_count, read_sector.bit_remain
        or   read_sector.cur_dword, read_sector.cur_dword, #1
no_bits_remain:
        qble rs_time_to_lo, read_sector.sector_len, read_sector.dword_count // dword_count <= sector_len

        rcp  STACK.ret_addr, read_sector.ret_addr
        jmp  STACK.ret_addr
.leave read_sector_scope
        


