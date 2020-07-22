FLASH_BASE_ADDR			= 0x40022000
FLASH_ACR_ADDR			= FLASH_BASE_ADDR + 0x00
FLASH_KEYR_ADDR			= FLASH_BASE_ADDR + 0x04
 FLASH_KEYR_VAL_1		= 0x45670123
 FLASH_KEYR_VAL_2		= 0xCDEF89AB
FLASH_OPTKEYR_ADDR		= FLASH_BASE_ADDR + 0x08
 FLASH_OPTKEYR_VAL_1	= 0x08192A3B
 FLASH_OPTKEYR_VAL_2	= 0x4C5D6E7F
FLASH_SR_ADDR			= FLASH_BASE_ADDR + 0x0C
 FLASH_SR_VAL_BSY		= 0x00000001
FLASH_CR_ADDR			= FLASH_BASE_ADDR + 0x10
 FLASH_CR_VAL_STRT		= 0x00000040
 FLASH_CR_VAL_OPTER		= 0x00000020
 FLASH_CR_VAL_OPTPG		= 0x00000010
FLASH_OPTCR_ADDR		= FLASH_BASE_ADDR + 0x14
 FLASH_OPTCR_OPTLOCK	= 0x00000001
 FLASH_OPTCR_OPTSTRT	= 0x00000002
FLASH_OPTCR1_ADDR		= FLASH_BASE_ADDR + 0x18

RAM_LOC					= 0x20000000
INSTR_STRH_R1_to_R0		= 0x8001

OPTION_BYTE_NUM			= 12
OPTION_BYTES_ADDR		= 0x1FFFC000	--the only bank or second bank if two
OPTION_BYTES_ADDR_B1	= 0x1FFEC000	--first bank if two


function flashWait()
	local val
	repeat
		val = cpuWordRead(FLASH_SR_ADDR)
		if not val then
			return nil
		end
		print(string.format(" - FLASH_SR=0x%08X", val))
	until (val & FLASH_SR_VAL_BSY) == 0
	
	print(string.format(" - FLASH_SR=0x%08X", cpuWordRead(FLASH_SR_ADDR)))
	print(string.format(" - FLASH_SR=0x%08X", cpuWordRead(FLASH_SR_ADDR)))
	print(string.format(" - FLASH_SR=0x%08X", cpuWordRead(FLASH_SR_ADDR)))
		
	return true
end

function readOneOptionWord(base, ofst)
	local val = cpuWordRead(base + ofst)
	if not val then
		print("Reading failed")
		return nil
	end
	
	local complement = (val >> 16) & 0xffff
	local val = val & 0xffff
	
	if (val == 0xffff) and (complement == 0xffff) then
		print(string.format(" OPTION WORD offset 0x%02X (addr 0x%08x) is currently erased", ofst, base + ofst))
	elseif (val + complement) == 0xffff then
		print(string.format(" OPTION WORD offset 0x%02X (addr 0x%08x) has a value of 0x%04X (complement matches)", ofst, base + ofst, val))
	else
		print(string.format(" OPTION WORD offset 0x%02X (addr 0x%08x) has a value of 0x%04X; complement mismatch (0x%04X)", ofst, base + ofst, val, complement))
	end
end

