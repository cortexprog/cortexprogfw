--needs to be tested. docs say this cannot be done over swd, only serial. but it should work this way. we'll test
--we may need a "STRH" gadget. maybe in ram?

function main(step, haveDbg, haveCpu, haveScpt)

	if step == TOOL_OP_STEP_PRE_SCRIPT then
	
		print("Erasing an MB9 chip")
		
		--disable WDT
		if not cpuWordWrite(0x40011c00, 0x1ACCE551) or not cpuWordWrite(0x40011c00, 0xE5331AAE) or not cpuWordWrite(0x40011000, 0) then
			print("Failed to disable WDT")
			return false
		end
	
		if not cpuWordWrite(0x100, 0xF0) or not cpuWordWrite(0xAA8, 0xAA) or not cpuWordWrite(0x554, 0x55) or not cpuWordWrite(0xAA8, 0x80) or not cpuWordWrite(0xAA8, 0xAA) or not cpuWordWrite(0x554, 0x55) or not cpuWordWrite(0xAA8, 0x10) then
			print("Failed to start erase")
			return false
		end
		
		
		print("Erase started...")
		
		--dummy read
		cpuWordRead(0x40000008)
		
		repeat
			ret = cpuWordRead(0x40000008)
			if not ret then
				print("Failed to read Flash status")
				return false
			end
		until (ret & 3) == 1
		
		print("Success, resetting chip")
		
		cpuReset()
		
	end
	
	return true
end

function init(params)

	return TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU
end