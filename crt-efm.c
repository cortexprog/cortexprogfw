#define WEAK __attribute__ ((weak))
#define ALIAS(f) __attribute__ ((weak, alias (#f)))



WEAK void IntDefaultHandler(void);
WEAK void NMI_Handler(void) ALIAS(IntDefaultHandler);
WEAK void HardFault_Handler(void) ALIAS(IntDefaultHandler);
WEAK void MemUsage_Handler(void) ALIAS(IntDefaultHandler);
WEAK void BusFault_Handler(void) ALIAS(IntDefaultHandler);
WEAK void UsageFault_Handler(void) ALIAS(IntDefaultHandler);
WEAK void SVC_Handler(void) ALIAS(IntDefaultHandler);
WEAK void PendSV_Handler(void) ALIAS(IntDefaultHandler);
WEAK void SysTick_Handler(void) ALIAS(IntDefaultHandler);


void DMA_IRQHandler(void) ALIAS(IntDefaultHandler);
void GPIO_EVEN_IRQHandler(void) ALIAS(IntDefaultHandler);
void TIMER0_IRQHandler(void) ALIAS(IntDefaultHandler);
void ACMP0_IRQHandler(void) ALIAS(IntDefaultHandler);
void ADC0_IRQHandler(void) ALIAS(IntDefaultHandler);
void I2C0_IRQHandler(void) ALIAS(IntDefaultHandler);
void GPIO_ODD_IRQHandler(void) ALIAS(IntDefaultHandler);
void TIMER1_IRQHandler(void) ALIAS(IntDefaultHandler);
void USART1_RX_IRQHandler(void) ALIAS(IntDefaultHandler);
void USART1_TX_IRQHandler(void) ALIAS(IntDefaultHandler);
void LEUART0_IRQHandler(void) ALIAS(IntDefaultHandler);
void PCNT0_IRQHandler(void) ALIAS(IntDefaultHandler);
void RTC_IRQHandler(void) ALIAS(IntDefaultHandler);
void CMU_IRQHandler(void) ALIAS(IntDefaultHandler);
void VCMP_IRQHandler(void) ALIAS(IntDefaultHandler);
void MSC_IRQHandler(void) ALIAS(IntDefaultHandler);
void AES_IRQHandler(void) ALIAS(IntDefaultHandler);
void USART0_RX_IRQHandler(void) ALIAS(IntDefaultHandler);
void USART0_TX_IRQHandler(void) ALIAS(IntDefaultHandler);
void USB_IRQHandler(void) ALIAS(IntDefaultHandler);
void TIMER2_IRQHandler(void) ALIAS(IntDefaultHandler);




//main must exist
extern int main(void);

//stack top (provided by linker)
extern void __stack_top();
extern void __data_data();
extern void __data_start();
extern void __data_end();
extern void __bss_start();
extern void __bss_end();



#define INFINITE_LOOP_LOW_POWER		while (1) {				\
							asm("wfi":::"memory");	\
						}




void __attribute__((noreturn)) IntDefaultHandler(void)
{
	INFINITE_LOOP_LOW_POWER
}


static void __attribute__((noreturn)) ResetISR(void)
{
	unsigned int *dst, *src, *end;
	//copy data
	dst = (unsigned int*)&__data_start;
	src = (unsigned int*)&__data_data;
	end = (unsigned int*)&__data_end;
	while(dst != end)
		*dst++ = *src++;

	//init bss
	dst = (unsigned int*)&__bss_start;
	end = (unsigned int*)&__bss_end;
	while(dst != end)
		*dst++ = 0;

//maybe this?

	main();

//if main returns => bad
	INFINITE_LOOP_LOW_POWER
}



//vector table

__attribute__ ((section(".vectors"))) void (*const __VECTORS[]) (void) =
{
	&__stack_top,		// The initial stack pointer
	ResetISR,		// The reset handler
	NMI_Handler,		// The NMI handler
	HardFault_Handler,	// The hard fault handler
	
	MemUsage_Handler,	// Reserved
	BusFault_Handler,	// Reserved
	UsageFault_Handler,	// Reserved
	0,			// Reserved
	0,			// Reserved
	0,			// Reserved
	0,			// Reserved
	
	SVC_Handler,		// SVCall handler
	0,			// Reserved
	0,			// Reserved
	PendSV_Handler,		// The PendSV handler
	SysTick_Handler,	// The SysTick handler


	// Chip Level - EFM32HG
	DMA_IRQHandler,
	GPIO_EVEN_IRQHandler,
	TIMER0_IRQHandler,
	ACMP0_IRQHandler,
	ADC0_IRQHandler,
	I2C0_IRQHandler,
	GPIO_ODD_IRQHandler,
	TIMER1_IRQHandler,
	USART1_RX_IRQHandler,
	USART1_TX_IRQHandler,
	LEUART0_IRQHandler,
	PCNT0_IRQHandler,
	RTC_IRQHandler,
	CMU_IRQHandler,
	VCMP_IRQHandler,
	MSC_IRQHandler,
	AES_IRQHandler,
	USART0_RX_IRQHandler,
	USART0_TX_IRQHandler,
	USB_IRQHandler,
	TIMER2_IRQHandler,
};


