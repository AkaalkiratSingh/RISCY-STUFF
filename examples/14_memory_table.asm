; 14_memory_table.asm
; Writes three words, reads them back, and sums them.
; Run with: risc201-sim 14_memory_table.asm --mem 0:4
; Expected: MEM[0..2]=10,20,30  R8=60

ADD R1, R0, 10
STORE R1, 0[R0]
ADD R2, R0, 20
STORE R2, 1[R0]
ADD R3, R0, 30
STORE R3, 2[R0]
LOAD R4, 0[R0]
LOAD R5, 1[R0]
ADD R6, R4, R5
LOAD R7, 2[R0]
ADD R8, R6, R7
HALT
