.syntax unified
.section .text

//we have at least 2K of ram at 0x20000000, at least 16K of flash at 0x00000000
//64-byte pages, 4 pages per row
//at least ATMSAD20 and ATSAMR21 work these way. likely all others too

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

	ldr   r7, =0x41004000		//load NVMC address
	movs  r0, #3			//interrupts to disable
	str   r0, [r7, #0x0C]		//disable all interrupts from NVMC
	ldr   r0, [r7, #0x04]		//load CTRLB
	movs  r1, #0x80			//set CTRLB.MANW
	orrs  r0, r1
	str   r0, [r7, #0x04]		//save CTRLB
	
	ldr   r0, =0x20000400
	mov   sp, r0
	ldrh  r3, [r7, #0x08]		//NVMC.PARAM.NVMP - num pages in devices (there are always 4 pages per row)
	ldrb  r4, [r7, #0x0A]		//NVMC.PARAM.PSZ
	movs  r2, #7
	ands  r4, r2
	adds  r4, #3			//page size in log scale
	movs  r2, #1
	lsls  r2, r4			//page size in bytes
	mov   r11, r2			//save page size in r11 as well
	
	//we erase a row and write a page at a time (on all devices this is safe for ram size)
	//in many cases it is faster to write more, but ascertaining there is enough ram is a bit of a PITA
	movs  r1, #2			//2 memory areas
	movs  r2, #0			//based at 0x00000000
	lsrs  r3, #2			//4 pages per row - we erase a row at a time. convert num page to num rows
	lsls  r3, #8			//make space for "write size"
	adds  r3, r4			//save "write size"
	lsls  r3, #8			//make space for "erase size"
	adds  r4, #2			//calculate "erase size" = 4x page size
	adds  r3, r4			//save "erase size"
	
	ldr   r5, =0x00804000		//addr of aux area
	subs  r4, #2			//page to "page size"
	movs  r6, #1			//1 such piece
	lsls  r6, #8			//make space for "write size"
	adds  r6, r4			//write size
	lsls  r6, #8			//make space for "erz size"
	adds  r6, r4			//erz size
	
	
	stmia r0!, {r1,r2,r3,r5,r6}	//save "num areas" and the area (flash)
	movs  r1, r0
	movs  r2, #2			//2 names
	movs  r3, #0			//addr 0
	ldr   r4, =strFLASH
	ldr   r6, =strAUX0
	stmia r0!, {r2,r3,r4,r5,r6}	//save "num areas" and the area (flash)
	
	mov   r0, sp

out:
	//flags (none)
	movs  r2, #0
	bkpt

strFLASH:
	.ascii "FLASH\0"
strAUX0:
	.ascii "AUX0\0"

.align 1


	
flash_op:
	movs  r1, #0xA5			//add key to command
	lsls  r1, #8
	orrs  r0, r1
	movs  r1, #2
	str   r1, [r7, #0x14]		//clear error flag
	str   r0, [r7, #0x00]		//start command

1:					//wait till it is ready for next command
	ldr   r0, [r7, #0x14]
	lsrs  r0, #1
	bcc   1b			//due to the shift above, the "error" bit is now in the lowest bit of r0

	movs  r1, #1
	ands  r0, r1			//now r0 is just the error bit
	subs  r1, r0			//r1 is now 1 in case of success
	movs  r0, r1
	bx    lr

.globl mass_erase
mass_erase:
	ldrh  r4, [r7, #0x08]		//NVMC.PARAM.NVMP - num pages in devices (there are always 4 pages per row)
	mov   r5, r11			//page size in bytes
	muls  r4, r5			//device size in bytes
	lsls  r5, #2			//row size in bytes
	movs  r6, #0
	
mass_erz_loop:
	movs  r0, r6
	bl    do_pg_erz
	cmp   r0, #0
	beq   out
	adds  r6, r5
	cmp   r6, r4
	bne   mass_erz_loop
	
	//now erase aux page
	ldr   r0, =0x00804000
	bl    do_pg_erz

	bkpt

.globl page_erase
page_erase:
	bl    do_pg_erz
	bkpt
	

do_pg_erz:
	push  {r4, lr}
	movs  r4, r0
	str   r0, [r0]			//put proper addr into reg (by doing this write)
	movs  r0, #0x41			//unlock before erase
	bl    flash_op
	cmp   r0, #0
	beq   out_pg_erz
	
	lsrs  r4, #23			//nonzero if aux area
	bne   pg_erz_aux
	
pg_erz_main:
	movs  r0, #2
	b     pg_erz_cmd_selected
	
pg_erz_aux:
	movs  r0, #5
	//fallthrough
	
pg_erz_cmd_selected:
	bl    flash_op
out_pg_erz:
	pop   {r4, pc}
	
	
.globl page_write
page_write:

	movs  r4, r0
	movs  r0, #0x44			//page bufffer clear (just in case)
	bl    flash_op
	cmp   r0, #0
	beq   out
	ldr   r5, =0x20000400
	mov   r6, r11
load_loop:
	ldr   r0, [r5]
	str   r0, [r4]
	adds  r5, #4
	adds  r4, #4
	subs  r6, #4
	bne   load_loop

	movs  r0, #0x41			//unlock
	bl    flash_op
	cmp   r0, #0
	beq   out

	lsrs  r4, #23			//nonzero if aux area
	bne   pg_wri_aux
	
pg_wri_main:
	movs  r0, #4
	b     pg_wri_cmd_selected
	
pg_wri_aux:
	movs  r0, #6
	//fallthrough
	
pg_wri_cmd_selected:	
	bl    flash_op
	bkpt

.align 4
.section .text.2
.globl info
info:

//checkvals
	.word 0x41002118		//DSU.DID
	.word 0xfff00000		//verify processor, family, ATMSAMD20 is series 0, ATSAMR21 is series 1
	.word 0x10000000

//now the fixed-length footer:
	//load address of this code
	.word 0x20000000

	//flash staging area in ram
	.word 0x20000400

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
	.word 0x000000d0
	.word 0x000000fc
	.word 0x00000009
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "ATSAM series with NVM-type flash"

