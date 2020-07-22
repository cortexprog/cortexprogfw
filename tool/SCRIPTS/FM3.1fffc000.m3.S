.syntax unified
.thumb
.section .text

//C-m3
//we have 16K of RAM at 0x1FFFC000 (or more, there is sometimes also ram below 0x1FFFC000 but we ignore it)
//we have a variable amount of main flash at 0x00000000 and 32k of "work flash" at 0x00200000
//FLASH I/F is at 0x40000000
//we'll write 8K at a time

//sp starts at 0x20000000
//we stage data at 0x20000000 too



//entry points (also we breakpoint this place and use it as debugger call)
.globl entrypts
entrypts:

	//syscall area
syscall:
1:
	nop
	b 1b

	//init stage 1
	bl init_stage_1
	
	//init stage 2
	.word 0
	
	//init stage 3
	.word 0
	
	//mass erase
	bl all_erase
	
	//erase at address (uint32_t addr)
	bl block_erase
	
	//write block  (uint32_t addr)
	bl block_write
	
	//cpuid func (none needed)
	.word 0


.globl init_stage_1
init_stage_1:

	//turn off WDT
	ldr   r1, =0x40011000
	ldr   r2, =0x1ACCE551
	ldr   r3, =0xE5331AAE
	movs  r4, #0
	str   r2, [r1, #0xc00]
	str   r3, [r1, #0xc00]
	str   r4, [r1, #0x08]
	
	//set up stack and flash IF ptr reg
	mov   r0, #0x20000000	// staging area and sp start
	mov   sp, r0
	mov   r7, #0x40000000	// flash unit base

	//not sure how to probe size here, so just assume 512K (layout is same for smaller units, just missing blocks at end)
	adds  r6, r0, #4	//save space for "num areas"
	
	movs  r1, #0x00000000	//at 0x00000000:
	ldr   r2, =0x00020d0d	// 2 erase blocks of 8K each

	mov   r3, #0x00004000	//at 0x00004000:
	ldr   r4, =0x00010dcd	// 1 eraseblock of 48K (writen at 8K)

	stmia r6!, {r1-r4}

	ldr   r1, =0x00010000	//at 0x00010000:
	ldr   r2, =0x00070d10	// 7 eraseblocks of 64K (writen at 8K) 
	
	ldr   r3, =0x00200000	//at 0x00200000:
	ldr   r4, =0x00040d0d	// 4 erase blocks of 8K each

	stmia r6!, {r1-r4}
	
	ldr   r1, =0x00100000	//at 0x00100000:	(security code)
	ldr   r2, =0x00010101	// 1 block of 2 bytes
	
	ldr   r3, =0x00102000	//at 0x00102000:	(CR trim data)
	ldr   r4, =0x00010101	// 1 block of 2 bytes

	stmia r6!, {r1-r4}
	movs  r1, r6

	//calc "num areas" and store it
	subs  r6, r0		//this will overcount how much we wrote by 4B, we do not care bc we are about to divide by 8
	lsrs  r6, #3
	str   r6, [r0]

	movs  r6, r1
	movs  r2, #4		//4 names
	movs  r3, #0
	ldr   r4, =strFLASH
	stmia r6!, {r2, r3, r4}
	ldr   r3, =0x00200000
	ldr   r4, =strWORKFLASH
	stmia r6!, {r3, r4}
	ldr   r3, =0x00100000
	ldr   r4, =strSECURITYTRIM
	stmia r6!, {r3, r4}
	ldr   r3, =0x00102000
	ldr   r4, =strCRTRIM
	stmia r6!, {r3, r4}

	//flags (none)
	movs  r2, #0
	bkpt

strFLASH:
	.ascii "FLASH\0"
strWORKFLASH:
	.ascii "WORK FLASH\0"
strSECURITYTRIM:
	.ascii "SECURITY BITS\0"
strCRTRIM:
	.ascii "TRIM BITS\0"

.align 1

wait:	//in: r0 = the AND value we should "wait with", will return 0 on failure, 1 on success
	ldr    r1, [r7, #8]	//dummy read is mandatory
wait_loop:
	ldr    r1, [r7, #8]
	tst    r1, #2
	itt    ne
	movsne r0, #0
	bxne   lr
	ands   r1, r0
	cmp    r1, #1
	bne    wait_loop
	movs   r0, #1
	bx     lr


flash_common:	//(u8 third write). returns (0xAA, 0x55, 0xAA8, 0x554) in r0..r3
	mov    r12, r0
	movs   r1, #0xF0		//reset controller
	mov    r2, #0x100
	strh   r1, [r2]
	
	movs   r0, #0xAA
	movs   r1, #0x55
	mov    r2, #0xAA8
	mov    r3, #0x554
	
	strh   r0, [r2]
	strh   r1, [r3]
	strh   r12, [r2]
	bx     lr

erase_common:	//returns 0xAA8 in r2
	push   {lr}
	movs   r0, #0x80
	bl     flash_common
	strh   r0, [r2]
	strh   r1, [r3]
	pop    {pc}
	
	

.globl all_erase
all_erase:
	bl     erase_common
	movs   r0, #0x10
	strh   r0, [r2]
	movs   r0, #0x11
	bl     wait
	bkpt

.globl block_erase
block_erase:
	movs   r4, r0
	bl     erase_common
	movs   r0, #0x30
	strh   r0, [r4]
	movs   r0, #0x05
	bl     wait
	bkpt


.globl block_write
block_write:
	//we write 8K one byte at a time
	
	
	//setup
	movs   r4, r0			//save address
	mov    r6, 0x20000000		//staging area pointer
	
	//see if writing trim or "Security" value. set loop counter apporpriately
	lsrs   r0, #16
	cmp    r0, #0x10
	ite    eq
	moveq  r5, #2			//loop counter for trip & security value
	movne  r5, #8192		//loop counter
	
write_loop:
	movs   r0, #0xA0
	bl     flash_common
	ldrb   r0, [r6], #1
	strb   r0, [r4], #1
	movs   r0, #0x21
	bl     wait
	cmp    r0, #0
	beq    write_out
	subs   r5, #1
	bne    write_loop
	//r1 will already be "1" here since the last call to wait(), no need to set it
write_out:
	bkpt



.align 2
.section .text.2
.globl info
info:

//checkvals (none)

//now the fixed-length footer:
	//load address of this code
	.word 0x1FFFC000

	//flash staging area in ram
	.word 0x20000000

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
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000054
	.word 0x0000004a
	.word 0x00000008
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "Fujitsu/Spansion/Cypress FM3-series"
