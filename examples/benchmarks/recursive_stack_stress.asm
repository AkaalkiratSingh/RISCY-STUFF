; Stack stress: repeat recursive sum(128) 128 times, saving R14 and the argument
; at each nonzero level.
; Expected: R1=128  R2=8256  R9=1056768  SP=4096
; Run with: risc201-sim examples/benchmarks/recursive_stack_stress.asm -6

.main:
MOV R8, 128
MOV R9, 0
repeat:
    MOV R1, 128
    CALL sum
    ADD R9, R9, R2
    SUB R8, R8, 1
    BEQ done
    JMP repeat
done:
    HALT
sum:
    CMP R1, 0
    BEQ base
    PUSH R14
    PUSH R1
    SUB R1, R1, 1
    CALL sum
    POP R1
    POP R14
    ADD R2, R2, R1
    RET
base:
    MOV R2, 0
    RET
