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
