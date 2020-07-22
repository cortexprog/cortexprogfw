#include "efm32hg308f64.h"
#include "led-efm.h"
#include "plat.h"
#include "wire.h"
#include "../ModulaR/bl.h"





//v0 (by mistake):
// swdck is b7 (US1_CLK alt #0)
// swdio is c1 (US1_TX  alt #5, US1_RX  alt #0)
//v1..v5
// swdck is b7 (US1_CLK alt #0)
// swdio is c0 (US1_TX  alt #0)



void (*mLlWireBusWriteBits)(uint32_t val, uint32_t nbits);
uint8_t (*mLlWireBusRead)(uint1_t ap, uint8_t a23, unaligned_uint32_t *valP);
uint8_t (*mLlWireBusWrite)(uint1_t ap, uint8_t a23, uint32_t val);

void llWireBusWriteBits(uint32_t val, uint32_t nbits)
{
	mLlWireBusWriteBits(val, nbits);
}

static void llWireBusWriteBitsV0(uint32_t val, uint32_t nbits)	//8 bits max
{
	//keep data high as we switch to output mode
	GPIO->P[2].DOUTSET = 1 << 1;
	
	//switch to output mode
	GPIO->P[2].MODEL = (GPIO->P[2].MODEL &~ _GPIO_P_MODEL_MODE1_MASK) | GPIO_P_MODEL_MODE1_PUSHPULL;
	
	//clock out bits
	while (nbits) {
		
		//set data pin
		if (val & 1)
			GPIO->P[2].DOUTSET = 1 << 1;
		else
			GPIO->P[2].DOUTCLR = 1 << 1;
	
		//clock it out
		GPIO->P[1].DOUTTGL = 1 << 7;
		asm volatile ("nop\nnop\nnop\nnop\n");
		asm volatile ("nop\nnop\nnop\nnop\n");
		val >>= 1;
		nbits--;
		GPIO->P[1].DOUTTGL = 1 << 7;
	}
}

static uint32_t llWireBusReadBitsV0(uint32_t nbits)		//8 bits max
{
	uint32_t mask, val = 0;
	
	//keep data high and pullup on as we switch to input mode
	GPIO->P[2].DOUTSET = 1 << 1;
	
	//switch to input with pullup mode
	GPIO->P[2].MODEL = (GPIO->P[2].MODEL &~ _GPIO_P_MODEL_MODE1_MASK) | GPIO_P_MODEL_MODE1_INPUTPULL;

	//gpio
	for (mask = 1; nbits; nbits--) {
		
		//read
		if (GPIO->P[2].DIN & (1 << 1))
			val |= mask;
		
		//tick
		GPIO->P[1].DOUTTGL = 1 << 7;
		asm volatile ("nop\nnop\nnop\nnop\n");
		asm volatile ("nop\nnop\nnop\nnop\n");
		
		mask <<= 1;
		//tock
		GPIO->P[1].DOUTTGL = 1 << 7;
	}
	
	return val;
}

static void llWireBusWriteBitsV1toV5(uint32_t val, uint32_t nbits, bool setDirctl)	/* 4..16bits */
{
	while (!(USART1->STATUS & USART_STATUS_TXBL));		/* wait for TX to be ready */

	if (setDirctl)
		GPIO->P[1].DOUTSET = 1 << 8;					/* SWDIO is output */

	USART1->FRAME = (nbits - 4 + _USART_FRAME_DATABITS_FOUR) << _USART_FRAME_DATABITS_SHIFT;
	USART1->CMD = USART_CMD_CLEARTX | USART_CMD_CLEARRX | USART_CMD_TXTRIDIS;
	if (nbits <= 8)
		USART1->TXDATA = val;
	else
		USART1->TXDOUBLE = val;
}

static void llWireBusWriteBitsV1(uint32_t val, uint32_t nbits)
{
	llWireBusWriteBitsV1toV5(val, nbits, false);
}

static void llWireBusWriteBitsV5(uint32_t val, uint32_t nbits)
{
	llWireBusWriteBitsV1toV5(val, nbits, true);
}

static inline __attribute__((always_inline)) uint32_t llWireParity32(uint32_t v)	//mask lower bit out later
{
	v ^= v >> 1;
    v ^= v >> 2;
    v = (v & 0x11111111U) * 0x11111111U;
    return (v >> 28);	
}

//these funcs are very repetitive but they are fast!

