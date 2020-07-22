.syntax unified
.section .text


//there is at least 16K of ram and 128K of flash
//nRF5x always has 1K-sized pages of flash
//we stage data at 0x20000400 and start our SP at the same address
//this code will live at 0x20000000
//we compile as m0 to support nRF51x
//we DO NOT Support nonzero area 0 size			XXX: check for this in init


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
	//page erase cannot be done always because only code in area 0 can erase pages in area 0,
	//and we're in ram and not in there. lock bits sometimes allow this but too much trouble
	//to be sure and even figure out if an address is in area 0
	//thus we only do our best
	bl page_erase
	
	//write block  (uint32_t addr)
	bl page_write
	
	//cpuid func (none needed)
	.word 0
	


.globl init_stage_1
init_stage_1:
	
	ldr   r4, =0x20000400			//our staging area and SP start
	mov   sp, r4
	movs  r7, #0x10
	lsls  r7, #24					//r7 = &FICR
	ldr   r5, [r7, #0x10]			//r5 = flash page size in bytes	(save this for later)
	ldr   r3, [r7, #0x14]			//r3 = num flashpages
	
	//calculate page size log into r7 (will fail if flash pages are 1 byte big)
	movs  r7, #0
	lsrs  r6, r5, #1
1:
	adds  r7, #1
	lsrs  r6, #1
	bcc   1b

	movs  r1, #2					//2 areas
	movs  r2, #0					//flash base addr
	lsls  r3, #8
	add   r3, r7					//write page size is flash page size
	lsls  r3, #8
	add   r3, r7					//erase page size is flash page size
	ldr   r6, =0x10001000			//UICR base
	ldr   r7, =0x00010A0A			//1x 1K page
	stmia r4!, {r1, r2, r3, r6, r7}	//store area count and both areas
	mov   r11, r4					//for names
//commented out lines are not needed because they are already at that value above
//	movs  r1, #2					//2 names
//	movs  r2, #0					//addr 0x000000
	ldr   r3, =strFLASH
//	movs  r6, =0x10001000			//addr 0x000000
	ldr   r7, =strUICR
	stmia r4!, {r1, r2, r3, r6, r7}	//store name count and both names

	ldr   r7, =0x4001e400			//NVMC "ready" regsiter address for later use (NVMC + 0x400)
	ldr   r6, =0x4001e500			//NVMC all other registers base (NVMC + 0x500)
	ldr   r4, =0x20000400			//leave staging area addr in r4 (expected by all entrypts)
	ldr   r0, =0x20000400			//where we stored the memory map
	
	ldr   r1, =0x10000000			//r1 = &FICR
	ldr   r1, [r1, #0x28]			//r1 = FICR.CLENR0
	adds  r1, #1
	bne   nonzero_area_0_sz
	ldr   r1, =0x10001000			//ra = &UICR
	ldr   r1, [r1, #0x00]			//r1 = UCIR.CLENR0
	adds  r1, #1
	beq   zero_area_size

nonzero_area_0_sz:					//nonzero area0 size means we can do nothing. return false
	movs  r0, #0
	bkpt

zero_area_size:						//zero area0 size means success

	//names
	mov   r1, r11

	//flags (none)
	movs  r2, #0
	bkpt

strFLASH:
	.ascii "FLASH\0"
strUICR:
	.ascii "UICR\0"


.align 1

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
	
	movs  r0, #1
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
	
	movs  r0, #1
	bkpt

.globl page_erase
page_erase:
	bl    flash_ready_wait		//patience is a virtue
	movs  r1, #2
	str   r1, [r6, #0x04]		//enable flash erase
	
	str   r0, [r6, #0x08]		//start page erase
	bl    flash_ready_wait		//patience is a virtue
	movs  r1, #0
	str   r1, [r6, #0x04]		//enable flash read
	
	movs  r0, #1
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
	.word 0xfffffff8
	.word 0xffffffff
	.word 0xffffff3f
	.word 0xffffffff
	
	//cpuid match values from cpuid (8 of them)
	.word 0x00000002
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000040
	.word 0x0000001c
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "Nordic nRF5x"
