

--[[

CODE we need to upload:

00			J			main
		
		fail:
01			EXIT		1
		
		write_ap_then_read:
02			MOV			r0, 1
03			CALLNATIVE	SWD_WRITE
04			BRZ			r0, fail
06			MOV			r0, 0
07			MOV			r1, 3
08			CALLNATIVE	SWD_READ
09			BRZ			r0, fail
0B			RET
		
		wrword:
0C			MOV			r1, 1
0D			MOV			r2, r4
0E			CALL		write_than_read
0F			MOV			r1, 3
10			MOV			r2, r5
11			J			write_than_read
		
		main:
			
			//lower RST
12			MOV			r0, 0
13			CALLNATIVE	RESET_CTL
14			BRZ			r0, fail
			
			//wait
16			MOV			r0, 128
		delay:
17			SUB			r0, 1
18			BRNZ		r0, delay
			
			//re-raise RST
1A			MOV			r0, #1
1B			CALLNATIVE	RESET_CTL
1C			BRZ			r0, fail
			
			
			//try this a few times
1E			MOV			r4, 20
		try:
1F			SUB			r4, 1
20			BRZ			r4, fail
22			MOV			r0, 0
23			MOV			r1, 3
24			MOV			r2, 0x7B0C06DB
27			CALLNATIVE  SWD_WRITE
28			BRZ			r0, try
			
2A			MOV			r4, 0x40050210
2D			MOV			r5, 0xEA7E30A9
30			CALL		wrword
31			MOV			r4, 0xE000EDFC
34			MOV			r5, 0xA05F0001
37			CALL		wrword
38			EXIT		0

--]]

function cgLblGetCur()

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_LABEL_GET_CUR, 0, 0, 0)
	if not ret then
		print("Failed to get local lbl")
	end
	
	return ret
end

function cgLblFree(lbl)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_LABEL_FREE, lbl, 0, 0)
	if not ret then
		print("Failed to free local lbl")
	end
end

function cgAllocPredeclLbl()

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_PREDECL_LBL_ALLOC, 0, 0, 0)
	if not ret then
		print("Failed to get predeclared lbl")
	end
	
	return ret
end

function cgPredeclLblToLbl(predeclLbl)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_PREDECL_LBL_TO_LBL, predeclLbl, 0, 0)
	if not ret then
		print("Failed to convert predeclared lbl to lbl")
	end
	
	return ret
end

function cgPredeclLblFill(predeclLbl, dstLbl)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_PREDECL_LBL_FILL, predeclLbl, dstLbl, 0)
	if not ret then
		print("Failed to fill predeclared lbl")
	end
end

function cgPredeclLblFree(predeclLbl)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_PREDECL_LBL_FREE, predeclLbl, 0, 0)
	if not ret then
		print("Failed to free predeclared lbl")
	end
end

function cgEmitCall(toLbl)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_CALL_GENERATED, toLbl, 0, 0)
	if not ret then
		print("Failed to emit call")
	end
end

function cgEmitJump(toLbl)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_BRANCH_UNCONDITIONAL, toLbl, 0, 0)
	if not ret then
		print("Failed to emit unconditional jump")
	end
end

function cgEmitBranchIfZero(regNo, toLbl)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_BRANCH_ZERO, regNo, toLbl, 0)
	if not ret then
		print("Failed to emit brz")
	end
end

function cgEmitBranchIfNonzero(regNo, toLbl)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_BRANCH_NOT_ZERO, regNo, toLbl, 0)
	if not ret then
		print("Failed to emit brnz")
	end
end

function cgEmitCallnative(nativeFuncIdx)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_CALL_NATIVE, nativeFuncIdx, 0, 0)
	if not ret then
		print("Failed to emit callnative")
	end
end

function cgEmitRet()

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_RETURN, 0, 0, 0)
	if not ret then
		print("Failed to emit ret")
	end
end

function cgEmitExit(exitCode)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_EXIT, exitCode, 0, 0)
	if not ret then
		print("Failed to emit exit")
	end
end

function cgEmitLdrImm(regNo, immVal)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_LDR_IMM, regNo, immVal, 0)
	if not ret then
		print("Failed to emit ldr_imm")
	end
end

function cgEmitMov(regNoDst, regNoSrc)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_MOV, regNoDst, regNoSrc, 0)
	if not ret then
		print("Failed to emit mov")
	end
end