//uint8_t toTx = 0x01 /* start */ + (ap << 1) + (read << 2) + (a23 << 3) + (transactParity << 5) + (0 << 6) /* stop */ + (1 << 7) /* "park" == 1 */;
//write 8 bits: toTx
//IF: read
//	read 4 bits (turn + reply)
//	IF: ACK or NOREPLY
//		read 34 bits (data + parity + turn)
//IF: write
//	read 5 bits (turn + reply + turn)
//	IF: ACK or NOREPLY
//		write 33 bits (data + parity)


//FOR write:
//
//		USART1->FRAME = (nbits - 4 + _USART_FRAME_DATABITS_FOUR) << _USART_FRAME_DATABITS_SHIFT;
//		USART1->CMD = USART_CMD_CLEARTX | USART_CMD_CLEARRX | USART_CMD_TXTRIDIS;
//		USART1->TXDATA or USART1->TXDOUBLE <- value

//FOR read:
//
//		USART1->FRAME = (nbits - 4 + _USART_FRAME_DATABITS_FOUR) << _USART_FRAME_DATABITS_SHIFT;
//		USART1->CMD = USART_CMD_RXEN | USART_CMD_TXEN | USART_CMD_CLEARTX | USART_CMD_CLEARRX | USART_CMD_TXTRIEN;
//		wait for !TXBL
//		USART1->TXDATA or USART1->TXDOUBLE <- 0
//		wait for !TXC
//		USART1->RXDATA or USART1->RXDOUBLE -> data


static uint8_t llWireBusReadV1toV5(uint1_t ap, uint8_t a23, unaligned_uint32_t *valP, bool setDirctl)
{
	uint32_t sta, rxv, vv;
	uint8_t *rxB = (uint8_t*)valP, *rxvvB = (uint8_t*)&vv;
	uint8_t transactParity = (ap ^ 1 ^ a23 ^ (a23 >> 1)) & 1;
	
	ledSet(LED_ACT, true);
	
	if (setDirctl)
		GPIO->P[1].DOUTSET = 1 << 8;					/* SWDIO is output */

	USART1->FRAME = USART_FRAME_DATABITS_EIGHT;
	USART1->CMD = USART_CMD_CLEARTX | USART_CMD_CLEARRX | USART_CMD_TXTRIDIS | USART_CMD_TXEN;
	USART1->TXDATA = 0x01 /* start */ + (ap << 1) + (1 << 2) /* read */ + (a23 << 3) + (transactParity << 5) + (0 << 6) /* stop */ + (1 << 7) /* "park" == 1 */;

	while (!(USART1->STATUS & USART_STATUS_TXC));		/* wait for TX idle (means rx done too) */
	
	if (setDirctl)
		GPIO->P[1].DOUTCLR = 1 << 8;					/* SWDIO is input */

	USART1->FRAME = USART_FRAME_DATABITS_FOUR;
	USART1->CMD = USART_CMD_RXEN | USART_CMD_CLEARTX | USART_CMD_CLEARRX | USART_CMD_TXTRIEN;
	USART1->TXDATA = 0x00;
	while (!(USART1->STATUS & USART_STATUS_TXC));		/* wait for TX idle (means rx done too) */
	sta = ((USART1->RXDATA << 28)) >> 29;				//mask out status bit as fast as pobbile (gcc fails to generate this so we do it for it)
	
	if (sta == BUS_SWD_ACK || sta == BUS_SWD_EMPTY) {
		
		USART1->FRAME = USART_FRAME_DATABITS_EIGHT;
		USART1->CMD = USART_CMD_RXEN | USART_CMD_CLEARTX | USART_CMD_CLEARRX | USART_CMD_TXTRIEN;
		USART1->TXDOUBLE = 0;
		while (!(USART1->STATUS & USART_STATUS_TXBL));
		USART1->TXDATA = 0;
		while(!(USART1->STATUS & USART_STATUS_RXDATAV));
		*rxvvB++ = *rxB++ = USART1->RXDATA;
		while(!(USART1->STATUS & USART_STATUS_RXDATAV));
		*rxvvB++ = *rxB++ = USART1->RXDATA;
		while(!(USART1->STATUS & USART_STATUS_RXDATAV));
		*rxvvB++ = *rxB++ = USART1->RXDATA;
		
		USART1->FRAME = USART_FRAME_DATABITS_TEN;
		USART1->TXDOUBLE = 0;
		while(!(USART1->STATUS & USART_STATUS_RXDATAV));
		*rxvvB++ = *rxB++ = rxv = USART1->RXDOUBLE;
		
		if (setDirctl)
			GPIO->P[1].DOUTSET = 1 << 8;					/* SWDIO is output */
		
		if ((llWireParity32(vv) ^ (rxv >> 8)) & 1)
			sta |= BUS_DATA_PAR_ERR;
	}
	else {
		if (setDirctl)
			GPIO->P[1].DOUTSET = 1 << 8;					/* SWDIO is output */

		// still need to read turn bit (do it by writing 4 zero bits - faster then our slow one bit read)
		USART1->FRAME = USART_FRAME_DATABITS_FOUR;
		USART1->CMD = USART_CMD_CLEARTX | USART_CMD_CLEARRX | USART_CMD_TXTRIDIS | USART_CMD_TXEN;
		USART1->TXDATA = 0;
	}
	
	ledSet(LED_ACT, false);
	
	return sta;
}

