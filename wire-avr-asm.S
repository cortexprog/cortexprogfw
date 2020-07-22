#include "wire-avr.h"
;The wire protocol helper for speed
;clock idles low, data is valid on rising clock



.globl llWireBusRead	; ap = r24, a23 = r22, valP = r20:r21, return is in r24
llWireBusRead:
	;prepare clock and io bit
	ldi   r27, (1 << BIT_NO_SWDCK);
	
	;save pointer in Z
	movw  r30, r20

	;calculate a3
	mov   r23, r22
	lsr   r23

	;calculate parity
	mov   r26, r22
	eor   r26, r23
	eor   r26, r24	;r26's lower bit is the opposite of parity bit we need to set

	;set bus to output
	sbi   DDR_SWD, BIT_NO_SWDIO
	
	;send start
	sbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;send ap
	sbrs  r24, 0
	cbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;send the read bit
	sbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;send a2
	sbrs  r22, 0
	cbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;send a3
	sbi   PORT_SWD, BIT_NO_SWDIO
	sbrs  r23, 0
	cbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;send parity
	cbi   PORT_SWD, BIT_NO_SWDIO
	sbrs  r26, 0
	sbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;send stop bit
	cbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;turn line to input and enable pullup
	sbi   PORT_SWD, BIT_NO_SWDIO
	cbi   DDR_SWD, BIT_NO_SWDIO
	
	;clock out park bit
	out   PIN_SWD, r27
	out   PIN_SWD, r27

	;clock out turn bit
	out   PIN_SWD, r27
	out   PIN_SWD, r27

	ldi   r25, 3
	ldi   r26, 1
	
	;read status bit
	clr   r24
rread_status_loop:
	out   PIN_SWD, r27
	sbic  PIN_SWD, BIT_NO_SWDIO
	or    r24, r26
	out   PIN_SWD, r27
	lsl   r26
	dec   r25
	brne  rread_status_loop
	
	cpi   r24, 1
	breq  read_data
	cpi   r24, 7	// no data means we still clock out the data bits
	brne  rout

read_data:
	ldi   r25, 4 ;byte counter
	ldi   r19, 0 ;parity will be the 7-th bit of this reg

	;read uint32 and calc parity
read_word_loop:
	ldi   r26, 8 ;bit counter
read_bit_loop:
	out   PIN_SWD, r27
	lsr   r18
	sbic  PIN_SWD, BIT_NO_SWDIO
	ori   r18, 0x80
	out   PIN_SWD, r27
	eor   r19, r18
	dec   r26
	brne  read_bit_loop
	st    Z+, r18
	dec   r25
	brne  read_word_loop
	
	;read parity and compare
	out   PIN_SWD, r27
	ldi   r18, 0x80
	sbic  PIN_SWD, BIT_NO_SWDIO
	eor   r19, r18
	out   PIN_SWD, r27
	;now 7-th bit is set if parity failed

rout:
	;clock out turn bit
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;set parity fail bit in result
	andi  r19, 0x80
	or    r24, r19
	
	sbi   DDR_SWD, BIT_NO_SWDIO
	cbi   PORT_SWD, BIT_NO_SWDIO
	ret

.globl llWireBusWrite	; ap = r24, a23 = r22, val = r18:r21, return is in r24
llWireBusWrite:
	;prepare clock and io bit
	ldi   r27, (1 << BIT_NO_SWDCK)

	;calculate a3
	mov   r23, r22
	lsr   r23

	;calculate parity
	mov   r26, r22
	eor   r26, r23
	eor   r26, r24	;r26's lower bit is the parity bit we need to set

	;set bus to output
	sbi   DDR_SWD, BIT_NO_SWDIO
	
	;send start
	sbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;send ap
	sbrs  r24, 0
	cbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;send the read bit
	cbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;send a2
	sbi   PORT_SWD, BIT_NO_SWDIO
	sbrs  r22, 0
	cbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;send a3
	sbi   PORT_SWD, BIT_NO_SWDIO
	sbrs  r23, 0
	cbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;send parity
	sbi   PORT_SWD, BIT_NO_SWDIO
	sbrs  r26, 0
	cbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;send stop bit
	cbi   PORT_SWD, BIT_NO_SWDIO
	out   PIN_SWD, r27
	out   PIN_SWD, r27
	
	;turn line to input and enable pullup
	sbi   PORT_SWD, BIT_NO_SWDIO
	cbi   DDR_SWD, BIT_NO_SWDIO
	
	;clock out park bit
	out   PIN_SWD, r27
	out   PIN_SWD, r27

	;clock out turn bit
	out   PIN_SWD, r27
	out   PIN_SWD, r27

	ldi   r25, 3
	ldi   r26, 1
	
	;read status bit
	clr   r24
wread_status_loop:
	out   PIN_SWD, r27
	sbic  PIN_SWD, BIT_NO_SWDIO
	or    r24, r26
	out   PIN_SWD, r27
	lsl   r26
	dec   r25
	brne  wread_status_loop
	
	;clock out turn bit
	out   PIN_SWD, r27
	out   PIN_SWD, r27

	cpi   r24, 1
	breq  wwrite
	cpi   r24, 7	//no data - we still clock out the bits
	breq  wwrite
	
	sbi   DDR_SWD, BIT_NO_SWDIO
	cbi   PORT_SWD, BIT_NO_SWDIO
	ret
wwrite:

	ldi   r26, (1 << BIT_NO_SWDIO);

	;set bus to output
	sbi   DDR_SWD, BIT_NO_SWDIO

	;prepare to calculate parity
	clr   r23
	
	;clock out data
	;since AVR allows us to address registers as memory, we'll do so here
	ldi   r30, 18	;point Z to where in RAM r18 is stored
	ldi   r31, 0
	
wwrite_byte_loop:
	ld    r22, Z+
	ldi   r25, 8
	
wwrite_bit_loop:
	cbi   PORT_SWD, BIT_NO_SWDIO
	sbrc  r22, 0
	out   PIN_SWD, r26
	out   PIN_SWD, r27
	eor   r23, r22
	lsr   r22
	dec   r25
	out   PIN_SWD, r27
	brne  wwrite_bit_loop
	cpi   r30, 22
	brne  wwrite_byte_loop
	
	;clock out parity
	cbi   PORT_SWD, BIT_NO_SWDIO
	sbrc  r23, 0
	out   PIN_SWD, r26
	out   PIN_SWD, r27
	out   PIN_SWD, r27

	sbi   DDR_SWD, BIT_NO_SWDIO
	cbi   PORT_SWD, BIT_NO_SWDIO
	ret
