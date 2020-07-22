.thumb
.section .text

//this chip is weird for it erased flash is all zeroes...
//two cores (in some cases): M4F and M0+. To allow this script to run on either, it is complied for ARMv6M
//flash writing is done via IPC to the Cortex-M0+ core which we might (or might not) be currently running on
//RAM is at 0x08000000, and we have at least 128K
//flash is at 0x10000000, and we have 512K - 2M. But, as always with PSoC, we never really know how much there really is
//sflash at 0x16000000, 32K (sz?)
//wflash at 0x14000000, 32K (sz?)
//let's start our stack at 0x08001000 and stage there too
//docs are wrong about this too, actually where docs say is data there really is just a pointer to data 


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
	

psoc_syscall:				//param in r0 -> reply in r0

							//find out which core we're on & pick the proper IPC struct
	ldr   r2, =0x40230000	//ipc struct to use
	ldr   r3, =0x40231000	//ipc interrupt stuct
	ldr   r1, =0x40210400
	ldr   r1, [r1]
	lsl   r1, #20			//get ms bit
	lsr   r1, #28
	beq   running_on_m0
	cmp   r1, #0x0E
failure_loop:
	bne   failure_loop

running_on_m4:
	add   r2, #0x20			//IPC struct 0 - offset 0x20
	b     ipc_struct_picked

running_on_m0:
	//fallthrough			//IPC struct 0 - offset 0x00, nothing to do
	
