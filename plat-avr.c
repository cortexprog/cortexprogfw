#include "plat.h"



struct UsbStringDescr {
	uint8_t len;
	uint8_t type;
	uint16_t chars[];
} __attribute__((aligned(4)));

static const PROGMEM struct UsbStringDescr mDevStr = {24, 3, {'C', 'o', 'r', 't', 'e', 'x', 'P', 'r', 'o', 'g', '1'}};
static struct UsbStringDescr mSnumStr = {.chars = {0, 0, 0, 0, 0, 0, 0, 0}};


//sig area is 0x2B bytes, but we read 0x2C, because fuck it
static void calc_snum(uint8_t *val)
{
	uint8_t bits = (1 << RSIG) | (1 << SPMEN), t, t2, smallIdx, bigIdx;
	uint16_t addr;
	
	asm volatile(
		"0:					\n\t"
		"	ldi  %5, 4		\n\t"
		"1:					\n\t"
		"	out  0x37, %2	\n\t"	//spmcsr = 0x37
		"	lpm  %3, Z+		\n\t"
		"	ld	 %4, X		\n\t"
		"	eor  %4, %3		\n\t"
		"	st   X+, %4		\n\t"
		"	dec  %5			\n\t"
		"	brne 1b			\n\t"
		"	sbiw r26, 4		\n\t"
		"	dec  %6			\n\t"
		"	brne 0b			\n\t"
		:"=z"(addr),		"=x"(val),	"=a"(bits),	"=a"(t),	"=a"(t2),	"=a"(smallIdx), "=a"(bigIdx)
		:"0"((uint16_t)0),	"1"(val),	"2"(bits),											"6"((uint8_t)(0x2C / 0x04))
		:"cc", "memory"
	);
}

static void print_snum(uint16_t *dst, const uint8_t *src)
{
	uint8_t smallIdx, bigIdx, t, t2;
	
	asm volatile(
		"0:					\n\t"
		"	ldi  %2, 2		\n\t"
		"	ld   %4, X+		\n\t"
		"1:					\n\t"
		"	mov  %5, %4		\n\t"
		"	andi %5, 15		\n\t"
		"	cpi  %5, 10		\n\t"	//carry set if "%5 < 10"
		"	brcs 2f			\n\t"	//taken if "%5 < 10"
		"	subi %5, -0x27	\n\t"	//0x27 = 'a' - '0' - 10
		"2:					\n\t"
		"	subi %5, -0x30	\n\t"	//0x30 = '0'
		"	st   Z, %5		\n\t"
		"	adiw r30, 2		\n\t"
		"	swap %4			\n\t"
		"	dec  %2			\n\t"
		"	brne 1b			\n\t"
		"	dec  %3			\n\t"
		"	brne 0b			\n\t"
		:"=z"(dst),	"=x"(src),	"=a"(smallIdx), "=a"(bigIdx),	"=a"(t),	"=a"(t2)
		:"0"(dst),	"1"(src),					"3"((uint8_t)4)
		:"cc", "memory"
	);
}

static void setupUsb(bool setSnum)
{
	uint8_t snum[4] = {0,};
	
	if (setSnum) {
		
		//get "snum"
		calc_snum(snum);
		
		//print it
		mSnumStr.len = sizeof(struct UsbStringDescr) + sizeof(uint16_t) * 8;
		mSnumStr.type = 3;
		print_snum(mSnumStr.chars, snum);
	}
	
	//use
	usbReenumerate(&mDevStr, setSnum ? &mSnumStr : 0);
}

void platInit(bool enableCdcAcmUart, uint8_t blVer)
{
	(void)blVer;	//for now
	
	//this all not needed as we trust our bootloader to disable WDT and not to touch the default (good) reset value of MCUCR
	if (0) {
		//wdt
		cli();
		wdt_reset();
		wdt_disable();
		sei();
		
		//ports
		MCUCR &=~ 0x40; //pullups on
	}
	
	(void)enableCdcAcmUart;
	setupUsb(true);
}

	