; 15_stack_pushpop.asm
; PUSH three values, then POP them back in reverse order.
; Run with: risc201-sim 15_stack_pushpop.asm --mem 4093:3
; Expected: R4=30 R5=20 R6=10  SP=4096  MEM[4093..4095]=30,20,10

ADD R1, R0, 10
ADD R2, R0, 20
ADD R3, R0, 30
PUSH R1
PUSH R2
PUSH R3
POP R4
POP R5
POP R6
HALT
