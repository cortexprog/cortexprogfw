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

	local family = cpuWordRead(PSOC_CPUID_REG_ADDR)

	if not v then
		error(" Failed to read PSoC romtab CPUID")
	end
	
	if (family >> 8) ~= 0 then
		error(string.format(" PSoC romtab CPUID invalid 0x%08X", ret))
	end
	
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
	
	return cpuss
	
end


function main(step, haveDbg, haveCpu, haveScpt)

	if step == TOOL_OP_STEP_PRE_SCRIPT then
	
		local val
		
		print("resetting")
		
		cpuWordWrite(0x40030014, 0x80000000)
		cpuReset()
		
		repeat
		
		until cpuWordWrite(0x40030014, 0x80000000)
		
		print("written")
		
		repeat
			val = cpuWordRead(0x40100004)
		until (val & 0x10000000) == 0
		
		print("stopped out of rom")
		
		print(string.format("SYSREQ=0x%08X Pc=0x%08X", val, cpuRegGet(15)))
		return true
		
		
	end
	
	return true
end

function init(params)

	if params == "open" then
		mDesiredMode = 0x01
	elseif params == "protected" then
		mDesiredMode = 0x02
	elseif params == "kill" then
		mDesiredMode = 0x04
	elseif params == "read" then
		mDesiredMode = 0
	else
		print("This script needs a paramater. Options are:\n\t\"read\" - read current mode (if possible)\n\t\"open\" - set open mode\n\t\"protected\" - set protected mode\n\t\"kill\" - set kill mode")
		return nil
	end

	return TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU
end