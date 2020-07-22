.syntax unified
.thumb
.section .text

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
	.word 0
	
	//init stage 3
	.word 0
	
	//mass erase
	bl all_erase
	
	//erase at address (uint32_t addr)
	bl page_erase
	
	//write block  (uint32_t addr)
	bl write_page
	
	//cpuid func (none needed)
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
	ldr   r3, =0x40048050	//flash size reg (SIM_FCFG2)
	ldr   r3, [r3]			//script matchvals verified top bit is set so we dont need to clear it
	lsrs  r3, #24
	lsls  r3, #3			//"lsl 13" for "bytes" size. our blocks are 1K, son only "lsl 3"
	movs  r1, #1			//1 flash area
	movs  r2, #0			//start address 0x00000000
	lsls  r3, #8
	adds  r3, #10			//1K sectors
	lsls  r3, #8
	adds  r3, #10			//1K sectors
	
	movs  r4, #0x20			//stash it here
	lsls  r4, #24
	movs  r0, r4
	stmia r4!, {r1,r2,r3}	//all done

	//names (none)
	movs  r1, #0

	//flags (none)
	movs  r2, #0
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
	
	.word 0x4004804C
	.word 0xf0fffffc
	.word 0x00000000
	
	.word 0x40048050
	.word 0x80ffffff
	.word 0x00800000


//now the fixed-length footer:
	//load address of this code
	.word 0x1ffffe00

	//flash staging area in ram
	.word 0x20000200

	//number of checkvals above
	.word 3
	
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