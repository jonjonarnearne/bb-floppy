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
#define SECT_OFST               #11

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

.macro  dec
.mparam reg
        sub reg, reg, #1
.endm

// This macro takes 15 + 5 = 20ns
.macro  M_CHECK_ABORT
        lbbo interface.command, GLOBAL.pruMem, OFFSET(interface.command), \
                                               SIZE(interface.command)
        qbeq QUIT, interface.command, COMMAND_QUIT
.endm
.struct GREGS
        .u32  pruMem
        .u32  sharedMem
        .u8   sector_offset
.ends
.assign GREGS, r0, r2.b0, GLOBAL
.struct SREGS
        .u32  ret_addr
.ends
.assign SREGS, r3, r3, STACK

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
        qbeq FIND_SYNC, interface.command, COMMAND_FIND_SYNC
        qbeq READ_SECTOR, interface.command, COMMAND_READ_SECTOR
        qbeq SET_HEAD_DIR, interface.command, COMMAND_SET_HEAD_DIR
        qbeq SET_HEAD_SIDE, interface.command, COMMAND_SET_HEAD_SIDE
        qbeq STEP_HEAD, interface.command, COMMAND_STEP_HEAD
        qbeq RESET_DRIVE, interface.command, COMMAND_RESET_DRIVE
        qbeq WRITE_TRACK, interface.command, COMMAND_ERASE_TRACK
        qbeq WRITE_TRACK, interface.command, COMMAND_WRITE_TRACK

        jmp  WAIT_FOR_COMMAND

QUIT:       
        // SET_DEFAULT_PIN_STATE - All OUTPUTS HI, DEBUG PIN LO
        ldi  r30.w0, 0x005f

        and  interface.command, interface.command, 0x7f
        sbbo interface.command, GLOBAL.pruMem, \
                                OFFSET(interface.command), \
                                SIZE(interface)
        mov  r31.b0, PRU0_ARM_INTERRUPT+16
        halt

START_MOTOR:
        clr  PIN_DRIVE_ENABLE_MOTOR

        // 50 ms seems to work
        // 10ns * 5,000,000 = 50,000,000us = 50ms
        // 5,000,000 = 0x004c 4b40
        ldi  r5.w0, #0x4b40
        ldi  r5.w2, #0x004c
spin_up_time:
        dec  r5
        qbne spin_up_time, r5, #0
        jmp  SEND_ACK

STOP_MOTOR:
        set  PIN_DRIVE_ENABLE_MOTOR
        jmp  SEND_ACK

SET_HEAD_DIR:
        clr  PIN_HEAD_DIR
        qbeq SEND_ACK, interface.argument, #1
        set  PIN_HEAD_DIR
        jmp  SEND_ACK

SET_HEAD_SIDE:
        set  PIN_HEAD_SELECT
        qbeq SEND_ACK, interface.argument, #1
        clr  PIN_HEAD_SELECT
        jmp  SEND_ACK

STEP_HEAD:
        jal  STACK.ret_addr, fnStep_Head
        jmp  SEND_ACK

RESET_DRIVE:
        // If we are at track zero, do nothing
        qbbc SEND_ACK, PIN_TRACK_ZERO

        set  PIN_HEAD_DIR
        ldi  interface.argument, #0
        jal  STACK.ret_addr, fnStep_Head
        jmp  SEND_ACK

FIND_SYNC:
        jal  STACK.ret_addr, fnWait_For_Hi
        jal  STACK.ret_addr, fnWait_For_Lo
        jal  STACK.ret_addr, fnFind_Sync
        jmp  SEND_ACK

