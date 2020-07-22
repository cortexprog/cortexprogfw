#include "swdCommsPacket.h"
#include "efm32hg308f64.h"
#include "led-efm.h"
#include "blupdate-efm.h"
#include "wire.h"
#include "plat.h"





#define HW3_REG_I2C_ADDR			0x5B
#define HW4to5_POT_I2C_ADDR			0x2F
#define HW3to5_I2C_SDA_PORT			0
#define HW3to5_I2C_SDA_PIN			0
#define HW3to5_I2C_SCL_PORT			2
#define HW3to5_I2C_SCL_PIN			1
#define HW4to5_PWR_EN_PORT			1
#define HW4to5_PWR_EN_PIN			14
#define HW4to5_PWR_EN_ON			0	//active low signal




struct UsbStringDescr {
	uint8_t len;
	uint8_t type;
	uint16_t chars[];
} __attribute__((aligned(4)));



static const struct UsbStringDescr mDevStr = {24, 3, {'C', 'o', 'r', 't', 'e', 'x', 'P', 'r', 'o', 'g', '2'}};
static struct UsbStringDescr mSnumStr = {sizeof(struct UsbStringDescr) + sizeof(uint16_t) * 16, 3, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
static bool mCdcIsUp = false;
static volatile uint32_t mTicksHi = 0;
static uint32_t mTicksPerSec;
static int16_t mCurVoltage = -1;

/* circular buffer */
struct CircularBuffer {	//zero-inited means mepty :)
	uint8_t buf[256];
	volatile uint8_t rP, wP;
};


static struct CircularBuffer uartDataFromPc, uartDataFromDut;

static bool platI2cTransact(uint8_t addr7b, const void *dataTx, uint32_t txLen, bool allowNakOnLastWrite, void *dataRx, uint32_t rxLen, bool nakLastRead);



static inline bool cbIsEmpty(const struct CircularBuffer *cb)
{
	return cb->rP == cb->wP;
}

static inline bool cbIsFull(const struct CircularBuffer *cb)
{
	return cb->rP == (uint8_t)(cb->wP + 1);
}

static inline int16_t cbRead(struct CircularBuffer *cb)	//returns negative on fail
{
	if (cbIsEmpty(cb))
		return -1;
	
	return cb->buf[cb->rP++];
}

static inline int16_t cbPeek(struct CircularBuffer *cb)	//returns negative on fail, does not dequeue
{
	if (cbIsEmpty(cb))
		return -1;
	
	return cb->buf[cb->rP];
}

static inline bool cbWrite(struct CircularBuffer *cb, uint8_t val)
{
	if (cbIsFull(cb))
		return false;
	
	cb->buf[cb->wP++] = val;
	return true;
}

static void snumTostr(uint16_t *dst, uint32_t val)
{
	static const char hexchr[] = "0123456789abcdef";
	uint32_t i;
	
	for (i = 0; i < 8; i++, val >>= 4)
		dst[7 - i] = hexchr[val & 0x0f];
}

static void platUartIrqSetup(IRQn_Type irqn)	//just for clenaliness
{
	NVIC_SetPriority(irqn, (1UL << __NVIC_PRIO_BITS) - 1UL);	//minimum prio for uart
	NVIC_ClearPendingIRQ(irqn);
	NVIC_EnableIRQ(irqn);
}

void USART0_RX_IRQHandler(void)
{
	uint32_t val = USART0->RXDATAX;
	
	//discard data with errors
	if (val & (USART_RXDATAX_PERR | USART_RXDATAX_FERR))
		return;
	
	//queue it if we can (do nothing if we cnanot)
	(void)cbWrite(&uartDataFromDut, val);
}

void USART0_TX_IRQHandler(void)
{
	int16_t val = cbRead(&uartDataFromPc);
	
	if (val < 0)				//empty -> ints off
		USART0->IEN &=~ USART_IEN_TXBL;
	else
		USART0->TXDATA = val;
}

static uint32_t usartClockDivVal(uint32_t baud)
{
	uint32_t ret, validDivBitmask = _USART_CLKDIV_DIVEXT_MASK | _USART_CLKDIV_DIV_MASK;
	
	baud *= 4;	//OVS
	
	if (!baud)	//avoid div by 0
		baud = 1;
	
	ret = (256ULL * CLOCKFREQ + baud / 2) / baud;
	if (ret < 256)	//avboid overflow
		ret = 0;
	else
		ret -= 256;
	
	if (ret > validDivBitmask)
		ret = validDivBitmask;
	ret &= validDivBitmask;
	
	return ret;
}

static void platUsbToSerialDataRxCbk(void *data, uint32_t len)
{
	const uint8_t *d = (const uint8_t*)data;
	
	while(len-- && cbWrite(&uartDataFromPc, *d++));
	
	//if buffer is now nonempty, enable interrupt on free bffer space (starts TX)
	if (!cbIsEmpty(&uartDataFromPc))
		USART0->IEN |= USART_IEN_TXBL;
}

static bool platUsbToSerialConfigMisc(uint8_t stopBitCfg, uint8_t parityCfg, uint8_t dataBits)
{
	//XXX: we could support 9-bit data, but it is a pain and requires larger buffers, so we do not!
	//XXX: we coudl emulate mark/space parity using extra TX bits, but we do not!

	uint32_t cfg;

	if (dataBits > 8 || dataBits < 4)
		return false;
	
	cfg = (USART_FRAME_DATABITS_FOUR + dataBits - 4);
	
	switch (parityCfg) {
		case CDC_PARITY_NONE:
			cfg |= USART_FRAME_PARITY_NONE;
			break;
		case CDC_PARITY_ODD:
			cfg |= USART_FRAME_PARITY_ODD;
			break;
		case CDC_PARITY_EVEN:
			cfg |= USART_FRAME_PARITY_EVEN;
			break;
		default:
			return false;
	}
	
	switch (stopBitCfg) {
		case CDC_STOP_BITS_1:
			cfg |= USART_FRAME_STOPBITS_ONE;
			break;
		case CDC_STOP_BITS_1_5:
			cfg |= USART_FRAME_STOPBITS_ONEANDAHALF;
			break;
		case CDC_STOP_BITS_2:
			cfg |= USART_FRAME_STOPBITS_TWO;
			break;
		default:
			return false;
	}
	
	USART0->FRAME = cfg;
	return true;
}

static void platUsbToSerialConfigCbk(struct CdcAcmLineCoding *cfg)
{
	USART0->CLKDIV = usartClockDivVal(cfg->baudrate);
	platUsbToSerialConfigMisc(cfg->nStopBits, cfg->party, cfg->dataBits);
}

void platWork(void)
{
	int16_t val;
	
	//handle TX work for UART
	if (mCdcIsUp) {
		while((val = cbPeek(&uartDataFromDut)) >= 0) {
			
			uint8_t byte = val;
			
			if (sizeof(byte) != BL->blCdcBulkTx(&byte, sizeof(byte)))
				break;
			
			//dequeue char if it was sent properly
			cbRead(&uartDataFromDut);
		}
	}
}

void SysTick_Handler(void)
{
	mTicksHi++;
}

static uint64_t getTicks(void)
{
	uint32_t hi, lo;
	uint64_t ret;
	
	do {
		hi = mTicksHi;
		lo = SysTick->VAL;
	} while (mTicksHi != hi);
	
	
	ret = hi;
	ret <<= 24;
	ret += (1 << 24) - lo;
	
	return ret;
}

uint64_t platGetTicks(void)
{
	return getTicks();
}

uint32_t platGetTicksPerSecond(void)
{
	return mTicksPerSec;
}

void platInit(bool enableCdcAcmUart, uint8_t blVer)
{
	extern uint8_t __vectors_start[];
	uint32_t ver = BL->blGetHwVer();
	
	//unlock cmu for everyone
	CMU->LOCK = _CMU_LOCK_LOCKKEY_UNLOCK;
	SCB->VTOR = (uint32_t)&__vectors_start;
	
	//update bootloader if needed	(will not return if update is done)
	blUpdateIfNeeded(blVer);
	
	//clock up to 25MHz if possible. for our chip each step in TUNING is 52.8KHz
	//currently we're tuned to 21MHz, so we need to add 76 steps or so
	//we do need to make sure we have the head room for this so we'll carefulyl look into that
	uint8_t initialTuning = (DEVINFO->HFRCOCAL1 & _DEVINFO_HFRCOCAL1_BAND21_MASK) >> _DEVINFO_HFRCOCAL1_BAND21_SHIFT;
	if (initialTuning > (255 - 76)) {
		//do the best we cna
		mTicksPerSec = 21000000 + 52800 * (255 - initialTuning);
		CMU->HFRCOCTRL |= 0xff;
	}
	else {		//we can do it
		CMU->HFRCOCTRL = (CMU->HFRCOCTRL & 0xffffff00) + (initialTuning + 76);
		mTicksPerSec = 21000000 + 52800 * 76;
	}
	
	//guess we're proceeding - get on with it
	if (enableCdcAcmUart) {
		
		#ifndef USE_TX_AS_RST

			if (ver == HW_VER_EFM_V2 || ver == HW_VER_EFM_V3 || ver == HW_VER_EFM_V4 || ver == HW_VER_EFM_V5) {
				//clock it up
				CMU->HFPERCLKEN0 |= CMU_HFPERCLKEN0_USART0;
				
				//enable proper pin config for our pins (E12,E13)
				GPIO->P[4].CTRL = GPIO_P_CTRL_DRIVEMODE_HIGH;
				GPIO->P[4].MODEH = (GPIO->P[4].MODEH &~ (_GPIO_P_MODEH_MODE12_MASK | _GPIO_P_MODEH_MODE13_MASK)) | GPIO_P_MODEH_MODE13_PUSHPULLDRIVE | GPIO_P_MODEH_MODE12_INPUTPULLFILTER;
		
				GPIO->P[4].DOUTCLR = 1 << 12;	//pulldown on P12
		
				USART0->CMD = USART_CMD_RXEN | USART_CMD_TXEN | USART_CMD_TXTRIDIS | USART_CMD_RXBLOCKDIS | USART_CMD_CLEARTX | USART_CMD_CLEARRX;		//get into a good state
				USART0->ROUTE = USART_ROUTE_LOCATION_LOC3 | USART_ROUTE_TXPEN | USART_ROUTE_RXPEN;			//route location 3, TX & RX pins on
				USART0->CTRL = USART_CTRL_OVS_X4;
				USART0->IEN = USART_IEN_RXDATAV;
				
				//defautl config is 115200 8n1
				USART0->CLKDIV = usartClockDivVal(115200);
				platUsbToSerialConfigMisc(CDC_STOP_BITS_1, CDC_PARITY_NONE, 8);
				
				platUartIrqSetup(USART0_RX_IRQn);
				platUartIrqSetup(USART0_TX_IRQn);
				
				BL->blCdcBulkCfg(platUsbToSerialDataRxCbk, platUsbToSerialConfigCbk);
			}
		#endif
	}
	
	//reenumerate
	snumTostr(mSnumStr.chars + 0, DEVINFO->UNIQUEH);
	snumTostr(mSnumStr.chars + 8, DEVINFO->UNIQUEL);
	usbReenumerate(&mDevStr, &mSnumStr);
	
	mCdcIsUp = true;	//safe even if cdc is not even enabled
	
	//leds
	ledInit();
	ledSet(LED_PWR, true);
	
	//SysTick
	SysTick_Config(1 << 24);
	
	//boolean power control if available
	if (ver == HW_VER_EFM_V2) {
		//power control is B14, and off
		GPIO->P[1].MODEH = (GPIO->P[1].MODEH &~ _GPIO_P_MODEH_MODE14_MASK) | GPIO_P_MODEH_MODE14_PUSHPULL;
		GPIO->P[1].DOUTSET = 1 << 14;
		
		mCurVoltage = 0;
	}
	
	//variable power i2c control if available
	if (ver == HW_VER_EFM_V3 || ver == HW_VER_EFM_V4 || ver == HW_VER_EFM_V5) {
		//SDA = PA0, SCL = C1		sadly this does not map to a valid mapping of I2C0 pins so we bitbang it
		
		GPIO->P[HW3to5_I2C_SDA_PORT].MODEL = (GPIO->P[HW3to5_I2C_SDA_PORT].MODEL &~ (_GPIO_P_MODEL_MODE0_MASK << (_GPIO_P_MODEL_MODE1_SHIFT * HW3to5_I2C_SDA_PIN))) | (_GPIO_P_MODEL_MODE0_WIREDANDPULLUP << (_GPIO_P_MODEL_MODE1_SHIFT * HW3to5_I2C_SDA_PIN));
		GPIO->P[HW3to5_I2C_SCL_PORT].MODEL = (GPIO->P[HW3to5_I2C_SCL_PORT].MODEL &~ (_GPIO_P_MODEL_MODE0_MASK << (_GPIO_P_MODEL_MODE1_SHIFT * HW3to5_I2C_SCL_PIN))) | (_GPIO_P_MODEL_MODE0_WIREDANDPULLUP << (_GPIO_P_MODEL_MODE1_SHIFT * HW3to5_I2C_SCL_PIN));
		
		GPIO->P[HW3to5_I2C_SDA_PORT].DOUTSET = 1 << HW3to5_I2C_SDA_PIN;
		GPIO->P[HW3to5_I2C_SCL_PORT].DOUTSET = 1 << HW3to5_I2C_SCL_PIN;
		
		mCurVoltage = 0;
	}
	
	//power en pin set (and disable output)
	if (ver == HW_VER_EFM_V4 || ver == HW_VER_EFM_V5) {
		GPIO->P[HW4to5_PWR_EN_PORT].MODEH = (GPIO->P[HW4to5_PWR_EN_PORT].MODEH &~ (_GPIO_P_MODEH_MODE8_MASK << (_GPIO_P_MODEH_MODE9_SHIFT * (HW4to5_PWR_EN_PIN - 8)))) | (_GPIO_P_MODEH_MODE8_PUSHPULL << (_GPIO_P_MODEH_MODE9_SHIFT * (HW4to5_PWR_EN_PIN - 8)));
		if (HW4to5_PWR_EN_ON)
			GPIO->P[HW4to5_PWR_EN_PORT].DOUTSET = 1 << HW4to5_PWR_EN_PIN;
		else
			GPIO->P[HW4to5_PWR_EN_PORT].DOUTCLR = 1 << HW4to5_PWR_EN_PIN;
		
		mCurVoltage = 0;
	}
}

uint8_t platGetHwVerForComms()
{
	return BL->blGetHwVer() + 1;	//this works for now...
}

uint32_t platGetFlags(void)
{
	uint32_t flags = 0, ver = BL->blGetHwVer();
	
	if (ver == HW_VER_EFM_V2)
		flags |= PWR_FLAG_PWR_CTRL_ON_OFF;
	
	if (ver == HW_VER_EFM_V3 || ver == HW_VER_EFM_V4 || ver == HW_VER_EFM_V5)
		flags |= PWR_FLAG_PWR_CTRL_SETTABLE;
	
	if (mCdcIsUp && (ver == HW_VER_EFM_V2 || ver == HW_VER_EFM_V3 || ver == HW_VER_EFM_V4 || ver == HW_VER_EFM_V5))
		flags |= UART_FLAG_UART_EXISTS;
	
	if (ver == HW_VER_EFM_V1 || ver == HW_VER_EFM_V2 || ver == HW_VER_EFM_V3 || ver == HW_VER_EFM_V4 || ver == HW_VER_EFM_V5)
		flags |= SWD_FLAG_CLOCK_SPEED_SETTABLE;
	
	if (ver == HW_VER_EFM_V5)		//v5 has a reset pin
		flags |= SWD_FLAG_RESET_PIN;
	
	flags |= SWD_FLAG_UPLOADABLE_CODE;		//always
	flags |= SWD_FLAG_MULTICORE_SUPPORT;	//always
	
	#ifdef USE_TX_AS_RST
		flags |= SWD_FLAG_RESET_PIN;
	#endif	
	
	return flags;
}

uint32_t platSetSwdClockSpeed(uint32_t speed)
{
	return wireSetClockSpeed(speed);
}

uint32_t platGetSwdMaxClkSpeed(void)
{
	return mTicksPerSec / 2;
}

bool platPowerOnOffSet(bool on)
{
	if (BL->blGetHwVer() != HW_VER_EFM_V2)
		return false;
	
	mCurVoltage = on ? 3300 : 0;
	
	if (on)
		GPIO->P[1].DOUTCLR = 1 << 14;
	else
		GPIO->P[1].DOUTSET = 1 << 14;
	
	return true;
}

static bool platPowerVariableSetV3pwr(bool on)
{
	uint8_t data[] = {1, on ? 3 : 0};
	
	return platI2cTransact(HW3_REG_I2C_ADDR, data, sizeof(data), false, NULL, 0, false);
}

static bool platPowerVariableSetV4to5pwr(bool on)
{
	bool state = on ? HW4to5_PWR_EN_ON : !HW4to5_PWR_EN_ON;
	
	if (state)
		GPIO->P[HW4to5_PWR_EN_PORT].DOUTSET = 1 << HW4to5_PWR_EN_PIN;
	else
		GPIO->P[HW4to5_PWR_EN_PORT].DOUTCLR = 1 << HW4to5_PWR_EN_PIN;
	
	return true;
}

bool platPowerVaribleSet(uint32_t mV)
{
	uint32_t ver = BL->blGetHwVer();
	
	if (ver == HW_VER_EFM_V3) {
	
		if (!mV) {	//off
			
			mCurVoltage = 0;
			return platPowerVariableSetV3pwr(false);
		}
		else {		//on
			//Vout = 0.7  + 0.01 * reg			[0.7V .. 2.4V]		we might want to try more?
			
			uint8_t data[] = {2, ((mV - 700) + 5) / 10};
			
			if (mV < 700 || mV > 2400) {
				platPowerVariableSetV3pwr(false);
				return false;
			}
		
			mCurVoltage = mV;
			return platI2cTransact(HW3_REG_I2C_ADDR, data, sizeof(data), false, NULL, 0, false) && platPowerVariableSetV3pwr(true);
		}
	}
	
	if (ver == HW_VER_EFM_V4 || ver == HW_VER_EFM_V5) {
	
		if (!mV) {	//off
			
			mCurVoltage = 0;
			return platPowerVariableSetV4to5pwr(false);
		}
		else {		//on
			//Vout = 0.6 * 127 / val
			//val = 76200 / mVout
			
			uint8_t data = (76200UL + mV / 2) / mV;
			
			if (mV < 600 || mV > 3300) {
				platPowerVariableSetV4to5pwr(false);
				return false;
			}
		
			mCurVoltage = mV;
			return platI2cTransact(HW4to5_POT_I2C_ADDR, &data, sizeof(data), false, NULL, 0, false) && platPowerVariableSetV4to5pwr(true);
		}
	}
	
	return false;
}

int32_t platGetCurSupplyVoltage(void)
{
	return mCurVoltage;
}

void platGetSupplyAbilities(uint16_t *millivoltsMinP, uint16_t *millivoltsMaxP, uint16_t *milliampsMaxP)
{
	uint32_t ver = BL->blGetHwVer();
	
	switch(ver) {
		case HW_VER_EFM_V2:
			*millivoltsMinP = 3300;
			*millivoltsMaxP = 3300;
			*milliampsMaxP = 50;
			break;
		
		case HW_VER_EFM_V3:
			*millivoltsMinP = 700;
			*millivoltsMaxP = 2400;
			*milliampsMaxP = 300;
			break;
		
		case HW_VER_EFM_V4:
		case HW_VER_EFM_V5:
			*millivoltsMinP = 600;
			*millivoltsMaxP = 3300;
			*milliampsMaxP = 300;
			break;
		
		default:
			*millivoltsMinP = 0;
			*millivoltsMaxP = 0;
			*milliampsMaxP = 0;
			break;
	}
}

void platDeinit(void)
{
	ledSet(LED_PWR, false);
}

static void platI2cTransactDelay(void)
{
	uint64_t time = getTicks();
	
	while(getTicks() - time < mTicksPerSec / 524288);	//at most 262KHz (really less). number picked for being a power of 2
}

static bool platI2cTransactLineUp(uint32_t port, uint32_t pin, uint32_t nTries)
{
	uint32_t bit = 1 << pin;
	
	while(nTries--) {
		
		GPIO->P[port].DOUTSET = bit;
		platI2cTransactDelay();
		if (GPIO->P[port].DIN & bit)
			return true;
	}
	
	//tumeout or arb lost
	return false;
}

static bool platI2cTransactDataLineUp(void)		//sda up in cases where arbitration might happen
{
	return platI2cTransactLineUp(HW3to5_I2C_SDA_PORT, HW3to5_I2C_SDA_PIN, 1);
}

static bool platI2cTransactClockLineUp(void)
{
	return platI2cTransactLineUp(HW3to5_I2C_SCL_PORT, HW3to5_I2C_SCL_PIN, 64);
}

static bool platI2cTransactStart(void)		//assumes proper bus state (this will not do a restart)
{
	if (!platI2cTransactDataLineUp())		//sda must be high or we lost arbitration
		return false;
	
	platI2cTransactDelay();
	GPIO->P[HW3to5_I2C_SDA_PORT].DOUTCLR = 1 << HW3to5_I2C_SDA_PIN;
	platI2cTransactDelay();
	GPIO->P[HW3to5_I2C_SCL_PORT].DOUTCLR = 1 << HW3to5_I2C_SCL_PIN;
	platI2cTransactDelay();
	
	return true;
}

static int32_t platI2cTransactBitRx(void)
{
	int32_t ret;
	
	GPIO->P[HW3to5_I2C_SDA_PORT].DOUTSET = 1 << HW3to5_I2C_SDA_PIN;
	platI2cTransactDelay();
	if (!platI2cTransactClockLineUp())
		return -1;
	platI2cTransactDelay();
	
	ret = (GPIO->P[HW3to5_I2C_SDA_PORT].DIN & (1 << HW3to5_I2C_SDA_PIN)) ? 1 : 0;
	GPIO->P[HW3to5_I2C_SCL_PORT].DOUTCLR = 1 << HW3to5_I2C_SCL_PIN;
	
	return ret;
}

static bool platI2cTransactBitTx(bool val)
{
	if (val)
		GPIO->P[HW3to5_I2C_SDA_PORT].DOUTSET = 1 << HW3to5_I2C_SDA_PIN;
	else
		GPIO->P[HW3to5_I2C_SDA_PORT].DOUTCLR = 1 << HW3to5_I2C_SDA_PIN;
	platI2cTransactDelay();
	if (!platI2cTransactClockLineUp())
		return false;
	platI2cTransactDelay();
	if (val && !(GPIO->P[HW3to5_I2C_SDA_PORT].DIN & (1 << HW3to5_I2C_SDA_PIN)))	//arb lost
		return false;
	GPIO->P[HW3to5_I2C_SCL_PORT].DOUTCLR = 1 << HW3to5_I2C_SCL_PIN;
	platI2cTransactDelay();
	
	return true;
}

static int32_t platI2cTransactByteRx(bool ack)
{
	uint32_t i, v = 0;
	int32_t ret;
	
	for (i = 0; i < 8; i++) {
	
		ret = platI2cTransactBitRx();
		if (ret < 0)
			return ret;
		
		v = (v << 1) + ret;
	}
	
	if (!platI2cTransactBitTx(!ack))
		return -1;
	
	return v;
}

static bool platI2cTransactByteTx(uint32_t val)
{
	uint32_t i;
	int32_t ret;
	
	for (i = 0; i < 8; i++, val <<= 1) {
		
		if(!platI2cTransactBitTx(!!(val & 0x80)))
			return false;
	}
	
	ret = platI2cTransactBitRx();
	return ret == 0;	//0 is ACK, 1 is NAK, negative is error
}

static bool platI2cTransact(uint8_t addr7b, const void *dataTx, uint32_t txLen, bool allowNakOnLastWrite, void *dataRx, uint32_t rxLen, bool nakLastRead)
{
	const uint8_t *tx = (const uint8_t*)dataTx;
	bool ret = false, needRestart = false;
	uint8_t *rx = (uint8_t*)dataRx;
	int32_t rxVal;

	
	//start (always)
	if (!platI2cTransactStart())
		return false;
	
	//if we have data to TX, do so (no tx and no rx data means we issue an empty tx transaction)
	if (txLen || !rxLen) {

		//send address
		if (!platI2cTransactByteTx(addr7b << 1))
			goto out_stop;					//addr got NAKed
		
		while(txLen) {
			
			txLen--;
			ret = platI2cTransactByteTx(*tx++);
			
			if (!ret) {				//NAK has many meanings - investigate
				if (txLen)			//in the middle of tx it is always bad
					goto out_stop;
				if (!allowNakOnLastWrite)	//if naks arent allowe don last byte, ti is an error to get one
					goto out_stop;
			}
		}
		needRestart = true;
	}
	
	//if we need to RX, do so (incl restart if needed)
	if (rxLen) {
		if (needRestart) {
			if (!platI2cTransactDataLineUp())	//arb loss?
				goto out_stop;
			platI2cTransactDelay();
			if (!platI2cTransactClockLineUp())	//clock stretching cna time out
				goto out_stop;
			platI2cTransactDelay();
			if (!platI2cTransactStart())
				goto out_stop;
		}
		
		//rx data
		while (rxLen) {
			
			rxLen--;
			rxVal = platI2cTransactByteRx(rxLen || !nakLastRead);
			if (rxVal < 0)				//nak or timeout
				goto out_stop;
			
			*rx++ = rxVal;
		}
	}
	//unless an error occurs fm now on, we are a success
	ret = true;

out_stop:	//no matter what, send a stop
	//stop
	{
		GPIO->P[HW3to5_I2C_SDA_PORT].DOUTCLR = 1 << HW3to5_I2C_SDA_PIN;
		platI2cTransactDelay();
		if (!platI2cTransactClockLineUp())
			return false;
		platI2cTransactDelay();
		if (!platI2cTransactDataLineUp())	//arb loss?
			return false;
		platI2cTransactDelay();
	}
	
	return ret;
}

void __attribute((used)) report_hard_fault(uint32_t* regs, uint32_t ret_lr, uint32_t *user_sr)
{
	uint32_t *push = (ret_lr == 0xFFFFFFFD) ? user_sr : (regs + 8);
	
	BL->dbgBreakLocks();
	
	BL->dbgPrintf("============ HARD FAULT ============\n");
	BL->dbgPrintf("R0	= 0x%08X		R8	= 0x%08X\n", push[0], regs[0]);
	BL->dbgPrintf("R1	= 0x%08X		R9	= 0x%08X\n", push[1], regs[1]);
	BL->dbgPrintf("R2	= 0x%08X		R10	= 0x%08X\n", push[2], regs[2]);
	BL->dbgPrintf("R3	= 0x%08X		R11	= 0x%08X\n", push[3], regs[3]);
	BL->dbgPrintf("R4	= 0x%08X		R12	= 0x%08X\n", regs[4], push[4]);
	BL->dbgPrintf("R5	= 0x%08X		SP	= 0x%08X\n", regs[5], push + 8);
	BL->dbgPrintf("R6	= 0x%08X		LR	= 0x%08X\n", regs[6], push[5]);
	BL->dbgPrintf("R7	= 0x%08X		PC	= 0x%08X\n", regs[7], push[6]);
	BL->dbgPrintf("RET = 0x%08X		SR	= 0x%08X\n", ret_lr,	push[7]);
	BL->dbgPrintf("CFSR= 0x%08X		HFSR= 0x%08X\n", *(uint32_t*)0xe000ed28, *(uint32_t*)0xe000ed2c);
}

void __attribute__((noreturn, naked, noinline)) HardFault_Handler(void)
{
	asm volatile(
			"push {r4-r7}			\n\t"
			"mov	r0, r8			\n\t"
			"mov	r1, r9			\n\t"
			"mov	r2, r10			\n\t"
			"mov	r3, r11			\n\t"
			"push {r0-r3}			\n\t"
			"mov	r0, sp			\n\t"
			"mov	r1, lr			\n\t"
			"mrs	r2, PSP			\n\t"
			"bl report_hard_fault		\n\t"
			"1:				\n\t"
			"wfi				\n\t"
			"b 1b				\n\t"	//loop forever
			:::"memory");
	while(1);
}

void USB_IRQHandler(void)
{
	(*((void (**)(void))((USB_IRQn + 16) * 4)))();	//call through to BL's usb code
}

void TIMER0_IRQHandler(void)	//not actually needed, but makes sr enobody else tries to use timer 0 as usb stack uses it already
{
	(*((void (**)(void))((TIMER0_IRQn + 16) * 4)))();	//call through to BL's usb code
}

void LEUART0_IRQHandler(void)
{
	(*((void (**)(void))((LEUART0_IRQn + 16) * 4)))();	//call through to BL's usb code
}