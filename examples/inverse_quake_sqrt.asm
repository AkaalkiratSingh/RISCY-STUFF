# This code computes 1/sqrt(x) using Fast Inverse Squareroot
# Source: https://en.wikipedia.org/wiki/Fast_inverse_square_root
# Expected: R4 ~ 2

.main:
    load r1, x
    load r2, half
    load r3, threehalves
    load r13, magic

    mov r4, r1                  # y = x
    fmul r5, r1, r2             # x2 = x*0.5f

    ; operations in int format
    shr r4, r4, 1               # y.int -> y.int >> 1
    sub r4, r13, r4             # 0x5f3759df - y.int

    ; operations in float format
    fmul r6, r4, r4             # temp = y*y
    fmul r6, r5, r6             # temp = temp*x2 
    fsub r6, r3, r6             # temp = 1.5f - temp
    fmul r4, r6, r4             # y    = temp*y
    
    halt

.literals:
half: 0.5f
threehalves: 1.5f
magic: 0x5f3759df
x: 0.25f