READ_SECTOR:
        //  We might try to read an entire track here.
        jal  STACK.ret_addr, fnWait_For_Hi
        jal  STACK.ret_addr, fnWait_For_Lo
        jal  STACK.ret_addr, fnFind_Sync
        jal  STACK.ret_addr, fnRead_Sector

        // if the GLOBAL.sector_offset is 0xff,
        // we eighter has a checksum error, or are not reading the entire track
        qbeq SEND_ACK, GLOBAL.sector_offset, #0xff
        // If the GLOBAL.sector_offset != SECT_OFST (11), keep looking
        qbne READ_SECTOR, GLOBAL.sector_offset, SECT_OFST

        jmp  SEND_ACK

WRITE_TRACK:
        jal  STACK.ret_addr, fnWrite_Track
        jmp  SEND_ACK

SEND_ACK:
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

        // Lemmings sync word!
        //ldi  find_sync.sync_word.w0, #0xaaaa
        //ldi  find_sync.sync_word.w2, #0x912a

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

/* The disk is encoded in big endian format.
 * We are reading one 32bit DWORD at a time, and when we are storing this to the pru memory,
 * the bytes are swapped into little endian mode.
 * So data on disk looks like this:
 *
 * 0x11 0x22 0x33 0x44 0x55 0x66 0x77 0x88
 *
 * The data in ram will look like this:
 *
 * 0x44 0x33 0x22 0x11 0x88 0x77 0x66 0x55
 *
 * We expect the bytes to be flipped back before processing on the ARM CPU
 */
.struct Read_Sector
        .u8   bit_remain
        .u8   bit_count
        .u16  timer
        .u16  dword_count       // Dwords, currently read
        .u16  ram_offset        // Position of write pointer
        .u32  cur_dword
        .u32  ret_addr
        .u32  sector_len        // Number of dwords to read
.ends
.enter read_sector_scope
.assign Read_Sector, r21, r25, read_sector
fnRead_Sector:
        rcp  read_sector.ret_addr, STACK.ret_addr

        rclr read_sector.dword_count
        rclr read_sector.ram_offset

        // Set the first two dwords to 0xaaaaaaaa 0x44894489
        ldi  read_sector.cur_dword.w0, 0xaaaa
        ldi  read_sector.cur_dword.w2, 0xaaaa
        sbbo read_sector.cur_dword, GLOBAL.sharedMem, read_sector.ram_offset, SIZE(read_sector.cur_dword)
        add  read_sector.ram_offset, read_sector.ram_offset, SIZE(read_sector.cur_dword)
        inc  read_sector.dword_count

        ldi  read_sector.cur_dword.w0, SYNC_WORD
        ldi  read_sector.cur_dword.w2, SYNC_WORD
        sbbo read_sector.cur_dword, GLOBAL.sharedMem, read_sector.ram_offset, SIZE(read_sector.cur_dword)
        add  read_sector.ram_offset, read_sector.ram_offset, SIZE(read_sector.cur_dword)
        inc  read_sector.dword_count

        rclr read_sector.cur_dword
        rclr read_sector.bit_count

        // If interface.argument is 16,
        // we read the sector head.
        // If sector head info says that we are on start of sectors,
        // We read the entire track
        // Else if argument != 16, just read the amount of dwords specified
        rcp  read_sector.sector_len, interface.argument

rs_time_to_lo:
        jal  STACK.ret_addr, fnWait_For_Hi
rs_time_to_lo_fast:
        rclr read_sector.timer
rs_timer:
        M_CHECK_ABORT
        inc  read_sector.timer
        qbbs rs_timer, PIN_READ_DATA 

        qbgt rs_short, read_sector.timer, #140//#140       // timer < 140
        qbgt rs_med, read_sector.timer, #198  //#190       // timer < 190

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

        // Mark the sector read done,
        // if we are not trying to read whole track!
        ldi  GLOBAL.sector_offset, #0xff
        qbne read_sector_done, read_sector.sector_len, #16
        // Exit this function here if have read more than the SECTOR_HEAD
        
        // We have only read the head, now check if we should continue reading
        jal  STACK.ret_addr, fnGet_Sector_Offset
        // 0xff == chksum error
        qbeq read_sector_done, GLOBAL.sector_offset, #0xff
        qbne read_sector_done, GLOBAL.sector_offset, SECT_OFST

        // Setup read counter to read rest of track
        ldi  read_sector.sector_len, #(0x3000/4) - 16
        // Now jump up to read rest of track
        jmp rs_time_to_lo

