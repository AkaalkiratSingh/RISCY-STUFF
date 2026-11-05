; Load-use stress: build a 1024-node ring, then follow 65536 links and accumulate
; their indices. Each LOAD is immediately consumed by a dependent ADD.
; Expected: R1=0  R2=33521664  MEM[0..1023]=(i+37)%1024  stalls 65536 with forwarding
; Run with: risc201-sim processor_benchmark/dependent_pointer_chase.asm -6 --vertical

.main:
MOV R1, 0
init:
    ADD R2, R1, 37
    AND R2, R2, 1023
    STORE R2, 0[R1]
    ADD R1, R1, 1
    CMP R1, 1024
    BEQ ready
    JMP init
ready:
    MOV R1, 0
    MOV R2, 0
    MOV R8, 65536
chase:
    LOAD R1, 0[R1]
    ADD R2, R2, R1
    SUB R8, R8, 1
    BEQ done
    JMP chase
done:
    HALT
