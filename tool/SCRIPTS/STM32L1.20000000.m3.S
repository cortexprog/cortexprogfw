.syntax unified
.thumb
.section .text

//C-m3
//we have 4K of RAM at 0x20000000 or more
//we have flash at 0x08000000

//sp starts at 0x20000400
//we stage data at 0x20000400 too


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
	ldr  sp, =0x20000400	// a good safe value
	ldr  r7, =0x40023C00	// flash unit base

	//unlock flash regs if needed
	ldr   r1, [r7, #0x04]
	lsrs  r1, #1
	itttt cs
	ldrcs r1, =0x89ABCDEF
	ldrcs r2, =0x02030405
	strcs r1, [r7, #0x0C]
	strcs r2, [r7, #0x0C]
	
	//unlock flash programming regs if needed
	ldr   r1, [r7, #0x04]
	lsrs  r1, #2
	itttt cs
	ldrcs r1, =0x8C9DAEBF
	ldrcs r2, =0x13141516
	strcs r1, [r7, #0x10]
	strcs r2, [r7, #0x10]

	//unlock flash opt regs if needed
	ldr   r1, [r7, #0x04]
	lsrs  r1, #3
	itttt cs
	ldrcs r1, =0xFBEAD9C8
	ldrcs r2, =0x24252627
	strcs r1, [r7, #0x14]
	strcs r2, [r7, #0x14]

	//get device ID and flash size (as per device id). for cat1 and cat2 also record eeprom size, for others calc it
	ldr   r1, =0x1FF80000
	ldr   r0, =0xE0042000	//script check vals verified device ID starts with 0x04, so we only care for bottom 8 bits
	ldr   r0, [r0]
	and   r0, #0xFF
	cmp   r0, #0x16
	beq   is_cat_1_or_2
	cmp   r0, #0x26
	bne   isnt_cat_1_or_2
	
is_cat_1_or_2:
	ldrh  r8, [r1, 0x4C]
	movs  r5, #4			//4K of eeprom
	b     memsz_reg_loaded

isnt_cat_1_or_2:
	ldrh  r8, [r1, 0xCC]
	cmp   r0, #0x27
	it    eq
	moveq r5, #8
	beq   memsz_reg_loaded
	cmp   r0, #0x36			//could be cat 3 and not cat 4. determine by reading past where eeprom would end for cat3
	bne   not_devid_0x436
	
	mov   r0, #2
	ldr   r1, =0x08082000
	bl    syscall			//after this r0 is 1 if the addr is readable (cat4 has > 8k of eeprom), 0 if addr isnt readable (cat3 has 8k only)
	cmp   r0, #0
	ite   eq
	moveq r5, #8
	movne r5, #12
	b     memsz_reg_loaded
	
not_devid_0x436:
	cmp   r0, #0x37
	it    eq
	moveq r5, #16
	beq   memsz_reg_loaded

	//if we're here, device id is unknown - return an error
	movs  r0, #0
	bkpt

memsz_reg_loaded:
	//For DEV_ID = 0x429, only LSB part of F_SIZE: F_SIZE[7:0] is valid.
	cmp   r0, #0x29
	it    eq
	andeq r8, 0xFF
	
	//For DEV_ID = 0x436, the field value can be 0 or 1, with 0 for 384 Kbytes and 1 for 256Kbytes.
	cmp   r8, #0x36
	ittt  eq
	andeq r8, #1
	lsleq r8, #7
	rsbeq r8, #384
	
	//at this point r8 is flash size in kbytes, r5 is eeprom size in kbytes, we'll save them also in r10 and r11 (for flash and eeprom respectively)
	mov   r10, r8
	mov   r11, r5
	mov   r6, sp
	movs  r2, #2				//2 areas
	ldr   r3, =0x08000000		//flash srarts at 0x08000000
	lsl   r4, r8, #10			//flash amount in blocks of 256K, lsl 8 (to save us an lsl 8 next)
	adds  r4, #0x08
	lsls  r4, #8
	adds  r4, #0x08
	stmia r6!, {r2-r4}			//save flash info
	ldr   r2, =0x08080000
	lsls  r5, #15				//KBytes to doublewords, lsl 8 (to save us an lsl 8 next)
	adds  r5, #3				//8 byte write possible	(actually 1 byte is but that is too granular and we don't need it)
	lsls  r5, #8
	adds  r5, #3				//8 byte erase possible	(actually 4 byte is but that is too granular and we don't need it)
	stmia r6!, {r2, r5}			//save EEPROM info

	//names
	movs  r1, r6
	movs  r2, #2				//2 names
	ldr   r3, =0x08000000
	ldr   r4, =strFLASH
	stmia r6!, {r2, r3, r4}
	ldr   r3, =0x08080000
	ldr   r4, =strEEPROM
	stmia r6!, {r3, r4}

	mov   r0, sp				//done
	
	//flags (none)
	movs  r2, #0
	bkpt

strFLASH:
	.ascii "FLASH\0"
strEEPROM:
	.ascii "EEPROM\0"

.align 1

wait:
	ldr  r12, [r7, #0x18]
	lsrs r12, #1
	bcs  wait
	bx   lr


.globl all_erase
all_erase:
	ldr   r0, =0x08000000
	lsl   r1, r10, #10
	mov   r2, #256
	bl    do_mass_erase
	ldr   r0, =0x08080000
	lsl   r1, r11, #10
	mov   r2, #8
	bl    do_mass_erase
	b     out_success_nowait

do_mass_erase:		//(addr, len, step)
	push  {r4-r6, lr}
	movs  r4, r0
	movs  r5, r1
	movs  r6, r2
	
do_mass_erase_loop:
	cmp   r5, #0
	it    eq
	popeq {r4-r6, pc}
	bl    wait
	mov   r0, r4
	bl    do_block_erase
	adds  r4, r6
	subs  r5, r6
	b     do_mass_erase_loop

.globl block_erase	//assumes valid address passed
block_erase:
	bl    do_block_erase

out_success_nowait:
	movs  r0, #1
	bkpt

do_block_erase:		//subroutine
	ldr   r1, =0x08080000
	cmp   r0, r1
	bcs   erase_eeprom_doubleword
	mov   r2, #0x0200		//erase bit
	str   r2, [r7, #0x04]
	adds  r2, #0x08			//program enabled
	str   r2, [r7, #0x04]
	movs  r1, #0
	str   r1, [r0]
	b     wait

erase_eeprom_doubleword:
	mov   r2, #0x0200		//ERASE bit
	str   r2, [r7, #0x04]
	adds  r2, #0x0010		//DATA bit
	str   r2, [r7, #0x04]
	movs  r1, #0
	str   r1, [r0, #0x00]
	str   r1, [r0, #0x04]
	b     wait

.globl block_write
block_write:
	ldr   r6, =0x20000400
	ldr   r1, =0x08080000
	movs  r4, r0
	cmp   r0, r1
	bcs   write_eeprom_doubleword
	
write_flash_page:
	movs  r5, #2

write_flash_halfpage:
	mov   r2, #0x0400		//half page program
	str   r2, [r7, #0x04]
	adds  r2, #0x08			//program enabled
	str   r2, [r7, #0x04]
	bl    wait
	mov   r2, #32
load_loop:
	ldmia r6!, {r0}
	stmia r4!, {r0}
	subs  r2, #1
	bne   load_loop
	bl    wait
	subs  r5, #1
	bne   write_flash_halfpage
	b     out_success_nowait

write_eeprom_doubleword:
	mov   r2, #0x0400		//FPRG bit
	str   r2, [r7, #0x04]
	adds  r2, #0x0010		//DATA bit
	str   r2, [r7, #0x04]
	bl    wait
	ldmia r6!, {r0, r1}
	str   r0, [r4, #0x00]
	str   r1, [r4, #0x04]
	bl    wait
	b     out_success_nowait
	




.align 4
.section .text.2
.globl info
info:
	//device id
	.word 0xE0042000
	.word 0xff070fc0
	.word 0x10000400

	//verify rom is present
	.word 0x1ff00000
	.word 0x00000000
	.word 0x00000000
	
	//verify option bytes are present
	.word 0x1ff80000
	.word 0x00000000
	.word 0x00000000
	

//now the fixed-length footer:
	//load address of this code
	.word 0x20000000

	//flash staging area in ram
	.word 0x20000400

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
	.word 0x00000016
	.word 0x00000004
	.word 0x0000000a
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "STM32FL1xx"