read_sector_done:
        rcp  STACK.ret_addr, read_sector.ret_addr
        jmp  STACK.ret_addr

// This is a helper function, so we keep scope of read_sector
.struct MFM_HEADER
        .u32  NULL
        .u32  SYNC
        .u32  odd_info
        .u32  even_info
        .u32  odd_label0
        .u32  odd_label1
        .u32  odd_label2
        .u32  odd_label3
        .u32  even_label0
        .u32  even_label1
        .u32  even_label2
        .u32  even_label3
        .u32  odd_head_chksum
        .u32  even_head_chksum
.ends
.struct Get_Sector_Offset
        .u32  MASK
        .u32  chksum
.ends
.enter get_sector_offset_scope
.assign MFM_HEADER, r7, r20, mfm_header
.assign Get_Sector_Offset, r4, r5, get_sector_offset
fnGet_Sector_Offset:
        ldi  get_sector_offset.MASK.w0, 0x5555
        ldi  get_sector_offset.MASK.w2, 0x5555
        lbbo mfm_header, GLOBAL.sharedMem, 0, SIZE(MFM_HEADER)

        rclr get_sector_offset.chksum
        xor  get_sector_offset.chksum, get_sector_offset.chksum, \
                                                        mfm_header.odd_info
        xor  get_sector_offset.chksum, get_sector_offset.chksum, \
                                                        mfm_header.even_info
        and  get_sector_offset.chksum, get_sector_offset.chksum, \
                                                        get_sector_offset.MASK
        xor  get_sector_offset.chksum, get_sector_offset.chksum, \
                                                        mfm_header.odd_label0
        xor  get_sector_offset.chksum, get_sector_offset.chksum, \
                                                        mfm_header.even_label0
        xor  get_sector_offset.chksum, get_sector_offset.chksum, \
                                                        mfm_header.odd_label1
        xor  get_sector_offset.chksum, get_sector_offset.chksum, \
                                                        mfm_header.even_label1
        xor  get_sector_offset.chksum, get_sector_offset.chksum, \
                                                        mfm_header.odd_label2
        xor  get_sector_offset.chksum, get_sector_offset.chksum, \
                                                        mfm_header.even_label2
        xor  get_sector_offset.chksum, get_sector_offset.chksum, \
                                                        mfm_header.odd_label3
        xor  get_sector_offset.chksum, get_sector_offset.chksum, \
                                                        mfm_header.even_label3
        and  get_sector_offset.chksum, get_sector_offset.chksum, \
                                                        get_sector_offset.MASK

        and  mfm_header.odd_info, mfm_header.odd_info, get_sector_offset.MASK
        lsl  mfm_header.odd_info, mfm_header.odd_info, #1
        and  mfm_header.even_info, mfm_header.even_info, get_sector_offset.MASK
        or   mfm_header.odd_info, mfm_header.odd_info, mfm_header.even_info

        and  mfm_header.odd_head_chksum, mfm_header.odd_head_chksum, \
                                                        get_sector_offset.MASK
        lsl  mfm_header.odd_head_chksum, mfm_header.odd_head_chksum, #1
        and  mfm_header.even_head_chksum, mfm_header.even_head_chksum, \
                                                        get_sector_offset.MASK
        or   mfm_header.odd_head_chksum, mfm_header.odd_head_chksum, \
                                                        mfm_header.even_head_chksum
        
        rclr GLOBAL.sector_offset

        qbeq chksum_match, mfm_header.odd_head_chksum, get_sector_offset.chksum
        // This header is unreadable -- We set OFFSET to 0xff for error
        ldi  GLOBAL.sector_offset, #0xff
        jmp  STACK.ret_addr

