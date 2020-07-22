EFM32HG_AAP_CMD						= 0xF0E00000
EFM32HG_AAP_CMDKEY					= 0xF0E00004
EFM32HG_AAP_STATUS					= 0xF0E00008
EFM32HG_AAP_IDR						= 0xF0E000FC

EFM32HG_AAP_IDR_VAL					= 0x16E60001
EFM32HG_AAP_CMDKEY_VAL				= 0xCFACC118
EFM32HG_AAP_CMD_VAL_ERASE			= 1
EFM32HG_AAP_CMD_VAL_RESET			= 2
EFM32HG_AAP_STATUS_ERASING			= 1


function main(step, haveDbg, haveCpu, haveScpt)

	if step == TOOL_OP_STEP_PRE_SCRIPT then
	
		print(" attempting to erase an EFM32 Happy Gecko chip")
	
		if cpuWordRead(EFM32HG_AAP_IDR) ~= EFM32HG_AAP_IDR_VAL then
			print(" EFM32HG_AAP_IDR value invalid. Refusing to proceed")
			return false
		end
		
		print(" AAP found")
		if not cpuWordWrite(EFM32HG_AAP_CMDKEY, EFM32HG_AAP_CMDKEY_VAL) then
			print(" Failed to write EFM32HG_AAP_CMDKEY 1")
			return false
		end
		
		print(" Erasing...")
		if not cpuWordWrite(EFM32HG_AAP_CMD, EFM32HG_AAP_CMD_VAL_ERASE) then
			print(" Failed to write EFM32HG_AAP_CMD 1")
			return false
		end
		
		if not cpuWordWrite(EFM32HG_AAP_CMDKEY, 0) then
			print(" Failed to write EFM32HG_AAP_CMDKEY 2")
			return false
		end
		
		repeat
			ret = cpuWordRead(EFM32HG_AAP_STATUS)
			if not ret then
				print(" Failed to read EFM32HG_AAP_STATUS\n")
				return false
			end
		until (ret & EFM32HG_AAP_STATUS_ERASING) == 0
		
		print(" Erase done, resetting")
		if not cpuWordWrite(EFM32HG_AAP_CMDKEY, EFM32HG_AAP_CMDKEY_VAL) then
			print(" Failed to write EFM32HG_AAP_CMDKEY 3")
			return false
		end
		
		if not cpuWordWrite(EFM32HG_AAP_CMD, EFM32HG_AAP_CMD_VAL_RESET) then
			print(" Failed to write EFM32HG_AAP_CMD 2")
			return false
		end
		
		print(" Success")
		
	end
	
	return true
end

function init(params)

	return TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU
end