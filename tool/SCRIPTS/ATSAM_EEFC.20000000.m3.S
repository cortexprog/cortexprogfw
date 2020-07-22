.syntax unified
.section .text

//we have at least 2K of ram at 0x20000000, at least 16K of flash at 0x00000000


//due to errata we must <<Set the number of Wait States (WS) to 6 (FWS = 6) during the programming>>

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
	bl cpuid


/*
	each short is (from MSB to LSB, rounded to nibbles for readability):
		BITS	NAME				USE
		8		devid				device id to match
		4		efcbase				which EFC base it is
		1		flash_split			second bank (if present) is not at same addr as first ends
		3		flash base			see table
		
		
efc bases (if second exists, it is always 0x200 after first):
		0 - 0x400E0800
		1 - 0x400E0A00
		2 - 0x400E0C00

flash bases:
		0 - 0x00080000
		1 - 0x00400000
		2 - 0x01000000

currently we do not care for flash layout since we just use pages which are always 512 bytes :D


*/

duel_efc_devices:
	.word 

devinfos_0x400E0940:
	.short 0x1021	// E70
	.short 0x1121	// S70
	.short 0x1221	// V71
	.short 0x1321	// V70
	.short 0x8310	// 3AxC
	.short 0x8410	// 3XxC
	.short 0x8510	// 3XxE
	.short 0x8610	// 3XxG
	.short 0		//terminator

devinfos_0x400E0740:
	.short 0x4311	// G51
	.short 0x4711	// G53/G54
	.short 0x4411	// G55
	.short 0x4511	// G55
	.short 0x9311	// 3NxA
	.short 0x9411	// 3NxB
	.short 0x9511	// 3NxC
	.short 0x8811	// {3/4}SxA
	.short 0x8911	// {3/4}SxB
	.short 0x8A11	// {3/4}SxC
	.short 0x9811	// {3/4}SDxA
	.short 0x9911	// {3/4}SDxB
	.short 0x9A11	// {3/4}SDxC
	.short 0x3C11	// 4E
	.short 0x9311	// 4NxA
	.short 0x9411	// 4NxB
	.short 0x9511	// 4NxC
	.short 0x8008	// 3UxC
	.short 0x8108	// 3UxE
	.short 0x6412	// 4CxxC
	.short 0x6612	// 4CxxE
	.short 0		//terminator

.align 2
efc_bases:
	.word 0x400E0800
	.word 0x400E0A00
	.word 0x400E0C00

flash_bases:
	.word 0x00080000
	.word 0x00400000
	.word 0x01000000

nvpsiz_flash_size_tab:
	.word 0x00000000			//  0 -> none
	.word 0x00002000			//  1 -> 8K
	.word 0x00004000			//  2 -> 16K
	.word 0x00008000			//  3 -> 32K
	.word 0						//  4 -> ?
	.word 0x00010000			//  5 -> 64K
	.word 0						//  6 -> ?
	.word 0x00020000			//  7 -> 128K
	.word 0x00028000			//  8 -> 160K
	.word 0x00040000			//  9 -> 256K
	.word 0x00080000			// 10 -> 512K
	.word 0						// 11 -> ?
	.word 0x00100000			// 12 -> 1M
	.word 0						// 13 -> ?
	.word 0x00200000			// 14 -> 2M
	.word 0						// 15 -> ?

cpuid_try:			//in: R1 = addr
	push  {r4, r5, lr}
	movs  r4, r0
	movs  r0, #2
	bl    syscall
	cmp   r0, #0
	beq   cpuid_not_this_one
	mov   r8, r1
	ubfx  r8, r1, #8, #4	//NVPSIZ
	ubfx  r1, r1, #20, #8	//arch
	movs  r0, r4
	
cpuid_loop:
	ldrh  r2, [r0], #2
	cmp   r2, #0
	beq   cpuid_not_this_one
	lsr   r3, r2, #8
	cmp   r3, r1
	bne   cpuid_loop