chksum_match:
        // set GLOBAL.sector_offset to the value of odd_info
        and  GLOBAL.sector_offset, mfm_header.odd_info, #0xff
        
        jmp  STACK.ret_addr
.leave get_sector_offset_scope
.leave read_sector_scope

.struct Step_Head
        .u16  step_count
        .u16  unused
        .u32  ret_addr
        .u32  timer
.ends
.enter step_head_scope
.assign Step_Head, r20, r22, step_head
fnStep_Head:
        rcp  step_head.ret_addr, STACK.ret_addr
        rcp  step_head.step_count, interface.argument    
        qblt end_step_head, step_head.step_count, #80 //Programming error
do:
        M_CHECK_ABORT

        clr  PIN_HEAD_STEP

        // 10ns * 80 = 800ns = 0.8us
        ldi  step_head.timer.w0, #80
        ldi  step_head.timer.w2, #0x0000
delay_lo:
        dec  step_head.timer
        qbne delay_lo, step_head.timer, #0

        set  PIN_HEAD_STEP
        
        ldi  step_head.timer.w0, #0x1a80
        ldi  step_head.timer.w2, #0x0006
        qbne normal_step, step_head.step_count, #0
        qbbc done, PIN_HEAD_DIR // This is a programming error, we forgot to step out
        // We are resetting the drive!
        qbbs cylinder_delay, PIN_TRACK_ZERO
        jmp done

normal_step:
        dec  step_head.step_count
        qbeq done, step_head.step_count, #0

        // DELAY PER CYLINDER - Increment
        // 4ms = 4 000 000ns = 400 000 = 0x 00 06 1a 80
cylinder_delay:
        dec  step_head.timer
        qbne cylinder_delay, step_head.timer, #0
        jmp do

done:
        // Wait for head to settle
        // 30ms = 30 000 000ns = 3 000 000 * 10ns = 0x 00 2d c6 c0
        ldi  step_head.timer.w0, #0xc6c0
        ldi  step_head.timer.w2, #0x002d
delay_head_settle:
        dec  step_head.timer
        qbne delay_head_settle, step_head.timer, #0

end_step_head:
        rcp  STACK.ret_addr, step_head.ret_addr
        jmp  STACK.ret_addr
.leave step_head_scope

// This function is used both for erase and write
.struct Write_Track
        .u8   bit_count
        .u8   unused0
        .u16  unused1
        .u16  get_function
        .u16  ret_addr
        .u16  dword_index
        .u16  dword_count
        .u32  dword
        .u32  delay_timer
.ends
.enter write_track_scope
.assign Write_Track, r20, r24, write_track
fnWrite_Track:
        qbeq write_track_fn_erase, interface.command, COMMAND_ERASE_TRACK

        // This is regular write mode
        ldi  write_track.get_function, fnGet_Dword
        ldi  write_track.dword_count, #0x2ec0 //#0x3200 //#0x2ec0 // Write exactly 0x2ec0 bytes = 11 sectors of 0x440 = 1088
        jmp  write_track_start

write_track_fn_erase:
        // We should write 0xaa to whole track
        ldi  write_track.get_function, fnGet_Erase
        ldi  write_track.dword_count, #0x3200
        
write_track_start:
        clr  PIN_DRIVE_ENABLE_MOTOR

        //   600ms spin up! = 6.0e8 == 6.0e9/10
        ldi  write_track.delay_timer.w0, #0x8700
        ldi  write_track.delay_timer.w2, #0x0393
