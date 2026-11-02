; Insertion sort of 32 reverse-ordered words, in place. The inner loop's trip count
; depends on the data, so the taken-branch density and the memory traffic vary per
; element. Complements bubble_sort, whose inner loop has a fixed trip count.
; Run with: risc201-sim insertion_sort.asm --mem 0:32
; Expected: MEM[0..31] = 1 2 3 ... 32

.main:
ADD R1, R0, 0
ADD R2, R0, 32
init:
    STORE R2, 0[R1]
    SUB R2, R2, 1
    ADD R1, R1, 1
    SUB R3, R1, 32
    BEQ initdone
    JMP init
initdone:
    ADD R1, R0, 1
outer:
    SUB R2, R1, 32
    BEQ sorted
    LOAD R3, 0[R1]
    ADD R4, R1, 0
inner:
    ADD R5, R4, 0
    BEQ place
    SUB R6, R4, 1
    LOAD R7, 0[R6]
    SUB R8, R7, R3
    BEQ place
    SHR R9, R8, 31
    BEQ shift
    JMP place
shift:
    STORE R7, 0[R4]
    ADD R4, R6, 0
    JMP inner
place:
    STORE R3, 0[R4]
    ADD R1, R1, 1
    JMP outer
sorted:
    HALT