function main(step, haveDbg, haveCpu, haveScpt)

	if step == TOOL_OP_STEP_PRE_SCRIPT then
	
		cpuStop()
	
		local memsz = cpuWordRead(0x1FFF7A20) >> 16
		local hasTwoBanks = false
		
		print(string.format(" Device memory size is %uKB", memsz))
	
		if memsz == 1024 then
			print(" Setting 1MB device to single-bank mode")
			cpuWordWrite(0x40023C14, cpuWordRead(0x40023C14) &~ 0x40000000)
		end
		
		if memsz > 1024 then
			print(" Device has dual option words")
			hasTwoBanks = true
		end
		
	
		if mOp == "read" then
		
			if hasTwoBanks then
				readOneOptionWord(OPTION_BYTES_ADDR_B1, 0)
			end
			readOneOptionWord(OPTION_BYTES_ADDR, 0)
			if hasTwoBanks then
				readOneOptionWord(OPTION_BYTES_ADDR_B1, 8)
			end
			readOneOptionWord(OPTION_BYTES_ADDR, 8)
			
		elseif mOp == "write" and hasTwoBanks then
			print(" 'write' operation not supported on a dual-bank device")
		elseif mOp == "dualwrite" and not hasTwoBanks then
			print(" 'dualwrite' operation not supported on a single-bank device")
		else
			local optcrval;
			
			--in either case unlock writing
			if not flashWait() then
				error("Failed to wait for flash")
			end
			print(" unlocking flash")
			if not cpuWordWrite(FLASH_KEYR_ADDR, FLASH_KEYR_VAL_1) then
				error("Failed to write KEYR one")
			end
			if not cpuWordWrite(FLASH_KEYR_ADDR, FLASH_KEYR_VAL_2) then
				error("Failed to write KEYR two")
			end
			print(" unlocking option byte access")
			if not cpuWordWrite(FLASH_OPTKEYR_ADDR, FLASH_KEYR_VAL_1) then
				error("Failed to write OPTKEYR one")
			end
			if not cpuWordWrite(FLASH_OPTKEYR_ADDR, FLASH_KEYR_VAL_2) then
				error("Failed to write OPTKEYR two")
			end
			
			local val = cpuWordRead(FLASH_CR_ADDR)
			if not val then
				error("Failed to read FLASH_CR_ADDR")
			end
			print(string.format(" - FLASH_CR=0x%08X", val))
			
			local val = cpuWordRead(FLASH_OPTCR_ADDR)
			if not val then
				error("Failed to read FLASH_OPTCR_ADDR")
			end
			print(string.format(" - FLASH_OPTCR_ADDR=0x%08X", val))
			
			if (val & FLASH_OPTCR_OPTLOCK) ~= 0 then
				error("Failed flash still locked")
			end
			
			if hasTwoBanks then
				if not cpuWordWrite(FLASH_OPTCR_ADDR, mVal1) then
					error("Failed to write OPTCR")
				end
				if not cpuWordWrite(FLASH_OPTCR1_ADDR, mVal2) then
					error("Failed to write OPTCR1")
				end
			else
				if not cpuWordWrite(FLASH_OPTCR_ADDR, mVal) then
					error("Failed to write OPTCR")
				end
			end
			
			optcrval = cpuWordRead(FLASH_OPTCR_ADDR) | FLASH_OPTCR_OPTSTRT
			if not cpuWordWrite(FLASH_OPTCR_ADDR, optcrval) then
				error("Failed to write OPTCR start")
			end
			
			if not flashWait() then
				error("Failed to wait for flash")
			end
			
			print("DONE")
			
			cpuReset()
			
		end
	end
	
	return true
end

function splitstr(inputstr, sep)
	if not sep then
		sep = "%s"
	end
	local t = {}
	local i = 1
	for str in string.gmatch(inputstr, "([^"..sep.."]+)") do
		t[i] = str
		i = i + 1
	end
	return t
end

function showhelp()
	print("This special function needs parameters. Eg:\n\tread\n\tdualwrite <VALUE1> <VALUE2>\n\twrite <VALUE>")
	return nil
end

function readint(str)
	
	if not str then
		return nil
	end
	
	return tonumber(str)
end

function init(params)

	if not params then
		return showhelp()
	end
	
	local params = splitstr(params, " ")
	if #params < 1 then
		return showhelp()
	end
	
	if params[1] == "read" then
		mOp = params[1]
		if #params ~= 1 then
			return showhelp()
		end
	elseif params[1] == "dualwrite" then
		mOp = params[1]
		mVal1 = readint(params[2])
		mVal2 = readint(params[3])
		
		print(string.format("#params = %x", #params))
		
		if #params ~= 3 or not mVal1 or not mVal2 then
			return showhelp()
		end
	elseif params[1] == "write" then
		mOp = params[1]
		mVal = readint(params[2])
		if #params ~= 2 or not mVal then
			return showhelp()
		end
	else
		return showhelp()
	end
	
	return TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU
end