ipc_struct_picked:			//R2 now points to our main IPC struct, r3 to intr IPC struct
							//force-release IPC struct
	mov   r1, #0
	str   r1, [r2, #0x04]
							//clear all status bits for intr struct
	mov   r1, #7
	str   r1, [r3, #0x00]
	lsl   r1, #16
	str   r1, [r3, #0x00]
	
	
loop_wait_aquire:			//wait to aquire lock
	ldr   r1, [r2, #0x00]
	lsl   r1, #1
	bcc   loop_wait_aquire

	str   r0, [r2, #0x0c]	//write our data
	
							//enable C-M0 to get notified
	mov   r1, #7
	lsl   r1, #16
	str   r1, [r3, #0x08]	//set intr mask

	//reset c-m0 vtor (only if we are on c-m0)
	ldr   r0, =0xE000ED08
	mov   r1, #0
	str   r1, [r0]

	mov   r1, #1
	str   r1, [r2, #0x08]	//notify
	
	mov r3, #0
loop_wait_reply:			//wait for lock to be released
	ldr   r1, [r2, #0x10]
	add r3, #1
	lsl   r1, #1
	bcs   loop_wait_reply

	ldr   r0, [r2, #0x0c]	//read reply	
	bx    lr


psoc_retval_to_ret:			//r0 = syscall ret, on return r0 = bool success
	lsr   r1, r0, #24
	cmp   r1, #0x0F
	beq   out_fail
	ldr   r0, [r0]
	lsr   r0, #28
	cmp   r0, #0x0A
	bne   out_fail
	mov   r0, #1
	bx    lr
out_fail:
	mov   r0, #0
	bx    lr

check_if_page_exists:		//r0 = page_num, r1 = page_type (main, work, supervisory)   => success
    push  {r3, lr}
	lsl   r0, #8
	lsl   r1, #22
	mov   r2, #0x0B
	lsl   r2, #24
	orr   r2, r0
	orr   r2, r1
	str   r2, [sp]
	mov   r0, sp
	bl    psoc_syscall
	bl    psoc_retval_to_ret
	pop   {r3, pc}




//binary searc hhow many pages of a ginve type there are using check_if_page_exists()
//warning: at leas ton some PSoC6 chips we cna checksum more pages than we can read
//after this we'll also verify the length by using our syscall
get_num_pages_of_page_type:			//r0 = page type (main, work, supervisory)   => num_pages
	push  {r0, r4-r6, lr}
	ldr   r4, =0x1FFF				//max + 1
	mov   r5, #0					//min

guess:
	cmp   r5, r4
	bcs   converged
	add   r6, r4, r5
	add   r6, #1
	lsr   r6, #1
	mov   r0, r6
	ldr   r1, [sp, #0]				//pushed_r0
	bl    check_if_page_exists
	cmp   r0, #0
	beq   guess_was_high
guess_was_low:
	mov   r5, r6
	b     guess
guess_was_high:
	sub   r4, r6, #1
	b     guess

converged:
	add   r0, r5, #1				//num pages is page number that we converged on plus one
	pop   {r1, r4-r6, pc}


init_stage_1:
	//set up sp
	ldr   r0, =0x08001000
	mov   sp, r0

	//see if we're in NMI mode and if so return to SVCALL priority (exc # 11)
	mrs   r0, IPSR
	lsl   r0, #26
	lsr   r0, #26
	cmp   r0, #2
	bne   not_in_nmi
	
exit_nmi_mode:
	adr   r0, not_in_nmi	//pc to "return" to
	ldr   r1, =0x0100000B	//sr to set
	push  {r0, r1}
	sub   sp, #6 * 4		//we do not care what ends up in r0,r1,r2,r3,r12,lr
	
	mov   r7, #0x0E	//0xFFFFFFF1
	mvn   r7, r7
	bx    r7


not_in_nmi:
	//get num pages of supervisory flash and lsl it by 16 into r6, also permanently save it into r9
	mov   r0, #2
	bl    get_num_pages_of_page_type
	mov   r9, r0
	lsl   r6, r0, #16
	
	//get num pages of work flash and lsl it by 16 into r4, also permanently save it into r10
	mov   r0, #1
	bl    get_num_pages_of_page_type
	mov   r10, r0
	lsl   r4, r0, #16
	
	//get num pages of main flash and lsl it by 16 into r2, also permanently save it into r11
	mov   r0, #0
	bl    get_num_pages_of_page_type
	mov   r11, r0
	lsl   r2, r0, #16
	
	//load the value for 512B write and erase sizes and ORR it into each size we got above
	ldr   r0, =0x0909
	orr   r2, r0
	orr   r4, r0
	orr   r6, r0
	
	//prepare the flash info structures
	mov   r7, sp
	mov   r0, #3			//3 areas
	ldr   r1, =0x10000000	//main flash base
	ldr   r3, =0x14000000	//work flash base
	ldr   r5, =0x16000000	//sflash base
	mov   r8, r7
	stmia r7!, {r0,r1,r2,r3,r4,r5,r6}

	//prepare the flash names structure
	mov   r0, #3			//3 areas
	ldr   r2, =strMAIN		//main flash name
	ldr   r4, =strWORK		//work flash name
	ldr   r6, =strSFLASH	//sflash name
	mov   r9, r7
	stmia r7!, {r0,r1,r2,r3,r4,r5,r6}
	
	//stash all the return values where they are expected
	mov   r0, r8
	mov   r1, r9
	mov   r2, #0			//no flags
	bkpt

strMAIN:
	.ascii "MAIN FLASH\0"

strWORK:
	.ascii "WORK FLASH\0"

strSFLASH:
	.ascii "SFLASH\0"

.align 1


erase_block_by_addr:		//addr -> success
	mov   r1, r0
	mov   r0, #0x1C
	lsl   r0, #24
	push  {r0, r1, lr}
	mov   r0, sp
	bl    psoc_syscall
	bl    psoc_retval_to_ret
	pop   {r2, r3, pc}


erase_region:				//r0 = addr, r1 = num_pages -> success
	push  {r4-r5, lr}
	mov   r4, r0
	add   r5, r1, #1		//easier loop maintenance
	b     erase_region_loop_check
erase_region_loop:
	mov   r0, r4
	bl    erase_block_by_addr
	cmp   r0, #0
	beq   erase_region_out
	mov   r0, #1
	lsl   r0, #9
	add   r4, r0
erase_region_loop_check:
	sub   r5, #1
	bne   erase_region_loop
	mov   r0, #1

erase_region_out:
	pop   {r4-r5, pc}


erase_all:					//we do not erase SFLASH since it might be important, we do erase work flash and main flash
	ldr   r0, =0x10000000
	mov   r1, r11
	bl    erase_region
	cmp   r0, #0
	beq   erase_all_out

	ldr   r0, =0x14000000
	mov   r1, r10
	bl    erase_region
	
erase_all_out:
	bkpt

erase_block:
	bl    erase_block_by_addr
	bkpt


write_block:
	mov   r2, r0
	ldr   r0, =0x06000100		//write, blocking
	ldr   r1, =0x00010100		//do integrity check, data in SRAM
	ldr   r3, =0x08001000		//data pointer
	push  {r0-r3}
	mov   r0, sp
	bl    psoc_syscall
	bl    psoc_retval_to_ret
	add   sp, #16
	bkpt
	

.align 4
.section .text.2
.globl info
info:

//checkvals
	.word 0x08000000	//RAM here is weird enough to be worth a check
	.word 0x00000000
	.word 0x00000000
	
	.word 0x16000000	//SFLASH here is weird enough to be worth a check
	.word 0x00000000
	.word 0x00000000
	
	.word 0x14000000	//WFLASH here is weird enough to be worth a check
	.word 0x00000000
	.word 0x00000000
	
	.word 0x40210400	//CPUSS_IDENTITY will always have our "MS" as 0x0F
	.word 0x00000f00
	.word 0x00000f00

//now the fixed-length footer:
	//load address of this code
	.word 0x08000000

	//flash staging area in ram
	.word 0x08001000

	//number of checkvals above
	.word 4
	
	//SBZ
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
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000041
	.word 0x0000002b
	.word 0x00000020
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "PSoC 6"