; ALU dependency stress: 8192 iterations of dependent ADD/MUL/DIV/XOR/shift/REM.
; Expected: R1=8192  R3=8192  R2=392094
; Run with: risc201-sim examples/benchmarks/alu_dependencies.asm -4

.main:
MOV R1, 0
MOV R2, 0
MOV R8, 8192
loop:
    ADD R1, R1, 1
    MUL R3, R1, 3
    DIV R3, R3, 3
    XOR R3, R3, 85
    XOR R3, R3, 85
    SHL R3, R3, 2
    SHR R3, R3, 2
    REM R4, R3, 97
    ADD R2, R2, R4
    SUB R8, R8, 1
    BEQ done
    JMP loop
done:
    HALT
