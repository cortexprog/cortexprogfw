
FREESCALE_MDM_AP_ADDR_STATUS		= 0x00
FREESCALE_MDM_AP_ADDR_CONTROL		= 0x04
FREESCALE_MDM_AP_IDR_VAL			= 0x001c0020

FREESCALE_MDM_AP_CONTROL_BIT_RESET	= 0x08
FREESCALE_MDM_AP_CONTROL_BIT_ERASE	= 0x01


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

	if step == TOOL_OP_STEP_PRE_CPUID then
	
		print(" Looking for Freescale MDM AP")
	
		for i=0,255 do
			
			local val = swdApRegRead(i, AP_ADDR_IDR)
			if val == nil or val == 0 then break end
			if val == FREESCALE_MDM_AP_IDR_VAL then
				print(string.format(" Freescale MDM AP found at index %u", i))
				val = swdApRegRead(i, FREESCALE_MDM_AP_ADDR_STATUS)
				if not val then
					error("Failed to read MDM-AP.STATUS")
				end
				print(string.format(" MDM-AP.STATUS = 0x%08X", val))
				
				val = swdApRegRead(i, FREESCALE_MDM_AP_ADDR_CONTROL)
				if not val then
					error("Failed to read MDM-AP.CONTROL")
				end
				print(string.format(" MDM-AP.CONTROL = 0x%08X", val))
				
				print(" Putting chip into reset...")
				if not swdApRegWrite(i, FREESCALE_MDM_AP_ADDR_CONTROL, FREESCALE_MDM_AP_CONTROL_BIT_RESET) then
					error(" Failed to put chip into reset")
				end
				print(" Starting erase...")
				if not swdApRegWrite(i, FREESCALE_MDM_AP_ADDR_CONTROL, FREESCALE_MDM_AP_CONTROL_BIT_RESET | FREESCALE_MDM_AP_CONTROL_BIT_ERASE) then
					error(" Failed to start erase")
				end
				print(" Releaseing the chip from reset...")
				if not swdApRegWrite(i, FREESCALE_MDM_AP_ADDR_CONTROL, FREESCALE_MDM_AP_CONTROL_BIT_ERASE) then
					error(" Failed to release reset")
				end
				
				print(" Waiting...")
				repeat
					val = swdApRegRead(i, FREESCALE_MDM_AP_ADDR_CONTROL)
					if not val then
						error("Failed to read erase status")
					end
				until (val & FREESCALE_MDM_AP_CONTROL_BIT_ERASE) == 0
				
				print(" All done")
				
				return true
			end
		end
		print(" Freescale MDM AP not found")
		return false
	end
	
	return true
end

function init(params)

	return TOOL_OP_NEEDS_DEBUGGER
end