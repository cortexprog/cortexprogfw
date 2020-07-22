#ifndef _CPU_H_
#define _CPU_H_


struct Cpu;

#include "types.h"
#include <stdio.h>
#define D_CPU_SUPPORTS_UNALIGNED_ACCESS_ //for windows
#undef UNALIGNED
#include "../cortex.h"
#include "scriptfiles.h"
#include "debugger.h"


#define CPU_NUM_REGS_PER_SET			NUM_REGS		//how many regs we get per cpuRegs*() call


//status codes
#define CPU_STAT_CODE_HALT_OR_STEP		CORTEX_W_HALT_OR_STEP
#define CPU_STAT_CODE_BKPT				CORTEX_W_BKPT
#define CPU_STAT_CODE_DWPT				CORTEX_W_DWPT
#define CPU_STAT_CODE_VCATCH			CORTEX_W_VCATCH
#define CPU_STAT_CODE_EXTERNAL			CORTEX_W_EXTERNAL
#define CPU_STAT_CODE_FAILED			CORTEX_W_FAIL

//watchpoint types
#define CPU_WPT_TYPE_OFF				CORTEX_WP_OFF
#define CPU_WPT_TYPE_PC					CORTEX_WP_PC
#define CPU_WPT_TYPE_READ				CORTEX_WP_READ
#define CPU_WPT_TYPE_WRITE				CORTEX_WP_WRITE
#define CPU_WPT_TYPE_RW					CORTEX_WP_RW
#define CPU_WPT_TYPE_MASK				CORTEX_WP_MASK

struct Cpu* cpuAttach(struct Debugger *dbg, bool verbose);							//returns non-NULL on success
const struct PotentialScriptfile *cpuGetScriptfileOptions(const struct Cpu* cpu);	//post attach returns the options
void cpuFree(struct Cpu* cpu);

uint32_t cpuGetOptimalReadNumWords(struct Cpu *cpu);
uint32_t cpuGetOptimalWriteNumWords(struct Cpu *cpu);

bool cpuMemReadEx(struct Cpu *cpu, uint32_t base, uint32_t numWords, uint32_t *dst, bool silent/* do not write any errors to stdout*/);
bool cpuMemRead(struct Cpu *cpu, uint32_t base, uint32_t numWords, uint32_t *dst);
bool cpuMemWrite(struct Cpu *cpu, uint32_t base, uint32_t numWords, const uint32_t *src, bool withAck);

bool cpuRegsGet(struct Cpu *cpu, uint8_t regSet, uint32_t *dst);
bool cpuRegsSet(struct Cpu *cpu, uint8_t regSet, const uint32_t *src);

uint8_t cpuStop(struct Cpu *cpu);				// ->CPU_STAT_*
bool cpuReset(struct Cpu *cpu);
bool cpuGo(struct Cpu *cpu);
uint8_t cpuStep(struct Cpu *cpu);				// ->CPU_STAT_*
uint8_t cpuIsStoppedAndWhy(struct Cpu *cpu);	// ->CPU_STAT_*

uint8_t cpuResetStop(struct Cpu *cpu);				// ->CPU_STAT_*

//pass-though getter for "is slow debugger?"
bool cpuIsDebuggerHwSlow(const struct Cpu *cpu);

//special-purpose convenience funcs
bool cpuSetWatchpoint(struct Cpu* cpu, uint32_t idx, uint32_t addr, uint32_t size, uint32_t type);	//type is CPU_WPT_TYPE_*
bool cpuSetCpsrTbitAndDisableInterrupts(struct Cpu *cpu);											//optionally return control regs we read (can pass NULL if uninterested)

//higher level
int32_t cpuTraceLogRead(struct Cpu* cpu, uint32_t addr, void **bufP);	//caller must free buf that this func allocates and returns in *bufP. length of buf is returned

//info
bool cpuHasFpu(const struct Cpu* cpu);
bool cpuIsV7(const struct Cpu* cpu);

//core switching (only ID 0 is supported in case of no debugger multicore support
bool cpuSelectCore(struct Cpu* cpu, uint16_t coreId);


#endif
