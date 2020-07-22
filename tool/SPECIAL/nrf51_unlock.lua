NRF51_NVMC_BASE		= 0x4001E000
NRF51_NVMC_CONFIG	= NRF51_NVMC_BASE + 0x504
NRF51_NVMC_ERASEALL	= NRF51_NVMC_BASE + 0x50C


function main(step, haveDbg, haveCpu, haveScpt)

	if step == TOOL_OP_STEP_PRE_SCRIPT then
	
		print("Erasing an nRF51 chip")
	
		ret = cpuWordWrite(NRF51_NVMC_CONFIG,  2)
		if (not ret) or (cpuWordRead(NRF51_NVMC_CONFIG) ~= 2) then
			print("Failed to write NVMC.CONFIG")
			return false
		end
		
		ret = cpuWordWrite(NRF51_NVMC_ERASEALL, 1)
		cpuWordRead(NRF51_NVMC_ERASEALL)	--force a read
		if not ret then
			print("Failed to write NVMC.ERASEALL")
			return false
		end
		print("Erase started...")
		
		repeat
			ret = cpuWordRead(NRF51_NVMC_ERASEALL)
			if not ret then
				print("Failed to read NVMC.ERASEALL")
				return false
			end
		until (ret & 1) == 0
		
		print("Success, resetting chip")
		
		cpuReset()
		
	end
	
	return true
end

function init(params)

	return TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU
end