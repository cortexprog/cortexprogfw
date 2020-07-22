.syntax unified
.thumb
.section .text

//the bootloader entrypt for flash is 0x1fff1ff0
//we have 4K of RAM at 0x10000000 or more

//this code lives in start (0x10000000 - 0x10000100)
//we start our sp at 0x10000400
//we stage data at 0x10000400 too
//we write 1K at a time


//we can use blacnkcheck api to find flash size (it give diff errors fo rout of bounds than blank/nonblank



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
	bl init_stage_2
	
	//init stage 3
	.word 0
	
	//mass erase
	bl all_erase
	
	//erase at address (uint32_t addr)
	bl erase_block
	
	//write block  (uint32_t addr)
	bl write_1K
	
	//cpuid func (none needed)
	bl cpuid

devices_at_F8:
    .word 0x28010541        //LPC1345FHN33
    .word 0x28010541        //LPC1345FBD48
    .word 0x08018542        //LPC1346FHN33
    .word 0x08018542        //LPC1346FBD48
    .word 0x08020543        //LPC1347FHN33
    .word 0x08020543        //LPC1347FBD48
    .word 0x08020543        //LPC1347FBD64
    .word 0x3A010523        //LPC1315FHN33
    .word 0x3A010523        //LPC1315FBD48
    .word 0x1A018524        //LPC1316FHN33
    .word 0x1A018524        //LPC1316FBD48
    .word 0x1A020525        //LPC1317FHN33
    .word 0x1A020525        //LPC1317FBD48
    .word 0x1A020525        //LPC1317FBD64
	.word 0

devices_at_F4:
    .word 0x2C42502B        //LPC1311FHN33
    .word 0x2C40102B        //LPC1313FHN33
    .word 0x2C40102B        //LPC1313FBD48
    .word 0x3D01402B        //LPC1342FHN33
    .word 0x3D01402B        //LPC1342FBD48
    .word 0x3D00002B        //LPC1343FHN33
    .word 0x3D00002B        //LPC1343FBD48
    .word 0x1816902B        //LPC1311FHN33/01
    .word 0x1830102B        //LPC1313FHN33/01
    .word 0x1830102B        //LPC1313FBD48/01
	.word 0
	

