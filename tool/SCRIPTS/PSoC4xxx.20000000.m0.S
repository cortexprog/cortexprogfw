.thumb
.section .text

//this chip is weird for it erased flash is all zeroes...

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
	bl erase_all
	
	//erase at address (uint32_t addr)
	bl erase_block
	
	//write block  (uint32_t addr)
	bl write_block
	
	//cpuid func (none needed)
	.word 0


//calling convention is sorta standard except where noted, AND except that r7 always is CPUSS_BASE
	

calckey:		//r0 = opcode -> r2 = key
	mov   r2, #0xd3
	add   r2, r0
	lsl   r2, #8
	add   r2, #0xb6
	bx    lr

psoc_syscall:		//r0 = opcode, r1 = arg
	mov   r2, #0x80
	lsl   r2, #24
	orr   r0, r2

	str   r1, [r7, #8]
	str   r0, [r7, #4]

	//waiting seems necessary despite docs
1:
	ldr   r0, [r7, #4]
	lsr   r0, #28
	bcs   1b
	
	ldr   r0, [r7, #8]		//return value

	bx    lr

syscall_noram:	//r0 = opcode, r1 = 16-bit param (in low bits)
	push  {lr}
	bl    calckey
	lsl   r1, #16
	add   r1, r2
	bl    psoc_syscall
	pop   {pc}

syscall_ram:	//r0 = opcode, r1 = ram where params are (0th word will be modified)
	push  {lr}
	bl    calckey
	ldr   r3, [r1, #0]
	lsr   r3, #16
	lsl   r3, #16
	add   r3, r2
	str   r3, [r1, #0]
	bl    psoc_syscall
	pop   {pc}


.globl init_stage_1
init_stage_1:
	//we do not know CPUSS base. we do know SFLASH base. read silicon id there and decide on CPUSS base based on it
	//psoc4000 has silicon id 0xA40 and CPUSS base 0x40100000 and SPCIF base 0x40110000
	//psoc41xx/42xx have silicon id 0x4xx and CPUSS base 0x40000000 and SPCIF base 0x400E0000
	mov   r7, #0x4			// == not-psoc4000 CPUSS_BASE 0x40000000
	lsl   r7, #28
	ldr   r6, =0x400E0000	// == not-psoc4000 SPCIF_BASE 0x400E0000
	
	ldr   r0, =0x20000800	//stack at end of ram of smallest possible ram (needed for syscalls)
	mov   sp, r0
	
	ldr   r0, =0x0FFFF144	//we assume all PSoC4100/4200 have bits8..11 as 0x04 and test for that
	ldr   r0, [r0]
	lsl   r0, #20
	lsr   r0, #28
	cmp   r0, #4
	beq   not_psoc_4000

psoc_4000:
	lsr   r0, r7, #10	// 0x00100000
	orr   r7, r0		// == psoc4000 CPUSS_BASE 0x40100000
	mov   r0, #3
	lsl   r0, #16
	add   r6, r0		// == psoc4000 SPCIF_BASE 0x40110000

	//setup clocks (4000 only)
	mov   r0, #0x15
	ldr   r1, =cmd
	bl    syscall_noram		//XXX: ERRATA HERE (docs say use RAM syscall, but only this works)

not_psoc_4000:
	//nothing

psoc_type_determined:
	//at this point R6 is SPCIF base and R7 is CPUSS BASE
	//actually reading flash size form geometry register is pointless as it does not account for wounding and supervisor reservation
	//we COULD assume those match SFLASH settings but this may not be true
	//we CANNOT read actual registers that control these as they are PRIVILEGED-only
	//instead we'll binary search using the checksum api (as it will report error on invalid rows
	
	mov   r4, #0			//range_lo
	mov   r5, #0x40			//range_hi
	lsl   r5, #8
1:
	cmp   r4, r5
	beq   3f				//done
	add   r1, r4, r5
	lsr   r1, #1			//guess
	mov   r8, r1
	mov   r0, #0x0B
	bl    syscall_noram
	lsr   r0, #29			//shifts out into carry lowest bit of return code. for success it would be lowest bit of 0xA == 0, for failure, the lowest bit of 0x0F == 1
	bcc   2f				//success - guess was too low
//guess was too high		//failure - guess was too high
	mov   r5, r8
	sub   r5, #1
	b     1b
//guess was too low
2:
	mov   r4, r8
	b     1b


//done
3:
	
	adr   r0, uploaddata	//store mem map there
	add   r4, #1			//r4 has last valid row id. number of rows is one more than that. calc that
	mov   r1, #1			//1 memory area
	mov   r2, #0			//base is 0x00000000
	ldr   r3, [r6]			//load SPCIF_GEOMETRY
	lsl   r3, #8
	lsr   r3, #30			//row size divided by 64
	cmp   r3, #2
	beq   init_fail			//we do not support non-power-of-two page size (and no PsoC4 has them AFAIK)
	cmp   r3, #3
	bne   1f
	mov   r3, #2
1:
	add   r3, #6			//r3 is now the shift needed for page size
	lsl   r4, #8
	add   r4, r3			//set write block sz
	lsl   r4, #8
	add   r4, r3			//set erase block sz
	stmia r0!, {r1, r2, r4}
	adr   r0, uploaddata	//we stored mem map there
	
	mov   r1, #1
	lsl   r1, r3
	mov   r11, r1			//store block size in r11
	lsr   r4, #16			//r4 again is num rows
	mul   r1, r4			//r1 now total flash size
	mov   r10, r1			//store total flash size in r10
	
	
	//names (none)
	movs  r1, #0
	
	//flags (none)
	movs  r2, #0
	bkpt
	
init_fail:
	mov r0, #0
	bkpt



.globl erase_all
erase_all:
	bl    clear_uploaddata_area
	mov   r4, #0
	mov   r5, #1		//by default we succeed
	
1:
	cmp   r10, r4		//r10 = flashsz
	beq   2f			//done
	
	mov   r0, r4
	add   r4, r11		//r11 = blocksz
	
	bl    do_block_erase_action
	cmp   r0, #0
	bne   1b
	mov   r5, #0		//record the error but try to erase the rest...
	b     1b
	
2:
	mov   r0, r5
	bkpt
	
	

.globl erase_block	//addr param in r0
erase_block:
	mov   r4, r0
	bl    clear_uploaddata_area
	mov   r0, r4
	bl    do_block_erase_action
	bkpt
	

clear_uploaddata_area:
	mov   r2, r11
	mov   r1, #0
	ldr   r3, =uploaddata
1:
	stmia r3!, {r1}
	sub   r2, #4
	bne   1b
	bx    lr
	
do_block_erase_action:	//addr param in r0, returns nonzero on success
	mov   r1, r0
	mov   r0, #5
	b     write_erase_block
	

.globl write_block		//addr param in r0, returns nonzero on success
write_block:

	mov   r1, r0
	mov   r0, #6
	bl    write_erase_block
	bkpt


write_erase_block:	//cmd to use is in r0, addr in r1, returns nonzero on success
	push  {r0-r1, r5-r6, lr}
	ldr   r1, =cmd
	mov   r2, #0
	str   r2, [r1]

	mov   r5, r11
	sub   r2, r5, #1	//byte written is size minus one, so sayeth api
	str   r2, [r1, #4]	//number of bytes goes here
	
	//load latches
	mov   r0, #0x04
	bl    syscall_ram
	lsr   r0, #28
	sub   r0, #0x0E
	beq   out_bigpop	//error
	
	//program
	pop   {r0, r2}		//cmd in r0, addr in r2
	ldr   r1, =cmd

	mov   r6, #0
1:
	add   r6, #1
	sub   r2, r5
	bpl   1b
	sub   r2, r6, #1

	lsl   r2, #16
	str   r2, [r1]
	
	bl    syscall_ram
	lsr   r0, #28
	sub   r0, #0x0E		//nonzero on error

	pop   {r5-r6, pc}

out_bigpop:
	pop   {r1, r2, r5, r6, pc}





.align 4
cmd:
	.word 0
	.word 0
	
uploaddata:	//you'd think we do not need this, but we do...do not ask...seemingly if DAP does not write these bytes, CPU gets unhappy
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0


.align 4
.section .text.2
.globl info
info:

//checkvals (verify that TST_MODE exists) since our cpuid is so broad. checking for SFLASH would be better, but on protected mde chips it cannot be done
	.word 0x40030014
	.word 0x00000004
	.word 0x00000004

//now the fixed-length footer:
	//load address of this code
	.word 0x20000000

	//flash staging area in ram
	.word uploaddata

	//number of checkvals above
	.word 1
	
	//tagetid on swd bus (most likely zero)
	.word 0
	
	//cpuid value masks from cpuid (8 of them)
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffc0
	.word 0xffffffff
	.word 0xffffffcf
	.word 0xffffff4f
	
	//cpuid match values from cpuid (8 of them)
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000080
	.word 0x00000040
	.word 0x0000000b
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "PSoC 4xxx"