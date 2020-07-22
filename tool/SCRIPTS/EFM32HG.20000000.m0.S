.syntax unified
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
	bl mass_erase
	
	//erase at address (uint32_t addr)
	bl page_erase
	
	//write block  (uint32_t addr)
	bl page_write
	
	//cpuid func (none needed)
	.word 0
	


.globl init_stage_1
init_stage_1:

	ldr   r0, =0x20000400
	mov   sp, r0
	movs  r6, #1			//convenient to have a 1 around
	
	ldr   r7, =0x400c0000	//save MSC base addr in r7 for later
	ldr   r1,=0x1b71
	str   r1, [r7, #0x3c]	//unlock regs
	str   r6, [r7, #0x08]	//enable flash writes

	//create memory map
	movs  r1, #3
	stmia r0!, {r1}			//3 memory areas
	
	//first area is flash

	ldr   r1, =0x0FE081F8
	ldrh  r4, [r1]			//flash amount (in kb)
	movs  r1, #0			//base addr 0x00000000
	lsls  r4, #8
	adds  r4, #10			//write size 1K
	lsls  r4, #8
	adds  r4, #10			//erase size 1K
	stmia r0!, {r1, r4}
	
	//second is USERBITS area (1K)
	ldr   r1, =0x0FE00000	//base is 0x0FE00000
	ldr   r2, =0x00010A0A	//1 area, 1K in size for erase and write
	
	//third is LOCKBITS area (1K)
	ldr   r3, =0x0FE04000	//base is 0x0FE04000
	movs  r4, r2			//1 area, 1K in size for erase and write
	
	//store them
	stmia r0!, {r1-r4}
	
	//export names
	movs  r1, r0
	movs  r2, #3			//3 names
	movs  r3, #0
	ldr   r4, =strFLASH
	stmia r0!, {r2, r3, r4}
	ldr   r3, =0x0FE00000
	ldr   r4, =strUSERDATA
	stmia r0!, {r3, r4}
	ldr   r3, =0x0FE04000
	ldr   r4, =strLOCKBITS
	stmia r0!, {r3, r4}
	
	mov   r0, sp
	
	//flags (none)
	movs  r2, #0
	bkpt

strFLASH:
	.ascii "FLASH\0"
strUSERDATA:
	.ascii "USERDATA\0"
strLOCKBITS:
	.ascii "LOCKBITS\0"

.align 1

.globl flash_ready_wait
flash_ready_wait:
1:	//branch to GLOBAL label generates long branch instr
	ldr   r0, [r7, #0x1c]
	lsrs  r0, #1
	bcs   1b
	bx    lr

flash_get_status:
	movs  r1, #0x36
	ldr   r0, [r7, #0x1c]
	ands  r0, r1
	beq   1f					//no error
	movs  r0, #0
	bx    lr
1:
	movs  r0, #1
	bx    lr

.globl mass_erase
mass_erase:
	bl    flash_ready_wait		//patience is a virtue
	ldr   r0, =0x631A
	str   r0, [r7, #0x54]		//unlock mass erase ability
	lsls  r0, r6, #8
	str   r0, [r7, #0x0c]		//initiate mass erase
	bl    flash_ready_wait		//patience is a virtue
	str   r6, [r7, #0x54]		//re-lock mass erase ability
	bl    flash_get_status
	bkpt

.globl page_erase
page_erase:
	str   r0, [r7, #0x10]		//write address
	bl    flash_ready_wait		//patience is a virtue
	str   r6, [r7, #0x0c]		//latch address
	movs  r0, #2
	str   r0, [r7, #0x0c]
	bl    flash_ready_wait		//patience is a virtue
	bl    flash_get_status
	bkpt
	
	
.globl page_write
page_write:
	str   r0, [r7, #0x10]	//write address
	bl    flash_ready_wait
	str   r6, [r7, #0x0c]	//latch address
	ldr   r4, =0x20000400	//source addr
	ldr   r3, =1024			//block size
	adds  r3, r4			//end address
	movs  r1, #8
	
1:	//big loop
	ldr   r2, [r4]
	
2:	//wait for flash to be ready for more data
	ldr   r0, [r7, #0x1C]
	lsrs  r0, #4
	bcc   2b
	
	//write flash data
	str   r2, [r7, #0x18]
	
	//do the write
	str   r1, [r7, #0x0c]
	bl    flash_ready_wait
	
	//loop maintenance
	adds  r4, #4
	
	cmp   r4, r3
	bne   1b
	
	bl    flash_get_status
	bkpt

.align 4
.section .text.2
.globl info
info:

//checkvals (none)

//now the fixed-length footer:
	//load address of this code
	.word 0x20000000

	//flash staging area in ram
	.word 0x20000400

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
	.word 0xffffffef
	
	//cpuid match values from cpuid (8 of them)
	.word 0x00000006
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000041
	.word 0x00000031
	.word 0x0000000f
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "EFM32 Happy Gecko"
	
	//only tested on EFM32HG308F

