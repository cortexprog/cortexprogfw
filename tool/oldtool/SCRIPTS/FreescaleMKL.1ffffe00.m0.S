.syntax unified
.thumb
.section .text

//entry points (also we breakpoint this place and use it as debugger call)
.globl entrypts
entrypts:
	//flash size (will be replaced by init code)
	flashsz:
	.word 0
	
	//flash block size for erase/write
	.word 1024

	//flash base
	.word 0

	//syscall area
syscall:
	nop
	b syscall

	//init stage 1
	bl init_stage_1
	
	//init stage 2
	.word 0
	
	//init stage 3
	.word 0
	
	//mass erase
	bl all_erase
	
	//erase at address (uint32_t addr)
	bl page_erase
	
	//write block  (uint32_t addr)
	bl write_page
	
	//custom command (...)
	.word 0


wait:
	ldrb r1, [r7]
	lsrs r1, #8
	bcc  wait
	
	//clear status
	movs r1, #0x30
	strb r1, [r7]
	
	bx   lr

go:
	strb r0, [r7, #0x07]
	movs r0, #0x80
	strb r0, [r7]
	b    wait

loadaddr:
	strb r0, [r7, #0x04]
	lsrs r0, #8
	strb r0, [r7, #0x05]
	lsrs r0, #8
	strb r0, [r7, #0x06]
	bx   lr

.globl init_stage_1
init_stage_1:

	ldr   r7, =0x40020000	//ftfa base
	ldr   r0, =0x4004804C	//flash size reg
	ldr   r0, [r0]
	lsrs  r0, #24
	movs  r1, #0x0f
	ands  r0, r1
	beq   special_0
	cmp   r0, #0x0f
	beq   special_f
	lsrs  r0, #1
	adds  r0, #14
	b     common_case

special_0:
	movs  r0, #13
	b     common_case

special_f:
	movs  r0, #15

common_case:
	movs  r1, #1
	lsls  r1, r0
	ldr   r0, =flashsz
	str   r1, [r0]
	bkpt


.globl all_erase
all_erase:
	bl    wait
	movs  r0, #0x49
	bl    go
	bkpt

.globl page_erase
page_erase:
	bl    loadaddr
	movs  r0, #0x09
	bl    go
	bkpt

.globl write_page	//addr param in r0
write_page:
	movs  r6, #0
	movs  r5, r0
	ldr   r4, =0x20000200
loop:
	adds  r0, r5, r6
	bl    loadaddr
	ldr   r0, [r4, r6]
	strb  r0, [r7, #0x08]
	lsrs  r0, #8
	strb  r0, [r7, #0x09]
	lsrs  r0, #8
	strb  r0, [r7, #0x0a]
	lsrs  r0, #8
	strb  r0, [r7, #0x0b]
	movs  r0, #0x06
	bl    go
	adds  r6, #4
	lsrs  r0, r6, #10
	beq   loop
	
	bkpt


.align 4
.section .text.2
.globl info
info:

//checkvals
	.word 0x40048024
	.word 0x00f00000
	.word 0x00100000


//now the fixed-length footer:
	//load address of this code
	.word 0x1ffffe00

	//flash staging area in ram
	.word 0x20000200

	//number of checkvals above
	.word 1
	
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
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x000000e0
	.word 0x00000008
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "Freecale MKL family"