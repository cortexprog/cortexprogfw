.syntax unified
.section .text


//there is at least 16K of ram and 128K of flash
//nRF51x always has 1K-sized pages of flash
//we stage data at 0x20000400 and start our SP at the same address
//this code will live at 0x20000000


//entry points (also we breakpoint this place and use it as debugger call)
.globl entrypts
entrypts:

	//flash size (will be replaced by init code)
	flashsz:
	.word 0
	
	//flash block size for erase/write (will be replaced by init code)
	.word 0

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
	bl mass_erase
	
	//erase at address (uint32_t addr)
	//page erase cannot be done always because only code in area 0 can erase pages in area 0,
	//and we're in ram and not in there. lock bits sometimes allow this but too much trouble
	//to be sure and even figure out if an address is in area 0
	//thus we only do our best
	bl page_erase
	
	//write block  (uint32_t addr)
	bl page_write
	
	//custom command (...)
	.word 0
	


.globl init_stage_1
init_stage_1:

	ldr   r4, =0x20000400	//our staging area and SP start
	mov   sp, r4
	ldr   r7, =0x10000010	//FICR base
	ldr   r6, =flashsz	//where we'll store flash size and block size
	ldmia r7!, {r1,r2}	//r1 = flash page size, r2 = num flash blocks
	mov   r0, r2
	muls  r0, r1		//r0 = flash size in bytes
	stmia r6!, {r0, r1}
	ldr   r7, =0x4001e400	//NVMC "ready" regsiter address for later use (NVMC + 0x400)
	ldr   r6, =0x4001e500	//NVMC all other registers base (NVMC + 0x500)
	mov   r5, r1		//safe flash page size in r5
	
	movs  r1, #0
	str   r1, [r6, #0x04]		//enable flash read
	bkpt

.globl flash_ready_wait
flash_ready_wait:
	ldr   r1, [r7]
	lsrs  r1, #1
	bcc   flash_ready_wait
	bx    lr

.globl mass_erase
mass_erase:
	bl    flash_ready_wait		//patience is a virtue
	movs  r1, #2
	str   r1, [r6, #0x04]		//enable flash erase
	movs  r1, #1
	str   r1, [r6, #0x0c]		//do mass erase
	bl    flash_ready_wait		//patience is a virtue
	movs  r1, #0
	str   r1, [r6, #0x04]		//enable flash read
	bkpt
	
.globl page_write
page_write:
	bl    flash_ready_wait		//patience is a virtue
	movs  r1, #1
	str   r1, [r6, #0x04]		//enable flash write
	
	movs  r2, #0
1:
	ldr   r1, [r4, r2]
	str   r1, [r0, r2]
	bl    flash_ready_wait		//patience is a virtue
	adds  r2, r2, #4
	cmp   r2, r5
	bne   1b
	
	movs  r1, #0
	str   r1, [r6, #0x04]		//enable flash read
	bkpt

.globl page_erase
page_erase:
	bl    flash_ready_wait		//patience is a virtue
	ldr   r1, =0x10000000		//load FICR.CLENR0
	ldr   r1, [r1, #0x28]
	adds  r2, r1, #1
	bne   1f
	ldr   r1, =0x10001000		//load UICR.CLENR0
	ldr   r1, [r2]
1:
	adds  r2, r1, #1
	bne   1f
	movs  r1, #0
1:								//r1 = bytes in area 0
	movs  r2, #0x08				//r2 = ERASEPCR1 - r6
	cmp   r0, r1				//flags now set on addr - size_page_0    -> MI if "area 0" else "area 1"
	bmi   1f
	adds  r2, #0x08				//r2 = ERASEPCR0 - r6
1:								//[r6, r2] = ERASEPCRx - proper reg for erasing this page
	movs  r1, #2
	str   r1, [r6, #0x04]		//enable flash erase
	
	str   r0, [r6, r2]			//start page erase
	
	bl    flash_ready_wait		//patience is a virtue
	movs  r1, #0
	str   r1, [r6, #0x04]		//enable flash read
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
	.word 0xffffffff
	
	//cpuid match values from cpuid (8 of them)
	.word 0x00000002
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000006
	.word 0x00000040
	.word 0x0000005c
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "Nordic nRF5x"
