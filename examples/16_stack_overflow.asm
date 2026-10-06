; 16_stack_overflow.asm
; Recursive CALL with no base case — the stack guard halts it.
; Expected: EXCEPTION: STACK_OVERFLOW  (SP runs down from 4096)

loop:
CALL loop
