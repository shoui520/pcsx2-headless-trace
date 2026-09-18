// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <string>

class Error;

namespace Pcsx2Trace
{
	using EePreInstructionCallback = void (*)();
	struct EeTraceConfig
	{
		std::string output_path;
		std::string match_trace_path;
		u64 max_records = 0;
		u64 max_instruction_records = 0;
		u64 skip_records = 0;
		u64 after_sif_records = 0;
		bool match_ignore_timing_state = false;
		// Scout-only subsequence matching. Repeated code can select a different
		// architectural phase, so callers must not treat the result as a state oracle.
		bool match_pc_only = false;
		bool defer_match_limit_until_mem_trace = false;
		bool wait_for_elf_entry = true;
		// Opt-in Region IR oracle schema. Ordinary EE traces retain the compact
		// version-1 layout used by the cross-host execution validators.
		bool capture_vu0_state = false;
		// Write one complete EE trace-schema record at the common ELF-entry seam.
		// This does not require an interpreter pre-instruction hook and is
		// therefore valid with the EE recompiler.
		bool record_elf_entry_state = false;
	};

	bool StartEeTrace(const EeTraceConfig& config, Error* error = nullptr);
	void StopEeTrace();

	bool IsEeTraceEnabled();
	void SetEePreInstructionCallback(EePreInstructionCallback callback);
	bool RecordEePreInstruction(u32 pc, u32 opcode);
	void NotifyEeElfEntry(u32 pc);
	void RecordEeElfEntryState(u32 pc);
	bool DidEeTraceRecordLastInstruction();
	bool ShouldEeTraceStopAfterMemTrace();

	u64 GetEeTraceRecordsWritten();
	bool DidEeTraceHitLimit();
	const std::string& GetEeTraceError();
} // namespace Pcsx2Trace
