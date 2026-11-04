.main:
MOV R1, 0
MOV R2, 0
MOV R3, 0
MOV R9, 777
STORE R9, 0[R0]
loop:
AND R4, R1, 1
BEQ even
ADD R3, R3, 1
JMP join
even:
ADD R2, R2, 1
join:
JMP hop1
STORE R0, 0[R0]
hop1:
JMP hop2
DIV R5, R9, 0
hop2:
JMP hop3
STORE R0, 0[R0]
hop3:
JMP hop4
DIV R5, R9, 0
hop4:
ADD R1, R1, 1
CMP R1, 16384
BEQ done
JMP loop
done:
LOAD R6, 0[R0]
HALT