static uint8_t llWireBusWriteV1toV5(uint1_t ap, uint8_t a23, uint32_t val, bool setDirctl)
{
	uint32_t sta, tx1 = val, tx2, tx3;
	uint8_t transactParity = (ap ^ 0 ^ a23 ^ (a23 >> 1)) & 1;
	uint8_t valueParity = llWireParity32(val);
	
	ledSet(LED_ACT, true);
	
	if (setDirctl)
		GPIO->P[1].DOUTSET = 1 << 8;					/* SWDIO is output */

	USART1->FRAME = USART_FRAME_DATABITS_EIGHT;
	USART1->CMD = USART_CMD_CLEARTX | USART_CMD_CLEARRX | USART_CMD_TXTRIDIS | USART_CMD_TXEN;
	USART1->TXDATA = 0x01 /* start */ + (ap << 1) + (0 << 2) /* read */ + (a23 << 3) + (transactParity << 5) + (0 << 6) /* stop */ + (1 << 7) /* "park" == 1 */;

	asm volatile("":"+r"(val):"0"(val):"memory");	//makes sure this calculation is done now
	tx2 = val >> 11;
	tx3 = (val >> 22) + (((uint32_t)valueParity) << 10);
	asm volatile("":"+r"(tx2):"0"(tx2):"memory");	//makes sure this calculation is done now
	asm volatile("":"+r"(tx3):"0"(tx3):"memory");	//makes sure this calculation is done now
	
	while (!(USART1->STATUS & USART_STATUS_TXC));		/* wait for TX idle (means rx done too) */
	
	
	if (setDirctl)
		GPIO->P[1].DOUTCLR = 1 << 8;					/* SWDIO is input */

	USART1->FRAME = USART_FRAME_DATABITS_FIVE;
	USART1->CMD = USART_CMD_RXEN | USART_CMD_CLEARTX | USART_CMD_CLEARRX | USART_CMD_TXTRIEN;
	USART1->TXDATA = 0x00;
	while (!(USART1->STATUS & USART_STATUS_TXC));		/* wait for TX idle (means rx done too) */
	sta = ((USART1->RXDATA << 28)) >> 29;				//mask out status bit as fast as pobbile (gcc fails to generate this so we do it for it)
	
	if (setDirctl)
		GPIO->P[1].DOUTSET = 1 << 8;					/* SWDIO is output */

	if (sta == BUS_SWD_ACK || sta == BUS_SWD_EMPTY) {

		if (sta == BUS_SWD_EMPTY)
			tx1 = tx2 = tx3 = 0;

		USART1->CMD = USART_CMD_CLEARTX | USART_CMD_CLEARRX | USART_CMD_TXTRIDIS | USART_CMD_TXEN;
		USART1->FRAME = USART_FRAME_DATABITS_ELEVEN;
		USART1->TXDOUBLE = tx1;
		while (!(USART1->STATUS & USART_STATUS_TXBL));
		USART1->TXDOUBLE = tx2;
		while (!(USART1->STATUS & USART_STATUS_TXBL));
		USART1->TXDOUBLE = tx3;
		while (!(USART1->STATUS & USART_STATUS_TXC));
	}
	
	ledSet(LED_ACT, false);
	
	return sta;
}

static uint8_t llWireBusReadV1(uint1_t ap, uint8_t a23, unaligned_uint32_t *valP)
{
	return llWireBusReadV1toV5(ap, a23, valP, false);
}

static uint8_t llWireBusReadV5(uint1_t ap, uint8_t a23, unaligned_uint32_t *valP)
{
	return llWireBusReadV1toV5(ap, a23, valP, true);
}

static uint8_t llWireBusWriteV1(uint1_t ap, uint8_t a23, uint32_t val)
{
	return llWireBusWriteV1toV5(ap, a23, val, false);
}