write_track_spin_up:
        dec  write_track.delay_timer
        qbne write_track_spin_up, write_track.delay_timer, #0

        //   One mfm track = NULL       0xaaaaaaaa = 4  bytes
        //                 + Sync dword 0x44894489 = 4  bytes
        //                 + Track Head 14 dword   = 56   bytes
        //                 + Track Data            = 1024 bytes
        //                 =                Total  = 1088 bytes * 11 sectors
        //                 =            Sum Total  = 11968 bytes = 0x2ec0
        rclr write_track.dword_index

        //   Must wait not wait more than 8us before writing data after WRITE_GATE = TRUE
        clr  PIN_WRITE_GATE

write_track_get_dword:
        // We spend a total of 40ns here
        jal  write_track.ret_addr, write_track.get_function                     // 35ns

        // if (dword_index > dword_count) goto epilogue
        qbgt write_track_epilogue, write_track.dword_count, \
                                        write_track.dword_index                 //  5ns


write_track_write:
        // MFM data is 01|001|0001
        // 0x0000
        //               a    a    a    a
        // 0xaaaa = 0b1010 1010 1010 1010
        // Sync
        //               4    4    8    9
        // 0x4489 = 0b0100 0100 1000 1001

        qbbc write_track_bit_0, write_track.dword, #31

write_track_bit_1:
        clr  PIN_WRITE_DATA
        jmp  write_track_write_bit

write_track_bit_0:
        nop0 r0, r0, r0
        nop0 r0, r0, r0

write_track_write_bit:
        // delay 500ns
        rclr write_track.delay_timer
        ldi  write_track.delay_timer.w0, #15
write_track_pulse_delay:
        // This loop takes 30ns
        M_CHECK_ABORT
        dec  write_track.delay_timer
        qbne write_track_pulse_delay, write_track.delay_timer, #0

        set  PIN_WRITE_DATA

        // Now delay 1500ns (-15ns) for a total of 2000ns | 2us
        rclr write_track.delay_timer
        ldi  write_track.delay_timer.w0, #50
write_track_0_delay:
        M_CHECK_ABORT                                           // 20ns
        dec  write_track.delay_timer                            //  5ns
        qbne write_track_0_delay, write_track.delay_timer, #0   //  5ns

        // The next 3 instructions = 15ns
        lsl  write_track.dword, write_track.dword, #1
        dec  write_track.bit_count
        // Get a new dword of 0 == bit_count
        qbeq write_track_get_dword, write_track.bit_count, #0

        // Spend as much time here as in write_track_get_dword
        nop0 r0, r0, r0
        nop0 r0, r0, r0
        nop0 r0, r0, r0
        nop0 r0, r0, r0
        nop0 r0, r0, r0
        nop0 r0, r0, r0
        nop0 r0, r0, r0
        nop0 r0, r0, r0
        jmp  write_track_write

write_track_epilogue:
        set  PIN_WRITE_GATE

        // Must wait 650us before stopping motor after PIN_WRITE_GATE == FALSE
        ldi  write_track.delay_timer.w0, #65000
        ldi  write_track.delay_timer.w2, #0
write_track_spin_down:
        dec  write_track.delay_timer
        qbne write_track_spin_down, write_track.delay_timer, #0

        set  PIN_DRIVE_ENABLE_MOTOR

        jmp  STACK.ret_addr

// This is called when we do write
fnGet_Dword:
        lbbo write_track.dword, GLOBAL.sharedMem, write_track.dword_index, 4    // 15ns
        add  write_track.dword_index, write_track.dword_index, 4                //  5ns
        ldi  write_track.bit_count, #32                                         //  5ns
        jmp  write_track.ret_addr                                               //  5ns

// This is called when we do erase
fnGet_Erase:
        ldi  write_track.dword.w0, #0xaaaa                                      //  5ns
        ldi  write_track.dword.w2, #0xaaaa                                      //  5ns
        add  write_track.dword_index, write_track.dword_index, 4                //  5ns
        ldi  write_track.bit_count, #32                                         //  5ns
        nop0 r0, r0, r0                                                         //  5ns
        jmp  write_track.ret_addr                                               //  5ns
.leave write_track_scope

