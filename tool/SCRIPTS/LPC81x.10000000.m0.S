.thumb
.section .text

//the bootloader entrypt for flash is 0x1fff1ff0
//we have 1K of RAM at 0x10000000

//we need 512 for temp data (0x10000200 - 0x100003ff)
//this code lives in start (0x10000000 - 0x10000100)
//we start our sp at 0x10000200

//page sizes are comparable to RAM so we cannot offer page erases
//we pretend they are smaller (as we can wwrite smaller pieces)
//but we do not offer page erase. we could if we detect more
//ram but fuck it

//entry points (also we breakpoint this place and use it as debugger call)
.globl entrypts
entrypts:

	//syscall area
syscall:
	nop
	b syscall

	//init stage 1
	bl init_stage_1
	
	//init stage 2
	bl init_stage_2
	
	//init stage 3
	.word 0
	
	//mass erase
	bl all_erase
	
	//erase at address (uint32_t addr)
	bl erase_1K
	
	//write block  (uint32_t addr)
	bl write_512b
	
	//cpuid func (none needed)
	.word 0
	

.globl prepare_cmd
prepare_cmd:
	.word 50
	.word 0
	.word 3

.globl erz_all_cmd
erz_all_cmd:
	.word 52
	.word 0
	.word 3
	.word 12000

.globl erz_blk_cmd
erz_blk_cmd:
	.word 52
	.word 0
	.word 0
	.word 12000

.globl flash_cmd
flash_cmd:
	.word 51
	.word 0 			//destination addr goes here
	.word 0x10000200	//source
	.word 0x200			//512 bytes at a time
	.word 12000

.globl flash_ret_area
flash_ret_area:
	.word 0
	.word 0

.globl init_stage_1
init_stage_1:

	//setup watchpoint on 0x8  - when this is read flash is ready via bootloader making it so
	mov   r0, #0 /* set watchpoint */
	mov   r1, #0x10
	mov   r2, #2
	mov   r3, #7
	bl    syscall
	
	//jump to reset vector
	mov   r0, #0
	ldmia r0!, {r1, r2}
	mov   sp, r1
	blx   r2
	
	//done (but never reached because watchpoint)
	mov   r0, #1
	bkpt


.globl init_stage_2
init_stage_2:
	//clear watchpoint
	mov   r0, #1 /* clear watchpoint */
	bl    syscall

	//setup SP
	ldr   r0, =0x10000200
	mov   sp, r0
	
	//map flash at 0
	ldr   r0, =0x40048000
	mov   r1, #2
	str   r1, [r0]
	
	//save IAP addr
	ldr   r6, =0x1fff1ff1
	
	//identify how much flash we have
	ldr   r1, =0x400483F8
	ldr   r1, [r1]
	lsl   r1, #24
	lsr   r1, #28
	mov   r2, #1
	lsl   r2, r1
	lsl   r3, r2, #2	//4 for 4k, 8 for 8k, 16 for 16k
	
	sub   r1, r3, #1
	ldr   r0, =prepare_cmd
	str   r1, [r0, #8]
	ldr   r0, =erz_all_cmd
	str   r1, [r0, #8]
	
	//prepare mem map
	ldr   r0, =0x10000200
	mov   r1, #1		//1 area
	mov   r2, #0		//based at 0x08000000 
	lsl   r3, #8
	add   r3, #9		//write block is 512 bytes
	lsl   r3, #8
	add   r3, #10		//erase block is 1024 bytes
	stmia r0!, {r1-r3}
	ldr   r0, =0x10000200
	
	//names (none)
	movs  r1, #0

	//flags (none)
	movs  r2, #0
	bkpt


check_all_errs:
	ldr   r1, =flash_ret_area
	ldr   r1, [r1]
	cmp   r1, #0
	beq   it_succeeded
	
it_failed:
	mov   r0, #0
	bkpt

it_succeeded:
	bx    lr

.globl all_erase
all_erase:
	ldr   r0, =prepare_cmd
	ldr   r1, =flash_ret_area
	blx   r6
	bl    check_all_errs
	
	ldr   r0, =erz_all_cmd
	ldr   r1, =flash_ret_area
	blx   r6
	bl    check_all_errs
	
	mov   r0, #1
	bkpt

.globl erase_1K	//addr param in r0
erase_1K:
	lsr   r0, #4
	ldr   r1, =erz_blk_cmd
	str   r0, [r1, #4]
	str   r0, [r1, #8]
	
	ldr   r0, =prepare_cmd
	ldr   r1, =flash_ret_area
	blx   r6
	bl    check_all_errs
	
	ldr   r0, =erz_blk_cmd
	ldr   r1, =flash_ret_area
	blx   r6
	bl    check_all_errs
	
	mov   r0, #1
	bkpt
	

.globl write_512b	//addr param in r0
write_512b:
	mov   r7, r0
	ldr   r0, =prepare_cmd
	ldr   r1, =flash_ret_area
	blx   r6
	bl    check_all_errs
	
	ldr   r0, =flash_cmd
	ldr   r1, =flash_ret_area
	str   r7, [r0, #4]
	blx   r6
	bl    check_all_errs
	
	mov   r0, #1
	bkpt



.align 4
.section .text.2
.globl info
info:

//checkvals (none)

//now the fixed-length footer:
	//load address of this code
	.word 0x10000000

	//flash staging area in ram
	.word 0x10000200

	//number of checkvals above
	.word 0
	
	//tagetid on swd bus (most likely zero)
	.word 0
	
	//cpuid value masks from cpuid (8 of them)
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	
	//cpuid match values from cpuid (8 of them)
	.word 0x00000004
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x000000c0
	.word 0x000000b4
	.word 0x0000000b
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "NXP LPC8xx"