cpuid_this_one:
	ubfx  r0, r2, #4, #4		//efc base index
	ubfx  r1, r2, #0, #3		//flash base index
	ldr   r7, =efc_bases
	ldr   r6, =flash_bases
	ldr   r7, [r7, r0, lsl #2]	//EFC base in r7			// PRESERVED!
	ldr   r6, [r6, r1, lsl #2]	//flash base in r6			// PRESERVED!
	
	//detect dual-EFC by comparing mem sz in chipid with mem sz in eefc
	
	//save the flag for "weird flash split"
	ubfx  r4, r2, #3, #1
	
	//first, figure out our flash size in bytes and store to r8
	ldr   r0, =nvpsiz_flash_size_tab
	ldr   r8, [r0, r8, lsl #2]
	
	movs  r1, #0x00		//cmd = GETD
	movs  r2, #0x0000	//arg = 0
	movs  r0, r7
	bl    flashcmd
	cmp   r0, #0
	beq   done
	
	ldr   r0, [r7, #0x0C]	//dummy read (FL_ID)
	ldr   r0, [r7, #0x0C]	//flash size in bytes
	
	cmp   r0, r8
	beq   done
	
	//if we're here, we've decided we have a dual-EEFC chip - mark this (we store base of second flash in r11)
	cmp   r4, #0
	ite   eq
	addeq r11, r6, r0
	addne r11, r6, #0x00080000

done:
cpuid_not_this_one:
	pop   {r4, r5, pc}
	

cpuid:
	movs  r7, #0
	movs  r11, #0
	ldr   r0, =devinfos_0x400E0940
	ldr   r1, =0x400E0940
	bl    cpuid_try
	ldr   r0, =devinfos_0x400E0740
	ldr   r1, =0x400E0740
	bl    cpuid_try
	cmp   r7, #0
	ite   eq
	moveq r0, #0
	movne r0, #1
	ldr   r1, =0xACEFACE5UL
	bkpt

flashcmd:	//r0 = eefc, r1 = cmd, r2 = arg. jumps to flashwait on return: bool in r0 for success
	lsls  r2, #8
	orrs  r1, r2
	orr   r1, #0x5A000000
	str   r1, [r0, #0x04]
	//fallthrough 

flashwait:	//r0 = eefc, on return bool in r0 for success
	ldr   r1, [r0, #0x08]
	lsrs  r1, #1
	bcc   flashwait
	//r0 no has error bits in lower 2 bits
	ands  r1, #3
	ite   eq
	moveq r0, #1
	movne r0, #0
	bx    lr

.globl init_stage_1
init_stage_1:
	//set wait states to 6
	ldr   r0, [r7, #0x00]
	movs  r1, #6
	bfi   r0, r1, #8, #4
	str   r0, [r7, #0x00]
	
	//if dual eefc, do so for second EEFC too
	cmp   r11, #0
	itttt ne
	ldrne r0, [r7, #0x200]
	movne r1, #6
	bfine r0, r1, #8, #4
	strne r0, [r7, #0x200]
	
	//get flash info
	movs  r0, r7
	movs  r1, #0x00		//cmd = GETD
	movs  r2, #0x0000	//arg = 0
	bl    flashcmd
	cmp   r0, #0
	beq   out_r0_is_set
	
	ldr   r3, [r7, #0x0C]	//dummy read (FL_ID)
	ldr   r3, [r7, #0x0C]	//flash size in bytes
	ldr   r5, [r7, #0x0C]	//page size in bytes
	
	//verify page size is a power or 2, else bail
	subs  r0, r5, #1
	ands  r0, r5
	itt   ne
	movne r0, #0
	bne   out_r0_is_set
	
	//find what power it is
	clz   r5, r5
	rsb   r5, #31											// PRESERVED!
	
	//divide flash size by page size
	lsrs  r3, r5

	//produce results		(always write as if 2 areas, but numAreas will be proper)
	mov   r0, sp
	movs  r2, r6			//flash base
	lsls  r3, #8
	adds  r3, r5
	lsls  r3, #8
	adds  r3, r5

	cmp   r11, #0			//# memory areas == 1 + isDualEfc
	ite   eq
	moveq r1, #1
	movne r1, #2

	stmia r0!, {r1,r2,r3}
	
	//produce second area	(only used in case of dual efc)
	mov   r2, r11
	stmia r0!, {r2, r3}
	
	mov   r0, sp

out_r0_is_set:

	//names (none)
	movs  r1, #0

	//flags (none)
	movs  r2, #0
	bkpt

.align 1

.globl mass_erase
mass_erase:
	movs  r0, r7
	movs  r1, #0x05		//cmd = EA
	movs  r2, #0x0000	//arg = 0
	bl    flashcmd
	movs  r8, r0
	cmp   r11, #0
	itttt ne
	addne r0, r7, #0x200
	movne r1, #0x05		//cmd = EA
	movne r2, #0x0000	//arg = 0
	blne  flashcmd
	and   r0, r8
	bkpt

resolve_dual:			//nonstandard - adjusts r0 and returns eefc base in r10	(honors GPNVM bit as expected to point us to the rigth controller in all cases)
	push  {r4, lr}
	mov   r10, r7
	cmp   r11, #0
	beq   not_dual
	cmp   r0, r11
	bcc   first

second:
	add   r10, #0x200
	sub   r0, r11
	b     handle_gpnvm_bit

first:
	sub   r0, r6

handle_gpnvm_bit:
	movs  r4, r0
	movs  r0, r7
	movs  r1, #0x0D			//cmd = GGPB
	movs  r2, #0x0000		//arg = 0
	bl    flashcmd
	cmp   r0, #0
	beq   resolve_dual_out
	ldr   r0, [r7, #0x0C]	//read first 32 GPNVM bits
	tst   r0, #4
	it    ne
	eorne r10, #0x200
	
resolve_dual_out:
	movs  r0, r4
	pop   {r4, pc}

not_dual:
	sub   r0, r6
	pop   {r4, pc}


.globl page_erase
page_erase:
	bl    resolve_dual

	lsrs  r2, r0, r5	//arg is page number
	
	mov   r1, #0xFFFFFFFF
	movs  r3, #1
	lsls  r3, r5

buf_erz:
	str   r1, [r0], #4
	subs  r3, #4
	bne   buf_erz

	movs  r1, #0x03		//cmd = EWP
	movs  r0, r10
	bl    flashcmd
	bkpt
	
	
.globl page_write
page_write:
	movs  r4, r0			//we need original addr to do the actual write to the flash write buffer (must be at dst addr)
	bl    resolve_dual
	mov   r12, r4

	lsrs  r2, r0, r5	//arg is page number
	ldr   r1, =0x20000400
	movs  r3, #1
	lsls  r3, r5

buf_fill:
	ldr   r4, [r1], #4
	str   r4, [r12], #4
	subs  r3, #4
	bne   buf_fill
	
	movs  r1, #0x01		//cmd = WP
	movs  r0, r10
	bl    flashcmd
	bkpt

.align 4
.section .text.2
.globl info
info:

//checkvals
	.word 0xE000ED00	//arm cpuid - to make sure that chips we cnanot read at all do not bother listingthis as a potential option
	.word 0xff000000
	.word 0x41000000

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
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "ATSAM series with EEFC-type flash "







/*
	most have IAP for flashing, write size is 512 bytes
	
	
					ROM BASE		FLASH BASE		SECTORS			TYPE		CHIPID ADDR		EFC0 ADDR		EFC1 ADDR	USERPG ADDR		CHIPIDMASK	CHIPIDVAL
	SAM E70			0x00800000		0x00400000		8,8,112,128*	EEFC		0x400E0940		0x400E0C00									FFC0F9FF	A1000800
	SAM S70			0x00800000		0x00400000		8,8,112,128*	EEFC		0x400E0940		0x400E0C00									FFC0F9FF	A1000800
	SAM V70			0x00800000		0x00400000		8,8,112,128*	EEFC		0x400E0940		0x400E0C00									FFC0F9FF	A1000800
	SAM V71			0x00800000		0x00400000		8,8,112,128*	EEFC		0x400E0940		0x400E0C00									FFF0F9FE	A1200800
	SAM G51			0x00800000		0x00400000		8,8,48,64*		EEFC		0x400E0740		0x400E0A00									FFFFFFF7	243B09E0
	SAM G53			0x00800000		0x00400000		8,8,48,64*		EEFC		0x400E0740		0x400E0A00									FFFFFFF0	247E0AD0
	SAM G54			0x00800000		0x00400000		8,8,48,64*		EEFC		0x400E0740		0x400E0A00									FFFFFFF0	247E0AE0
	SAM G55			0x00800000		0x00400000		8,8,112,128*	EEFC		0x400E0740		0x400E0A00									FFEFFFFF	24470AE0
	SAM 3N			0x00800000		0x00400000		???				EEFC		0x400E0740		0x400E0A00									FF80F0FE	29000060
	SAM 3S			0x00800000		0x00400000		???				EEFC		0x400E0740		0x400E0A00									FF80F0FF	28800060
	SAM 3S8/3D8		0x00800000		0x00400000		???				EEFC		0x400E0740		0x400E0A00									FE80F0FF	28800060
	SAM 4E			0x00800000		0x00400000		8,8,48,64*		EEFC		0x400E0740		0x400E0A00									FFFFFFFF	A3CC0CE0
	SAM 4N			0x00800000		0x00400000		8,8,48,64*		EEFC		0x400E0740		0x400E0A00									FF80F9FF	290008E0
	SAM 4S			0x00800000		0x00400000		8,8,48,64*		EEFC		0x400E0740		0x400E0A00									FE80F0FE	288000E0	[same as other 4s)
	SAM 4C			0x02000000		0x01000000		8,8,48,64*		EEFC		0x400E0740		0x400E0A00									FF4EF9F8	A64C08E0
	
	SAM 3X/3A		0x00100000		0x00080000		???				EEFC		0x400E0940		0x400E0A00		0x400E0C00					FF8AF8FF	280A0860
	
	SAM 4Sx32/Sx16	0x00800000		0x00400000		8,8,48,64*		EEFC		0x400E0740		0x400E0A00		0x400E0C00					FE80F0FE	288000E0	[same as other 4s)
	
	SAM 3U			0x00180000		0x00080000		???				EEFC		0x400E0740		0x400E0800		0x400E0A00					FFE0F0FF	28000061


GPNVM2 has various purposes on various chips. on some it remaps flash areas on some only which gets mapped at 0
on 3X, 3A, 3U, and 4S, if set it swaps map areas of flash0 & flash1
on 3S it only seems to select which bank get mapped at 0 and does not swap them
but since 3S and 4S are the same arch and 4S says it does swap, and that doc is newer assume this is error in 3S doc and assume all GPNVM2 bits swap


*/