static uint8_t llWireBusWriteV5(uint1_t ap, uint8_t a23, uint32_t val)
{
	return llWireBusWriteV1toV5(ap, a23, val, true);
}

//v0 is old and we do not care...
static inline __attribute__((always_inline)) uint32_t llWireCmdStatusTransactV0(uint1_t ap, uint1_t read, uint8_t a23, uint1_t transactParity)
{	/*returns status */
	uint8_t toTx = 0x01 /* start */ + (ap << 1) + (read << 2) + (a23 << 3) + (transactParity << 5) + (0 << 6) /* stop */ + (1 << 7) /* "park" == 1 */;

	llWireBusWriteBitsV0(toTx, 8);
	/* now come park and turn bits */
	/* then the 3 status bits we need */
	/* and, in case of write, another turn bit */
	/* slurp them all up in one read. separate data from fluff and return data. */
	return (llWireBusReadBitsV0(read ? 4 : 5) >> 1) & 7;
}

static uint8_t llWireBusReadV0(uint1_t ap, uint8_t a23, unaligned_uint32_t *valP)
{
	uint32_t sta;
	uint8_t transactParity = (ap ^ 1 ^ a23 ^ (a23 >> 1)) & 1;

	ledSet(LED_ACT, true);
	sta = llWireCmdStatusTransactV0(ap, 1, a23, transactParity);

	if (sta == BUS_SWD_ACK || sta == BUS_SWD_EMPTY) {

		uint32_t low16 = llWireBusReadBitsV0(16);
		uint32_t middle8 = llWireBusReadBitsV0(8);
		uint32_t high8 = llWireBusReadBitsV0(10);
		uint32_t rxedPar = high8 >> 8;
		uint32_t val = low16 + (middle8 << 16) + (high8 << 24);

		UNALIGNED(valP)	= val;

		/* compare parity */
		if ((rxedPar ^ llWireParity32(val)) & 1)
			sta |= BUS_DATA_PAR_ERR;
	}
	else	/* still need to read turn bit (do it by writing 4 zero bits - faster then our slow one bit read) */
		llWireBusWriteBitsV0(0, 4);

	ledSet(LED_ACT, false);

	return sta;
}

static uint8_t llWireBusWriteV0(uint1_t ap, uint8_t a23, uint32_t val)
{
	uint32_t sta, low16, mid8, high8;
	uint8_t transactParity = (ap ^ 0 ^ a23 ^ (a23 >> 1)) & 1;
	uint8_t valueParity = llWireParity32(val);

	low16 = (uint16_t)val;
	mid8 = (uint8_t)(val >> 16);
	high8 = (val >> 24) | ((uint32_t)valueParity << 8);	/* include parity */

	ledSet(LED_ACT, true);
	sta = llWireCmdStatusTransactV0(ap, 0, a23, transactParity);
	if (sta == BUS_SWD_ACK || sta == BUS_SWD_EMPTY) {

		if (sta == BUS_SWD_EMPTY)
			low16 = mid8 = high8 = 0;

		llWireBusWriteBitsV0(low16, 16);
		llWireBusWriteBitsV0(mid8, 8);
		llWireBusWriteBitsV0(high8, 10);
	}

	ledSet(LED_ACT, false);

	return sta;
}




