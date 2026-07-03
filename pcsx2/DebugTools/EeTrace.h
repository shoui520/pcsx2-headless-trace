// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <string>

class Error;

namespace Pcsx2Trace
{
	struct EeTraceConfig
	{
		std::string output_path;
		std::string match_trace_path;
		u64 max_records = 0;
		u64 max_instruction_records = 0;
		u64 skip_records = 0;
		bool match_ignore_timing_state = false;
		bool defer_match_limit_until_mem_trace = false;
		bool wait_for_elf_entry = true;
	};

	bool StartEeTrace(const EeTraceConfig& config, Error* error = nullptr);
	void StopEeTrace();

	bool IsEeTraceEnabled();
	bool RecordEePreInstruction(u32 pc, u32 opcode);
	void NotifyEeElfEntry(u32 pc);
	bool DidEeTraceRecordLastInstruction();
	bool ShouldEeTraceStopAfterMemTrace();

	u64 GetEeTraceRecordsWritten();
	bool DidEeTraceHitLimit();
	const std::string& GetEeTraceError();
} // namespace Pcsx2Trace
