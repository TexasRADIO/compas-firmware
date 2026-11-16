/* Emulator-only entry. Model the SPL's load with poisoned RAM. */
    .section .text,"ax",@progbits
    .globl _start
_start:
    cli
    r0 = 0x01C0A800
    r1 = 0xA5A5A5A5
    r2 = 0x01C1E800
1:  [r0++=4] = r1
    if (r0 != r2) goto 1b
    r0 = 0x01C0A800
    r1 = loader_image
    r2 = loader_end
2:  r3 = [r1++=4]
    [r0++=4] = r3
    if (r1 != r2) goto 2b
    r0 = 0x01C0A800
    goto r0
    .balign 4
loader_image:
    .incbin "loader.bin"
    .balign 4
loader_end:
