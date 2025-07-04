.section .rodata
.global firmware
.type   firmware, %object
.balign 4
firmware:
.incbin "firmware.bin"
firmware_end:

.global firmware_size
.type   firmware_size, %object
.balign 4
firmware_size:
.int	firmware_end - firmware

.section    .note.GNU-stack