function cgEmitSubImm(regNoDst, imm)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_SUB_IMM, regNoDst, imm, 0)
	if not ret then
		print("Failed to emit sub_imm")
	end
end

function cgEmitPush(regNo)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_PUSH, regNo, 0, 0)
	if not ret then
		print("Failed to emit push")
	end
end

function cgEmitPop(regNo)

	local ret = dbgCodeUploadAddOpcode(SWD_UPLOAD_OPCODE_POP, regNo, 0, 0)
	if not ret then
		print("Failed to emit pop")
	end
end

function main(step, haveDbg, haveCpu, haveScpt)

	if step == TOOL_OP_STEP_PRE_CPUID then
	
		print("Preparing PSoC5 upload")
	
		if not dbgCodeUploadInit() then
			print("Failed to init upload")
			return false
		end
		
		local lblMain = cgAllocPredeclLbl()
		cgEmitJump(cgPredeclLblToLbl(lblMain))
		
		local lblFail = cgLblGetCur()
		
		--so debugger helps
		
		cgEmitLdrImm(0, 1)
		cgEmitLdrImm(1, 3)
		cgEmitLdrImm(2, 0)
		cgEmitCallnative(SWD_UPLOAD_NATIVE_FUNC_SWD_WRITE)
		
		cgEmitExit(1)
		
		local lblWriteApThenRead = cgLblGetCur()
		
		cgEmitPush(4)
		cgEmitLdrImm(4, 5)
		
		local lblWriteApThenReadRetry = cgLblGetCur()
		cgEmitSubImm(4, 1)
		cgEmitBranchIfZero(4, lblFail)
		
		cgEmitLdrImm(0, 1)
		cgEmitCallnative(SWD_UPLOAD_NATIVE_FUNC_SWD_WRITE)
		cgEmitBranchIfZero(0, lblWriteApThenReadRetry)
		cgEmitPop(4)
		cgEmitRet()

		local lblWrWord = cgLblGetCur()
		cgEmitLdrImm(1, 1)
		cgEmitMov(2, 4)
		cgEmitCall(lblWriteApThenRead)
		cgEmitLdrImm(1, 3)
		cgEmitMov(2, 5)
		cgEmitJump(lblWriteApThenRead)
		
		--main
		local lblMainStart = cgLblGetCur()
		cgPredeclLblFill(lblMain, lblMainStart)
		
		cgEmitLdrImm(5, 0x7B0C06DB)
		cgEmitLdrImm(4, 64)
		
		cgEmitLdrImm(0, 0)
		cgEmitCallnative(SWD_UPLOAD_NATIVE_FUNC_RESET_CTL)
		
		cgEmitMov(0, 0)	--some NOPS for delay (we only need a uS)
		cgEmitMov(0, 0)
		
		--release reset
		

		--do the thing (and relese reset)
		local lblTry = cgLblGetCur()
		cgEmitSubImm(4, 1)
		cgEmitBranchIfZero(4, lblFail)
		cgEmitLdrImm(1, 3)
		cgEmitMov(2, 5)
		cgEmitLdrImm(0, 1)
		cgEmitCallnative(SWD_UPLOAD_NATIVE_FUNC_RESET_CTL)
		cgEmitLdrImm(0, 0)
		cgEmitCallnative(SWD_UPLOAD_NATIVE_FUNC_SWD_WRITE)
		cgEmitBranchIfZero(0, lblTry)
	
		cgEmitLdrImm(4, 0x40050210)
		cgEmitLdrImm(5, 0xEA7E30A9)
		cgEmitCall(lblWrWord)
		cgEmitLdrImm(4, 0xE000EDFC)
		cgEmitLdrImm(5, 0xA05F0001)
		cgEmitCall(lblWrWord)
		
		cgEmitExit(0)

		--now free all labels
		cgPredeclLblFree(lblMain)
		cgLblFree(lblFail)
		cgLblFree(lblWriteApThenRead)
		cgLblFree(lblWrWord)
		cgLblFree(lblMainStart)
		cgLblFree(lblTry)
		cgLblFree(lblWriteApThenReadRetry)

		print("uploaded, running")
		local r0, r1, r2, r3 = dbgCodeUploadRun(0, 0, 0, 0)
		if not r0 then
			print("Failed to run")
		end
		print(string.format("regs = 0x%08x 0x%08x 0x%08x 0x%08x", r0, r1, r2, r3))
		
		
		
	end
	
	return true
end

function init(params)

	return TOOL_OP_NEEDS_DEBUGGER
end