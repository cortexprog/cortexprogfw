.section .vecs

.globl __reset_vector_ptr
__reset_vector_ptr:
__bad_interrupt:
	rjmp __reset_vector

//vector table for cpu (forward all but INT0)
	rjmp __reset_vector		//this vector is never used as BL does not give it to us
	rjmp __vector_2
	rjmp __vector_3
	rjmp __vector_4
	rjmp __vector_5
	rjmp __vector_6
	rjmp __vector_7
	rjmp __vector_8
	rjmp __vector_9
	rjmp __vector_10
	rjmp __vector_11
	rjmp __vector_12
	rjmp __vector_13
	rjmp __vector_14
	
	
.section .text

__reset_vector:
	
	//load data start and end, copy data
	ldi  r17, hi8(__data_end_bss_start)
	ldi  r26, lo8(__data_start)
	ldi  r27, hi8(__data_start)
	ldi  r30, lo8(__data_data)
	ldi  r31, hi8(__data_data)

	//copy data
	rjmp __data_copy_loop_check
	
__data_copy_loop:
	lpm  r0, Z+
	st   X+, r0

__data_copy_loop_check:
	cpi  r26, lo8(__data_end_bss_start)
	cpc  r27, r17
	brne __data_copy_loop
	
	//load bss start and end ptrs
	ldi  r18, hi8(__bss_end)
	
	//clear bss
	rjmp __bss_clear_loop_check

__bss_clear_loop:
	st   X+, r1

__bss_clear_loop_check:
	cpi  r26, lo8(__bss_end)
	cpc  r27, r18
	brne __bss_clear_loop


	rjmp main

