    .section .gnu.linkonce.this_module,"aw",@progbits
    .balign 64
    .globl __this_module
__this_module:
    .zero 0x120
    .quad init_module
    .zero 0xf0
    .quad cleanup_module
    .zero 0x8
    .size __this_module, 0x228

    .section .modinfo,"a",@progbits
    .asciz "description=Lenovo P90 psbfb DSI cached-context switch"
    .asciz "author=P90 native Linux port"
    .asciz "license=GPL"
    .asciz "depends="
    .asciz "vermagic=3.10.20-x86_64_moor SMP preempt mod_unload "
    .section .note.GNU-stack,"",@progbits
