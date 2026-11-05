; Bubble sort of 10 words, in place. Heavy on LOAD->use hazards.
; Run with: risc201-sim bubble_sort.asm --mem 0:10
; Expected: MEM[0..9] = 3 7 15 28 36 42 51 64 80 93

.main:
MOV R1, 93
STORE R1, 0[R0]
MOV R1, 7
STORE R1, 1[R0]
MOV R1, 51
STORE R1, 2[R0]
MOV R1, 28
STORE R1, 3[R0]
MOV R1, 64
STORE R1, 4[R0]
MOV R1, 3
STORE R1, 5[R0]
MOV R1, 80
STORE R1, 6[R0]
MOV R1, 15
STORE R1, 7[R0]
MOV R1, 42
STORE R1, 8[R0]
MOV R1, 36
STORE R1, 9[R0]

MOV R1, 9
pass:
    MOV R2, 0
    MOV R3, 9
inner:
    LOAD R4, 0[R2]
    LOAD R5, 1[R2]
    SUB R6, R4, R5
    SHR R6, R6, 31
    BEQ doswap
    JMP noswap
doswap:
    STORE R5, 0[R2]
    STORE R4, 1[R2]
noswap:
    ADD R2, R2, 1
    SUB R3, R3, 1
    BEQ endpass
    JMP inner
endpass:
    SUB R1, R1, 1
    BEQ sorted
    JMP pass
sorted:
    HALT
