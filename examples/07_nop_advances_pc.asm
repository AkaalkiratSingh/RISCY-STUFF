; 07_nop_advances_pc.asm
; NOP is a real instruction: it fetches, increments PC, and retires.
; Expected: R1=1  PC=3  retired=3 (two NOPs + the ADD)

NOP
ADD R1, R0, 1
NOP
HALT