cpuid:
	//LPC 1311/1313/1342/1343 has cpuid at 0x400483F4
	//LPC 1315/1316/1317/1345/1346/1347 has it at 0x400483F4
	//don't ask...
	//luckily both addrs are safe to read on both devices, so we'll just read them
	
	ldr   r4, =0x400483F0
	ldr   r0, [r4, #4]
	ldr   r1, =devices_at_F4
	bl    cpuid_check
	ldr   r0, [r4, #8]
	ldr   r1, =devices_at_F8
	bl    cpuid_check
	movs  r0, #0
	
cpuid_done:
	ldr   r1, =0xACEFACE5UL
	bkpt

cpuid_check:		//r0 = cpuid val, r1 = list of devices, does not return in case of success
	ldmia r1!, {r2}
	cmp   r2, #0
	it    eq
	bxeq  lr
	cmp   r2, r0
	bne   cpuid_check
	movs  r0, #1
	b     cpuid_done


lpcSyscall:			//params as per arm spec, call number in r12
	push  {r0-r3, lr}
	push  {r12}
	mov   r0, sp	//params
	sub   sp, #16	//for results
	mov   r1, sp
	ldr   r2, =0x1fff1ff1
	blx   r2
	pop   {r0-r3}	//load return vals
	add   sp, #20
	pop   {pc}

blankCheckSector:	//check just one sector for blankness		(sec_num) -> (ret)
	mov   r12, #53
	movs  r1, r0
	b     lpcSyscall

prepareSector:		//prepare just one sector					(sec_num) -> (ret)
	movs  r1, r0
	//fallthrough
prepareSectors:		//prepare multiple sectors					(sec_first, sec_last) -> (ret)
	mov   r12, #50
	b     lpcSyscall

eraseSector:		//erase just one sector						(sec_num) -> (ret)
	movs  r1, r0
	//fallthrough
eraseSectors:		//erase multiple sectors					(sec_first, sec_last) -> (ret)
	mov   r12, #52
	mov   r2, #12000
	b     lpcSyscall

writePage:		//write just one page (1K)						(dst, src) -> (ret)
	mov   r12, #51
	mov   r2, #1024
	mov   r3, #12000
	b     lpcSyscall




.globl init_stage_1
init_stage_1:

	//setup watchpoint on 0x2fc  - when this is read flash is ready via bootloader making it so
	movs  r0, #0 /* set watchpoint */
	ldr   r1, =0x2fc
	movs  r2, #2
	movs  r3, #7
	bl    syscall
	
	//jump to reset vector
	ldr   r0, =0x1fff0000
	ldmia r0!, {r1, r2}
	mov   sp, r1
	bx    r2
	
	//done (but never reached because watchpoint)
	bkpt


.globl init_stage_2
init_stage_2:
	//clear watchpoint
	movs  r0, #1 /* clear watchpoint */
	bl    syscall

	//setup SP
	ldr   sp, =0x10000400
	
	//map flash at 0
	ldr   r0, =0x40048000
	movs  r1, #2
	str   r1, [r0]
	
	//identify how much flash we have (without using undocumented features), store it in r7
	movs  r7, #0
size_find_loop:
	adds  r0, r7, #1		//this api takes a valid sector number, so account for this to produce proper answer
	adds  r7, #1
	bl    blankCheckSector
	cmp   r0, #7
	bne   size_find_loop
	
	//produce the map
	mov   r0, sp
	movs  r1, #1			//1 memory map
	movs  r2, #0			//base address 0x00000000
	lsls  r3, r7, #8
	adds  r3, #10			//1K blocks for write
	lsls  r3, #8
	adds  r3, #12			//4K blocks for erase
	stmia r0!, {r1-r3}
	
	//return it
	mov   r0, sp
	
	//names (none)
	movs  r1, #0

	//flags (none)
	movs  r2, #0
	bkpt



.globl all_erase
all_erase:
	movs  r0, #0
	subs  r1, r7, #1
	bl    prepareSectors
	cmp   r0, #0
	bne   out_fail
	
	movs  r0, #0
	subs  r1, r7, #1
	bl    eraseSectors
	cmp   r0, #0
	bne   out_fail
	
out_success:
	movs  r0, #1
	bkpt

out_fail:
	movs  r0, #0
	bkpt

.globl write_1K	//addr param in r0
write_1K:
	movs  r4, r0	//save address
	lsrs  r0, #12	//convert addr to sector number
	bl    prepareSector
	cmp   r0, #0
	bne   out_fail
	movs  r0, r4	//dst
	ldr   r1, =0x10000400
	bl    writePage
	cmp   r0, #0
	bne   out_fail
	b     out_success

erase_block:
	lsrs  r0, #12	//convert addr to sector number
	movs  r4, r0	//save address
	bl    prepareSector
	cmp   r0, #0
	bne   out_fail
	movs  r0, r4	//sector to erase
	bl    eraseSector
	cmp   r0, #0
	bne   out_fail
	b     out_success
	
	

.align 2
.section .text.2
.globl info
info:

//checkvals
	.word 0x1FFF1FF0	//verify IAP location exists and is readable (it is a good day to start checking for this chip)
	.word 0x00000000
	.word 0x00000000
	
	.word 0x1FFF0000	//verify rom begins with a valid stack pointer
	.word 0xfff00003
	.word 0x10000000
	
	.word 0x1FFF0004	//verify rom has a valid reset vector
	.word 0xFFFF0001
	.word 0x1FFF0001
	
	.word 0x400483F4	//cpuid is either here or at F8, either way F4 is always readable - verify this
	.word 0x00000000
	.word 0x00000000
	

//now the fixed-length footer:
	//load address of this code
	.word 0x10000000

	//flash staging area in ram
	.word 0x10000400

	//number of checkvals above
	.word 4
	
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
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "NXP LPC13xx"