void wireInit(void)
{
	uint32_t hwVer = BL->blGetHwVer();
	
	if (hwVer == HW_VER_EFM_V0) {
		//set pins as gpios in normal drive mode
		GPIO->P[1].MODEL = (GPIO->P[1].MODEL &~ _GPIO_P_MODEL_MODE7_MASK) | GPIO_P_MODEL_MODE7_PUSHPULL;
		GPIO->P[2].MODEL = (GPIO->P[2].MODEL &~ _GPIO_P_MODEL_MODE1_MASK) | GPIO_P_MODEL_MODE1_PUSHPULL;
		
		//data idle driven high, clock low
		GPIO->P[1].DOUTCLR = 1 << 7;
		GPIO->P[2].DOUTSET = 1 << 1;
		
		//set pointers
		mLlWireBusWriteBits = llWireBusWriteBitsV0;
		mLlWireBusRead = llWireBusReadV0;
		mLlWireBusWrite = llWireBusWriteV0;
		
		return;
	}
	
	//set pins as gpios in normal drive mode
	GPIO->P[1].MODEL = (GPIO->P[1].MODEL &~ _GPIO_P_MODEL_MODE7_MASK) | GPIO_P_MODEL_MODE7_PUSHPULL;
	GPIO->P[2].MODEL = (GPIO->P[2].MODEL &~ _GPIO_P_MODEL_MODE0_MASK) | GPIO_P_MODEL_MODE0_PUSHPULL;
	
	//data idle driven low, clock low
	GPIO->P[1].DOUTCLR = 1 << 7;
	GPIO->P[2].DOUTCLR = 1 << 0;

	//clock up usart1
	CMU->HFPERCLKEN0 |= CMU_HFPERCLKEN0_USART1;
	
	USART1->CTRL = USART_CTRL_AUTOTRI | USART_CTRL_CLKPHA_SAMPLETRAILING | USART_CTRL_CLKPOL_IDLEHIGH | USART_CTRL_OVS_X4 | USART_CTRL_LOOPBK | USART_CTRL_SYNC | USART_CTRL_TXBIL;
	USART1->CMD = USART_CMD_RXEN | USART_CMD_TXEN;
	USART1->CMD = USART_CMD_MASTEREN;
	USART1->CLKDIV = 0;	//fast please
	
	USART1->ROUTE = USART_ROUTE_LOCATION_LOC0 | USART_ROUTE_TXPEN | USART_ROUTE_CLKPEN;
	
	if (hwVer == HW_VER_EFM_V5) {
		//set pointers
		mLlWireBusWriteBits = llWireBusWriteBitsV5;
		mLlWireBusRead = llWireBusReadV5;
		mLlWireBusWrite = llWireBusWriteV5;
		
		//setup reset and direction control gpios (B13 = reset, b8 = dir)
		GPIO->P[1].MODEH = (GPIO->P[1].MODEH &~ (_GPIO_P_MODEH_MODE8_MASK | _GPIO_P_MODEH_MODE13_MASK)) | GPIO_P_MODEH_MODE8_PUSHPULL | GPIO_P_MODEH_MODE13_PUSHPULL;
		
		//set reset as high (not active) and direction control high (SDWIO is output)
		GPIO->P[1].DOUTSET = (1 << 13) | (1 << 8);
	}
	else {
		//set pointers
		mLlWireBusWriteBits = llWireBusWriteBitsV1;
		mLlWireBusRead = llWireBusReadV1;
		mLlWireBusWrite = llWireBusWriteV1;
	}
	
	#ifdef USE_TX_AS_RST
	
		//DANGER: this is our debugging SWDIO pin!!!
		
		GPIO->ROUTE = 0;
		GPIO->P[5].CTRL = GPIO_P_CTRL_DRIVEMODE_HIGH;
		GPIO->P[5].MODEL = (GPIO->P[5].MODEL &~ _GPIO_P_MODEL_MODE1_MASK) | GPIO_P_MODEL_MODE1_PUSHPULLDRIVE;
		
	#endif
	
	wireSetResetPinVal(1);	//reset is active low
}

uint32_t wireSetClockSpeed(uint32_t speed)
{
	if (speed > platGetSwdMaxClkSpeed())
		speed = platGetSwdMaxClkSpeed();
	
	speed *= 2;
	speed = (256ULL * platGetSwdMaxClkSpeed() * 2 + speed / 2) / speed - 256;
	if (speed > 0x00200000)
		speed = 0x001FFFF8;
	speed &= 0x001FFFF8;
	
	USART1->CLKDIV = speed;
	
	return (256ULL * platGetSwdMaxClkSpeed()) / (256 + speed);
}

void wireSetResetPinVal(bool high)
{
	#ifdef USE_TX_AS_RST
	
		if (high)
			GPIO->P[5].DOUTSET = 1 << 1;
		else
			GPIO->P[5].DOUTCLR = 1 << 1;
	#else
		if (BL->blGetHwVer() == HW_VER_EFM_V5) {
			if (high)
				GPIO->P[1].DOUTSET = 1 << 13;
			else
				GPIO->P[1].DOUTCLR = 1 << 13;
		}
	#endif
}

static void send64ones(void)
{
	uint32_t i;
	
	for(i = 0; i < 64; i += 8)
		llWireBusWriteBits(0xff, 8);
}


void wireSwdSendKey(void)
{
	uint16_t key = 0xE79E;
	
	send64ones();
	llWireBusWriteBits(key, 16);
	send64ones();
	llWireBusWriteBits(0x00, 8);
}

uint8_t llWireBusRead(uint1_t ap, uint8_t a23, unaligned_uint32_t *valP)
{
	return mLlWireBusRead(ap, a23, valP);
}

uint8_t llWireBusWrite(uint1_t ap, uint8_t a23, uint32_t val)
{
	return mLlWireBusWrite(ap, a23, val);
}
















