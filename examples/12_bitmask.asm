; 12_bitmask.asm
; Bitwise ops with hexadecimal immediates.
; Expected: R1=255  R2=15  R3=511  R4=0

ADD R1, R0, 0xFF
AND R2, R1, 0x0F
OR  R3, R1, 0x100
XOR R4, R1, 0xFF
HALT
