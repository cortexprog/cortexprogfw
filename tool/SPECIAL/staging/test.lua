
SWD_ADDR_DPIDR_R = 0
SWD_ADDR_ABORT_W = 0
SWD_ADDR_SELECT_W = 2
SWD_ADDR_RDBUFF_R = 3

AP_ADDR_IDR = 0xFC

SWD_REG_BANK_MASK = 0xF0
SWD_REG_SEL_MASK = 0x0C
SWD_REG_SEL_SHIFT = 2

mSelectedAp = 0xffffffff

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

	if step == TOOL_OP_STEP_PRE_SCRIPT then
	
		print("Checking on APs")
	
		for i=0,255 do
			
			val = swdApRegRead(i, AP_ADDR_IDR)
			if val == nil or val == 0 then break end
			print(string.format("AP %d has id 0x%08x", i, val))
		end
	end
	
	return true
end

function init(params)

	return TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_WANTS_CPU
end