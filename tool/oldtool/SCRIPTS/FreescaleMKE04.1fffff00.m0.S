.thumb
.section .text


//entry points (also we breakpoint this place and use it as debugger call)
.globl entrypts
entrypts:

	//flash size (will be replaced by init code)
	flashsz:
	.word 8192
	
	//flash block size for erase/write
	.word 512

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
	bl all_erase
	
	//erase at address (uint32_t addr)
	bl page_erase
	
	//write block  (uint32_t addr)
	bl write_page
	
	//custom command (...)
	.word 0




go:	//r0 = cmd, r1 = cobbix it needs
	
	//clear status
	mov  r2, #0x30
	strb r2, [r7, #5]


	movs r2, #0
	strb r2, [r7, #1]	//write CCOBIX for cmd write
	strb r0, [r7, #9]	//write command
	strb r1, [r7, #1]	//write CCOBIX command needs
	movs r1, #0x80
	strb r1, [r7, #5]
	nop
	nop

wait:
	ldrb r1, [r7, #5]
	lsr  r1, #8
	bcc  wait
	
	bx   lr


loadaddr:
	movs r1, #1
	strb r1, [r7, #1]	//write CCOBIX
	
	strb r0, [r7, #0x08]
	lsr  r0, #8
	strb r0, [r7, #0x09]
	lsr  r0, #8
	
	movs r1, #0
	strb r1, [r7, #1]	//write CCOBIX
	
	strb r0, [r7, #0x08]
	bx   lr

.globl init_stage_1
init_stage_1:

	//disable wdt but allow it to be updated later
	ldr   r0, =0x40052000
	
	ldr   r1, =0x20C5	//unclock it just in case
	strh  r1, [r0, #2]
	ldr   r1, =0x28D9
	strh  r1, [r0, #2]
	
	ldr   r1, =0x0101	//docs say we must write these all and in this order - do it
	strh  r1, [r0, #0x06]
	ldr   r1, =0xffff	//docs say we must write these all and in this order - do it
	strh  r1, [r0, #0x04]
	
	mov   r1, #0x41
	strb  r1, [r0, #1]

	mov   r1, #0x20		//allow later updates
	strb  r1, [r0]

	//disable caches & shit
	ldr   r0, =0xF000300C
	ldr   r1, =0x0001BC00
	str   r1, [r0]

	//bus divider
	ldr   r7, =0x40048000	//SIM
	mov   r0, #1
	lsl   r0, #24
	str   r0, [r7, #0x1C]	//SIM->CLKDIV = 0x01000000

	//flash clock
	ldr   r7, =0x40020000	//FTMRE base
	mov   r0, #0x17			//FDIV value for 24MHz bus
	strb  r0, [r7, #0x03]	//write it
	
	//set up FLL (ref clk is 37.5KHz in this chip)
	ldr   r0, =0x40064000	//ICS base
	mov   r1, #0x20			//BDIV = 1
	strb  r1, [r0, #1]
	mov   r1, #4			//use internal reference clock for FLL (1280x ref clk) at 48MHz, bus at 24MHz
	strb  r1, [r0, #0]
	
fll_lockwait:
	ldrb  r1, [r0, #4]
	lsr   r1, #7
	bcc   fll_lockwait

	//todo: get sizes
	bkpt


.globl all_erase
all_erase:
	mov   r0, #0x08
	mov   r1, #0x00
	bl    go
	bkpt

.globl page_erase
page_erase:
	bl    loadaddr
	mov   r0, #0x0A
	mov   r1, #0x01
	bl    go
	bkpt

.globl write_page	//addr param in r0
write_page:
	movs  r6, #0
	movs  r5, r0
	ldr   r4, =0x20000100
loop:
	add   r0, r5, r6
	bl    loadaddr
	
	mov   r3, #2
miniloop:
	strb  r3, [r7, #1]		//write CCOBIX
	ldrh  r0, [r4, r6]		//get halfword
	strb  r0, [r7, #0x08]	//write low byte
	lsr   r0, #8
	strb  r0, [r7, #0x09]	//write high byte
	add   r3, #1
	add   r6, #2
	cmp   r3, #6
	bne   miniloop
	
	mov   r0, #0x06
	mov   r1, #0x05
	bl    go
	
	lsr   r0, r6, #10
	bcc   loop
	
	bkpt


.align 4
.section .text.2
.globl info
info:

//checkvals
	.word 0x40048000
	.word 0xff000000
	.word 0x04000000


//now the fixed-length footer:
	//load address of this code
	.word 0x1fffff00

	//flash staging area in ram
	.word 0x20000100

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
	.word 0x00000000
	.word 0x000000e0
	.word 0x00000008
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "Freecale MKE04"