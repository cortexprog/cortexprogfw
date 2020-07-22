
SWD_ADDR_DPIDR_R = 0
SWD_ADDR_ABORT_W = 0
SWD_ADDR_SELECT_W = 2
SWD_ADDR_RDBUFF_R = 3

AP_ADDR_IDR = 0xFC

SWD_REG_BANK_MASK = 0xF0
SWD_REG_SEL_MASK = 0x0C
SWD_REG_SEL_SHIFT = 2

mSelectedAp = 0xffffffff

REG_CSW	= 0x00
REG_TAR = 0x04
REG_DRW = 0x0C

DSU_BASE = 0x41002100	-- offset of 0x100 for special access while in protectefd mode (see docs)

--you may need to touch reset pin to gorund while running this to get the chip to talk to you

function swdSelectApAndRegBank(apNum, regNum)

	desiredVal = (apNum << 24) | (regNum & SWD_REG_BANK_MASK)
	
	if desiredVal == mSelectedAp then
		return true
	end
	
	ret = dbgSwdWrite(0, SWD_ADDR_SELECT_W, desiredVal)
	if ret == nil then
		return nil
	end
	
	mSelectedAp = desiredVal
	return true
end


function swdApRegRead(apNum, addr)

	regSel = (addr & SWD_REG_SEL_MASK) >> SWD_REG_SEL_SHIFT;
	
	--setup the bank
	ret = swdSelectApAndRegBank(apNum, addr)
	if ret == nil then
		return nil
	end
	
	--request read of given reg
	ret = dbgSwdRead(1, regSel)
	if ret == nil then
		return nil
	end
	
	--let it cycle through the AP & get the result
	ret = dbgSwdRead(0, SWD_ADDR_RDBUFF_R)
	if ret == nil then
		return nil
	end
	
	--done
	return ret
end

function swdApRegWrite(apNum, addr, val)

	regSel = (addr & SWD_REG_SEL_MASK) >> SWD_REG_SEL_SHIFT;
	
	--setup the bank
	ret = swdSelectApAndRegBank(apNum, addr)
	if ret == nil then
		return nil
	end
	
	--request read of given reg
	ret = dbgSwdWrite(1, regSel, val)
	if ret == nil then
		return nil
	end
	
	--let it cycle through the AP & get the result
	ret = dbgSwdRead(0, SWD_ADDR_RDBUFF_R)
	if ret == nil then
		return nil
	end
	
	--done
	return true
end


function main(step, haveDbg, haveCpu, haveScpt)

	local i, val
	
	if step == TOOL_OP_STEP_PRE_SCRIPT then
	
		print("Checking on APs")
	
		for i = 0,255 do
			
			val = swdApRegRead(i, AP_ADDR_IDR)
			if val == nil or val == 0 then break end
			if val == 0x04770031 then
			
				print(string.format("MEMAP found at index %u", i))
				
				val = swdApRegRead(i, REG_CSW)
				if not val then break end
				print(string.format("CSW is 0x%08X", val))
				
				val = (val & 0xFFFFFFC8) | 2		--noautoincrement and word accesses (saves us dummy reads)
				if not swdApRegWrite(i, REG_CSW, val) then break end
				print(string.format("CSW set to 0x%08X", val))
				
				val = swdApRegRead(i, REG_CSW)
				if not val then break end
				print(string.format("CSW is 0x%08X", val))
				
				if not swdApRegWrite(i, REG_TAR, DSU_BASE) then break end
				val = swdApRegRead(i, REG_TAR)
				if not val then break end
				print(string.format("TAR is 0x%08X", val))
				
				val = swdApRegRead(i, REG_DRW)
				if not val then break end
				print(string.format("DRW is 0x%08X", val))
				
				val = val | 16
				if not swdApRegWrite(i, REG_DRW, val) then break end
				print "erase write done"
				
				repeat
					val = swdApRegRead(i, REG_DRW)
					if not val then break end
					print(string.format("wainting ... 0x%08X ... ", val))
				
				until ((val & 0x100) ~= 0)
				
				val = val | 1
				if not swdApRegWrite(i, REG_DRW, val) then break end
				print "reset write done"
				
			end
		end
	end
	
	return true
end

function init(params)

	return TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_WANTS_CPU
end