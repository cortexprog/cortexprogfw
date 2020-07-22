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
 FLASH_CR_VAL_OPTWRE	= 0x00000200
 FLASH_CR_VAL_STRT		= 0x00000040
 FLASH_CR_VAL_OPTER		= 0x00000020
 FLASH_CR_VAL_OPTPG		= 0x00000010
FLASH_SR_ADDR			= FLASH_BASE_ADDR + 0x14
FLASH_OBR_ADDR			= FLASH_BASE_ADDR + 0x1C
FLASH_WRPR_ADDR			= FLASH_BASE_ADDR + 0x20

RAM_LOC					= 0x20000000
INSTR_STRH_R1_to_R0		= 0x8001

OPTION_TYPES_ADDR		= 0x1FFFF800
OPTION_BYTE_NUM			= 16


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

function main(step, haveDbg, haveCpu, haveScpt)

	if step == TOOL_OP_STEP_PRE_SCRIPT then
	
		cpuStop()
	
		if mOp == "read" then
		
			local val = cpuWordRead(OPTION_TYPES_ADDR + (mAddr & 0xfffffffc))
			if not val then
				print("Reading failed")
				return nil
			end
			if (mAddr & 2) ~= 0 then
				val = val >> 16
			end
			
			local complement = (val >> 8) & 0xff
			local val = val & 0xff
			
			if (val == 0xff) and (complement == 0xff) then
				print(string.format(" OPTION BYTE offset 0x%02X is currently erased", mAddr))
			elseif (val + complement) == 0xff then
				print(string.format(" OPTION BYTE offset 0x%02X has a value of 0x%02X (complement matches)", mAddr, val))
			else
				print(string.format(" OPTION BYTE offset 0x%02X has a value of 0x%02X; complement mismatch (0x%02X)", mAddr, val, complement))
			end
			
		else
		
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
				error("Failed to read CR")
			end
			
			if (val & FLASH_CR_VAL_OPTWRE) == 0 then
				error("Failed to set CR.OPTWRE")
			end
			
			print(string.format(" - FLASH_CR=0x%08X", cpuWordRead(FLASH_CR_ADDR)))
			
			if mOp == "erase" then
				print(" erasing option bytes...")
				val = val | FLASH_CR_VAL_OPTER
				if not cpuWordWrite(FLASH_CR_ADDR, val) then
					error("Failed to set CR.OPTER")
				end
				val = val | FLASH_CR_VAL_STRT
				if not cpuWordWrite(FLASH_CR_ADDR, val) then
					error("Failed to set CR.STRT")
				end
				print("  waiting for completion")
				if not flashWait() then
					error("Failed to wait for flash")
				end
			elseif mOp == "write" then
				print(string.format(" writing option byte %u with value 0x%02X...", mAddr, mVal))
				val = val | FLASH_CR_VAL_OPTPG
				if not cpuWordWrite(FLASH_CR_ADDR, val) then
					error("Failed to set CR.OPTPG")
				end
				print("  saving value to R1")
				if not cpuRegSet(1, mVal) then
					error("cannot set R1")
				end
				print("  saving location to R0")
				if not cpuRegSet(0, OPTION_TYPES_ADDR + (mAddr & 0xfffffffe)) then
					error("cannot set R0")
				end
				print("  writing instruction to RAM")
				if not cpuWordWrite(RAM_LOC, INSTR_STRH_R1_to_R0) then
					error("cannot write")
				end
				print("  reading instruction from RAM")
				val = cpuWordRead(RAM_LOC)
				if val ~= INSTR_STRH_R1_to_R0 then
					error("cannot verify write")
				end
				print("  setting SR.T")
				if not cpuRegSet(16, 0x01000000) then
					error("cannot set SR")
				end
				print("  setting PC")
				if not cpuRegSet(15, RAM_LOC) then
					error("cannot set PC")
				end
				print("  verifying PC")
				if cpuRegGet(15) ~= RAM_LOC then
					error("PC set failed")
				end
				print("  stepping")
				if not cpuStep() then
					error("STEP failed")
				end
				print("  verifying post-step PC")
				if cpuRegGet(15) ~= RAM_LOC + 2 then
					error("STEP didn't work")
				end
				print("  waiting for completion")
				if not flashWait() then
					error("Failed to wait for flash")
				end
				
			end
			
			val = cpuWordRead(FLASH_CR_ADDR)
			if not val then
				error("Failed to read CR")
			end
			
			print(string.format(" - FLASH_CR=0x%08X", cpuWordRead(FLASH_CR_ADDR)))
			
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
	print("This special function needs parameters. Eg:\n\tread <ADDRESS>\n\terase\n\twrite <ADDRESS> <VALUE>")
	return nil
end

function readbyte(str)
	
	if not str then
		return nil
	end
	
	local val = tonumber(str)
	
	if val < 0 or val > 255 then
		return nil
	end
	
	return val
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
		mOp = "read"
		mAddr = readbyte(params[2])
		if #params ~= 2 or not mAddr or mAddr >= OPTION_BYTE_NUM or (mAddr & 1) ~= 0 then
			return showhelp()
		end
	elseif params[1] == "erase" then
		mOp = "erase"
		if #params ~= 1 then
			return showhelp()
		end
	elseif params[1] == "write" then
		mOp = "write"
		mAddr = readbyte(params[2])
		mVal = readbyte(params[3])
		if #params ~= 3 or not mAddr or mAddr >= OPTION_BYTE_NUM or (mAddr & 1) ~= 0 or not mVal then
			return showhelp()
		end
	else
		return showhelp()
	end
	
	return TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU
end