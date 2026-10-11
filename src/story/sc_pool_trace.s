.set noreorder
.text
.globl main_pool_alloc_from_pool
.ent main_pool_alloc_from_pool
main_pool_alloc_from_pool:
    j sc_pool_alloc_traced
    move $a2, $ra
.end main_pool_alloc_from_pool
.globl main_pool_alloc
.ent main_pool_alloc
main_pool_alloc:
    j sc_pool_alloc
    move $a2, $ra
.end main_pool_alloc
