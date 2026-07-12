// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "pcsx2/CDVD/CDVDcommon.h"
#include "pcsx2/Config.h"
#include "pcsx2/DebugTools/CoreEventTrace.h"
#include "pcsx2/DebugTools/EeTrace.h"
#include "pcsx2/DebugTools/GsTrace.h"
#include "pcsx2/DebugTools/IopTrace.h"
#include "pcsx2/DebugTools/IpuTrace.h"
#include "pcsx2/DebugTools/MemTrace.h"
#include "pcsx2/DebugTools/SifTrace.h"
#include "pcsx2/DebugTools/Spu2Trace.h"
#include "pcsx2/DebugTools/VifTrace.h"
#include "pcsx2/DebugTools/VuTrace.h"
#include "pcsx2/Host.h"
#include "pcsx2/IopMem.h"
#include "pcsx2/R5900.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/SIO/Pad/PadDualshock2.h"
#include "pcsx2/VMManager.h"

#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

namespace
{
	static constexpr u64 DEFAULT_MAX_INSTRUCTIONS = 100000;
	static constexpr u64 MAX_IOP_DUMP_SIZE = 1024 * 1024;

	struct TraceOptions
	{
		std::string bios_path;
		std::string elf_path;
		std::string output_path;
		std::string ee_match_trace_path;
		std::string iop_output_path;
		std::string mem_output_path;
		std::string gs_output_path;
		std::string ipu_output_path;
		std::string sif_output_path;
		std::string core_event_output_path;
		std::string spu2_output_path;
		std::string vif_output_path;
		std::string vu_output_path;
		std::string gs_debug_dump_directory;
		std::string iop_dump_path;
		std::string data_root;
		u64 max_instructions = DEFAULT_MAX_INSTRUCTIONS;
		u64 max_iop_instructions = DEFAULT_MAX_INSTRUCTIONS;
		u64 max_mem_instructions = DEFAULT_MAX_INSTRUCTIONS;
		u64 max_mem_records = 0;
		u64 max_gs_instructions = DEFAULT_MAX_INSTRUCTIONS;
		u64 max_gs_records = 0;
		u64 max_ipu_records = 0;
		u64 max_sif_records = 0;
		u64 max_core_event_records = 0;
		u64 max_spu2_records = 0;
		u64 max_vif_records = 0;
		u64 max_vu_instructions = DEFAULT_MAX_INSTRUCTIONS;
		u64 max_vu_records = 0;
		u64 ee_skip_records = 0;
		u64 iop_skip_records = 0;
		u64 mem_skip_records = 0;
		u64 gs_skip_records = 0;
		u64 ipu_skip_records = 0;
		u64 sif_skip_records = 0;
		u64 core_event_skip_records = 0;
		u64 core_event_after_sif_records = 0;
		u64 ee_after_sif_records = 0;
		u64 spu2_skip_records = 0;
		u64 vif_skip_records = 0;
		u64 vu_skip_records = 0;
		u64 mem_hash_interval = Pcsx2Trace::MemTraceDefaultHashInterval;
		u64 iop_dump_address = 0;
		u64 iop_dump_size = 0;
		u32 mem_region_mask = Pcsx2Trace::MemTraceDefaultRegionMask;
		u32 vu_unit_mask = Pcsx2Trace::VuTraceUnitMaskBoth;
		bool wait_for_elf_entry = true;
		bool ee_match_ignore_timing_state = false;
		bool ee_match_pc_only = false;
		bool mem_sample_ee_trace = false;
		bool gs_state_snapshots = false;
		bool gs_state_full_dumps = false;
		bool gs_debug_dump = false;
		bool boot_bios_only = false;
		bool boot_disc = false;
		bool fast_boot = true;
		bool show_help = false;
		bool max_iop_instructions_overridden = false;
		bool max_mem_records_overridden = false;
		bool max_gs_records_overridden = false;
		bool max_ipu_records_overridden = false;
		bool max_sif_records_overridden = false;
		bool max_core_event_records_overridden = false;
		bool max_spu2_records_overridden = false;
		bool max_vu_records_overridden = false;
		bool pad_pulse_script = false;
	};

	std::unique_ptr<MemorySettingsInterface> s_base_settings;
	std::unique_ptr<MemorySettingsInterface> s_secrets_settings;
	u64 s_pad_script_entry_cycle = 0;
	u32 s_pad_script_state = 0;
	bool s_pad_pulse_script_enabled = false;

	void UpdatePadPulseScript()
	{
		// Core-event records are emitted at scheduler/device seams rather than at
		// the EE trace hook.  Leave the interpreter at the next architectural
		// instruction boundary once that independent capture reaches its cap.
		if (Pcsx2Trace::DidCoreEventTraceHitLimit())
		{
			Cpu->ExitExecution();
			return;
		}

		if (!s_pad_pulse_script_enabled || !VMManager::Internal::HasBootedELF())
			return;
		if (s_pad_script_entry_cycle == 0)
			s_pad_script_entry_cycle = cpuRegs.cycle;

		static constexpr u64 EE_CYCLES_PER_SECOND = 294912000;
		static constexpr u64 PERIOD = EE_CYCLES_PER_SECOND * 2;
		static constexpr u64 PULSE = EE_CYCLES_PER_SECOND / 5;
		const u64 elapsed = cpuRegs.cycle - s_pad_script_entry_cycle;
		const u64 event = elapsed / PERIOD;
		const bool pressed = (elapsed % PERIOD) < PULSE;
		const u32 state = pressed ? ((event & 1) ? 2u : 1u) : 0u;
		if (state == s_pad_script_state)
			return;

		s_pad_script_state = state;
		Pad::SetControllerState(0, PadDualshock2::Inputs::PAD_START, state == 1 ? 1.0f : 0.0f);
		Pad::SetControllerState(0, PadDualshock2::Inputs::PAD_CROSS, state == 2 ? 1.0f : 0.0f);
	}

	void PrintUsage(const char* program)
	{
		std::fprintf(stderr,
			"usage: %s <bios-file-or-dir> [elf-or-disc] (--out trace.bin | --iop-out trace.bin | --mem-out trace.bin | --gs-out trace.bin | --ipu-out trace.bin | --sif-out trace.bin | --core-event-out trace.bin | --spu2-out trace.bin | --vif-out trace.bin | --vu-out trace.bin) [options]\n"
			"\n"
			"options:\n"
			"  --boot-bios           Boot the BIOS with no disc and no ELF fast-boot override.\n"
			"  --boot-disc           Boot the positional path as an ISO/disc image.\n"
			"  --full-boot           Disable PCSX2 fast boot for --boot-disc.\n"
			"  --out trace.bin       Write an EE/R5900 pre-instruction trace.\n"
			"  --ee-match-trace trace.bin\n"
			"                         Write only EE records matching this trace, scanning up to --max-instructions.\n"
			"  --ee-match-ignore-timing-state\n"
			"                         Ignore timing-derived EE state while matching --ee-match-trace.\n"
			"  --ee-match-pc-only     Scout a PC/opcode subsequence only; this is not a state oracle.\n"
			"  --iop-out trace.bin   Write an IOP/R3000A pre-instruction trace.\n"
			"  --mem-out trace.bin   Write a guest-memory hash trace from the EE pre-instruction hook.\n"
			"  --mem-sample-ee-trace\n"
			"                         With --ee-match-trace, sample MEM only when the EE trace writes a record.\n"
			"  --gs-out trace.bin    Write decoded GS register/image transfer records.\n"
			"  --ipu-out trace.bin   Write IPU command/output hash records.\n"
			"  --sif-out trace.bin   Write SIF0/SIF1 FIFO data/tag transfer records.\n"
			"  --core-event-out trace.bin\n"
			"                         Write EE/IOP scheduler and device-event records.\n"
			"  --spu2-out trace.bin  Write SPU2 48 kHz mixer output records.\n"
			"  --vif-out trace.bin   Write VIF command and unpack effect records.\n"
			"  --vu-out trace.bin    Write VU0/VU1 interpreter micro-step records.\n"
			"  --pad-pulse-script    Alternate deterministic EE-cycle START/CROSS pulses after ELF entry.\n"
			"  --gs-state-snapshots  Include full GSState/local-memory hash sections in the GS trace.\n"
			"  --gs-state-full       With --gs-state-snapshots, write raw leaf GS state bytes to trace.bin.state.bin.\n"
			"  --gs-debug-dump-dir DIR\n"
			"                         Enable PCSX2 Debug/GS draw dumping to DIR for the software renderer.\n"
			"  --max-instructions N   Stop after N pre-instruction records (default: %llu).\n"
			"  --max-iop-instructions N\n"
			"                         Stop the IOP trace after N records (default: --max-instructions).\n"
			"  --max-mem-records N    Optional cap on MEM region hash records.\n"
			"  --max-mem-instructions N\n"
			"                         Stop the MEM trace after N EE pre-instruction hooks (default: --max-instructions).\n"
			"  --max-gs-records N     Optional cap on decoded GS records.\n"
			"  --max-gs-instructions N\n"
			"                         Stop the GS trace after N EE pre-instruction hooks (default: --max-instructions).\n"
			"  --max-ipu-records N    Optional cap on IPU records.\n"
			"  --max-sif-records N    Optional cap on SIF transfer records.\n"
			"  --max-core-event-records N\n"
			"                         Optional cap on core-event records.\n"
			"  --max-spu2-records N   Optional cap on SPU2 records.\n"
			"  --max-vif-records N    Optional cap on VIF command/unpack records.\n"
			"  --max-vu-records N     Optional cap on VU records.\n"
			"  --max-vu-instructions N\n"
			"                         Stop the VU trace after N EE pre-instruction hooks (default: --max-instructions).\n"
			"  --ee-skip-records N   Skip N EE pre-instruction records before writing.\n"
			"  --ee-after-sif-records N\n"
			"                         Start EE capture after N observed SIF records.\n"
			"  --iop-skip-records N  Skip N IOP pre-instruction records before writing.\n"
			"  --mem-skip-records N  Skip N MEM region hash records before writing.\n"
			"  --gs-skip-records N   Skip N decoded GS records before writing.\n"
			"  --ipu-skip-records N  Skip N IPU records before writing.\n"
			"  --sif-skip-records N  Skip N SIF records before writing.\n"
			"  --core-event-skip-records N\n"
			"                         Skip N core-event records before writing.\n"
			"  --core-event-after-sif-records N\n"
			"                         Start core-event capture after N observed SIF records.\n"
			"  --spu2-skip-records N Skip N SPU2 records before writing.\n"
			"  --vif-skip-records N  Skip N VIF records before writing.\n"
			"  --vu-skip-records N   Skip N VU records before writing.\n"
			"  --mem-hash-interval N Hash selected memory regions every N EE pre-instruction records.\n"
			"  --mem-regions LIST    MEM regions or all: ee_ram,iop_ram,ee_scratchpad,vu0_micro,\n"
			"                         vu0_data,vu1_micro,vu1_data,spu2_ram,gs_local.\n"
			"  --vu-units LIST        VU units to trace: vu0,vu1,both/all or a numeric mask.\n"
			"  --max-blocks N         Compatibility alias for --max-instructions.\n"
			"  --trace-from boot      Start tracing immediately instead of at the ELF entry point.\n"
			"  --trace-from entry     Start tracing at the ELF entry point (default for trace types that can wait).\n"
			"  --iop-dump ADDR SIZE PATH\n"
			"                         Dump a bounded IOP memory span after execution (max 1 MiB).\n"
			"  --data-root DIR        Isolated PCSX2 data root (default: trace output directory/pcsx2-trace-data).\n",
			program, static_cast<unsigned long long>(DEFAULT_MAX_INSTRUCTIONS));
	}

	bool ParseU64(const char* text, u64* value)
	{
		char* end = nullptr;
		const unsigned long long parsed = std::strtoull(text, &end, 0);
		if (!end || *end != '\0')
			return false;

		*value = static_cast<u64>(parsed);
		return true;
	}

	bool MemRegionNameMatches(std::string_view text, const char* name)
	{
		size_t name_length = 0;
		while (name[name_length])
			name_length++;
		if (text.size() != name_length)
			return false;
		for (size_t i = 0; i < text.size(); i++)
		{
			const char ch = text[i] >= 'A' && text[i] <= 'Z' ?
				static_cast<char>(text[i] - 'A' + 'a') : text[i];
			if (ch != name[i])
				return false;
		}
		return true;
	}

	bool ParseMemRegionMask(std::string_view text, u32* mask)
	{
		if (text.empty())
			return false;
		if ((text.front() >= '0' && text.front() <= '9'))
		{
			u64 parsed = 0;
			if (!ParseU64(std::string(text).c_str(), &parsed) || parsed > 0xffffffffu)
				return false;
			*mask = static_cast<u32>(parsed);
			return true;
		}

		u32 parsed_mask = 0;
		size_t start = 0;
		while (start <= text.size())
		{
			const size_t end = text.find(',', start);
			const std::string_view token =
				end == std::string_view::npos ?
					text.substr(start) :
					text.substr(start, end - start);
			if (token.empty())
				return false;
			if (MemRegionNameMatches(token, "all"))
				parsed_mask |= Pcsx2Trace::MemTraceAllRegionMask;
			else if (MemRegionNameMatches(token, "ee_ram"))
				parsed_mask |= Pcsx2Trace::MemTraceRegionMaskEeRam;
			else if (MemRegionNameMatches(token, "iop_ram"))
				parsed_mask |= Pcsx2Trace::MemTraceRegionMaskIopRam;
			else if (MemRegionNameMatches(token, "ee_scratchpad"))
				parsed_mask |= Pcsx2Trace::MemTraceRegionMaskEeScratchpad;
			else if (MemRegionNameMatches(token, "vu0_micro"))
				parsed_mask |= Pcsx2Trace::MemTraceRegionMaskVu0Micro;
			else if (MemRegionNameMatches(token, "vu0_data"))
				parsed_mask |= Pcsx2Trace::MemTraceRegionMaskVu0Data;
			else if (MemRegionNameMatches(token, "vu1_micro"))
				parsed_mask |= Pcsx2Trace::MemTraceRegionMaskVu1Micro;
			else if (MemRegionNameMatches(token, "vu1_data"))
				parsed_mask |= Pcsx2Trace::MemTraceRegionMaskVu1Data;
			else if (MemRegionNameMatches(token, "spu2_ram"))
				parsed_mask |= Pcsx2Trace::MemTraceRegionMaskSpu2Ram;
			else if (MemRegionNameMatches(token, "gs_local"))
				parsed_mask |= Pcsx2Trace::MemTraceRegionMaskGsLocal;
			else
				return false;
			if (end == std::string_view::npos)
				break;
			start = end + 1;
		}

		if (parsed_mask == 0)
			return false;
		*mask = parsed_mask;
		return true;
	}

	bool ParseVuUnitMask(std::string_view text, u32* mask)
	{
		if (text.empty())
			return false;
		if ((text.front() >= '0' && text.front() <= '9'))
		{
			u64 parsed = 0;
			if (!ParseU64(std::string(text).c_str(), &parsed) || parsed > 0xffffffffu)
				return false;
			*mask = static_cast<u32>(parsed);
			return true;
		}

		u32 parsed_mask = 0;
		size_t start = 0;
		while (start <= text.size())
		{
			const size_t end = text.find(',', start);
			const std::string_view token =
				end == std::string_view::npos ?
					text.substr(start) :
					text.substr(start, end - start);
			if (token.empty())
				return false;
			if (MemRegionNameMatches(token, "all") || MemRegionNameMatches(token, "both"))
				parsed_mask |= Pcsx2Trace::VuTraceUnitMaskBoth;
			else if (MemRegionNameMatches(token, "vu0"))
				parsed_mask |= Pcsx2Trace::VuTraceUnitMaskVu0;
			else if (MemRegionNameMatches(token, "vu1"))
				parsed_mask |= Pcsx2Trace::VuTraceUnitMaskVu1;
			else
				return false;
			if (end == std::string_view::npos)
				break;
			start = end + 1;
		}

		if (parsed_mask == 0)
			return false;
		*mask = parsed_mask;
		return true;
	}

	bool ParseCommandLine(int argc, char** argv, TraceOptions* options)
	{
		for (int i = 1; i < argc; i++)
		{
			const std::string_view arg(argv[i]);
			if (arg == "--help" || arg == "-h")
			{
				PrintUsage(argv[0]);
				options->show_help = true;
				return true;
			}
			else if (arg == "--out")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--out requires a path.\n");
					return false;
				}
				options->output_path = argv[i];
			}
			else if (arg == "--ee-match-trace")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--ee-match-trace requires a path.\n");
					return false;
				}
				options->ee_match_trace_path = argv[i];
			}
			else if (arg == "--ee-match-ignore-timing-state")
			{
				options->ee_match_ignore_timing_state = true;
			}
			else if (arg == "--ee-match-pc-only")
			{
				options->ee_match_pc_only = true;
			}
			else if (arg == "--iop-out")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--iop-out requires a path.\n");
					return false;
				}
				options->iop_output_path = argv[i];
			}
			else if (arg == "--mem-out")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--mem-out requires a path.\n");
					return false;
				}
				options->mem_output_path = argv[i];
			}
			else if (arg == "--mem-sample-ee-trace")
			{
				options->mem_sample_ee_trace = true;
			}
			else if (arg == "--gs-out")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--gs-out requires a path.\n");
					return false;
				}
				options->gs_output_path = argv[i];
			}
			else if (arg == "--ipu-out")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--ipu-out requires a path.\n");
					return false;
				}
				options->ipu_output_path = argv[i];
			}
			else if (arg == "--sif-out")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--sif-out requires a path.\n");
					return false;
				}
				options->sif_output_path = argv[i];
			}
			else if (arg == "--core-event-out")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--core-event-out requires a path.\n");
					return false;
				}
				options->core_event_output_path = argv[i];
			}
			else if (arg == "--spu2-out")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--spu2-out requires a path.\n");
					return false;
				}
				options->spu2_output_path = argv[i];
			}
			else if (arg == "--vif-out")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--vif-out requires a path.\n");
					return false;
				}
				options->vif_output_path = argv[i];
			}
			else if (arg == "--vu-out")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--vu-out requires a path.\n");
					return false;
				}
				options->vu_output_path = argv[i];
			}
			else if (arg == "--pad-pulse-script")
			{
				options->pad_pulse_script = true;
			}
			else if (arg == "--gs-state-snapshots")
			{
				options->gs_state_snapshots = true;
			}
			else if (arg == "--gs-state-full")
			{
				options->gs_state_snapshots = true;
				options->gs_state_full_dumps = true;
			}
			else if (arg == "--gs-debug-dump-dir")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--gs-debug-dump-dir requires a path.\n");
					return false;
				}
				options->gs_debug_dump = true;
				options->gs_debug_dump_directory = argv[i];
			}
			else if (arg == "--boot-bios")
			{
				options->boot_bios_only = true;
				options->wait_for_elf_entry = false;
			}
			else if (arg == "--boot-disc")
			{
				options->boot_disc = true;
			}
			else if (arg == "--full-boot")
			{
				options->fast_boot = false;
			}
			else if (arg == "--max-instructions" || arg == "--max-blocks")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_instructions))
				{
					std::fprintf(stderr, "%.*s requires an integer.\n", static_cast<int>(arg.size()), arg.data());
					return false;
				}
				if (!options->max_iop_instructions_overridden)
					options->max_iop_instructions = options->max_instructions;
				if (!options->max_mem_records_overridden)
					options->max_mem_instructions = options->max_instructions;
				if (!options->max_gs_records_overridden)
					options->max_gs_instructions = options->max_instructions;
				if (!options->max_ipu_records_overridden)
					options->max_ipu_records = options->max_instructions;
				if (!options->max_sif_records_overridden)
					options->max_sif_records = options->max_instructions;
				if (!options->max_core_event_records_overridden)
					options->max_core_event_records = options->max_instructions;
				if (!options->max_vu_records_overridden)
					options->max_vu_instructions = options->max_instructions;
			}
			else if (arg == "--max-iop-instructions")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_iop_instructions))
				{
					std::fprintf(stderr, "--max-iop-instructions requires an integer.\n");
					return false;
				}
				options->max_iop_instructions_overridden = true;
			}
			else if (arg == "--max-mem-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_mem_records))
				{
					std::fprintf(stderr, "--max-mem-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--max-mem-instructions")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_mem_instructions))
				{
					std::fprintf(stderr, "--max-mem-instructions requires an integer.\n");
					return false;
				}
				options->max_mem_records_overridden = true;
			}
			else if (arg == "--max-gs-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_gs_records))
				{
					std::fprintf(stderr, "--max-gs-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--max-spu2-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_spu2_records))
				{
					std::fprintf(stderr, "--max-spu2-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--max-vif-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_vif_records))
				{
					std::fprintf(stderr, "--max-vif-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--max-ipu-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_ipu_records))
				{
					std::fprintf(stderr, "--max-ipu-records requires an integer.\n");
					return false;
				}
				options->max_ipu_records_overridden = true;
			}
			else if (arg == "--max-sif-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_sif_records))
				{
					std::fprintf(stderr, "--max-sif-records requires an integer.\n");
					return false;
				}
				options->max_sif_records_overridden = true;
			}
			else if (arg == "--max-core-event-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_core_event_records))
				{
					std::fprintf(stderr, "--max-core-event-records requires an integer.\n");
					return false;
				}
				options->max_core_event_records_overridden = true;
			}
			else if (arg == "--max-gs-instructions")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_gs_instructions))
				{
					std::fprintf(stderr, "--max-gs-instructions requires an integer.\n");
					return false;
				}
				options->max_gs_records_overridden = true;
			}
			else if (arg == "--max-vu-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_vu_records))
				{
					std::fprintf(stderr, "--max-vu-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--max-vu-instructions")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_vu_instructions))
				{
					std::fprintf(stderr, "--max-vu-instructions requires an integer.\n");
					return false;
				}
				options->max_vu_records_overridden = true;
			}
			else if (arg == "--iop-skip-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->iop_skip_records))
				{
					std::fprintf(stderr, "--iop-skip-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--mem-skip-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->mem_skip_records))
				{
					std::fprintf(stderr, "--mem-skip-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--gs-skip-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->gs_skip_records))
				{
					std::fprintf(stderr, "--gs-skip-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--spu2-skip-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->spu2_skip_records))
				{
					std::fprintf(stderr, "--spu2-skip-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--vif-skip-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->vif_skip_records))
				{
					std::fprintf(stderr, "--vif-skip-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--ipu-skip-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->ipu_skip_records))
				{
					std::fprintf(stderr, "--ipu-skip-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--sif-skip-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->sif_skip_records))
				{
					std::fprintf(stderr, "--sif-skip-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--core-event-skip-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->core_event_skip_records))
				{
					std::fprintf(stderr, "--core-event-skip-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--core-event-after-sif-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->core_event_after_sif_records))
				{
					std::fprintf(stderr, "--core-event-after-sif-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--ee-after-sif-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->ee_after_sif_records))
				{
					std::fprintf(stderr, "--ee-after-sif-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--vu-skip-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->vu_skip_records))
				{
					std::fprintf(stderr, "--vu-skip-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--mem-hash-interval")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->mem_hash_interval))
				{
					std::fprintf(stderr, "--mem-hash-interval requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--mem-regions")
			{
				if (++i >= argc || !ParseMemRegionMask(argv[i], &options->mem_region_mask))
				{
					std::fprintf(stderr,
						"--mem-regions requires a comma-separated list from ee_ram,iop_ram,ee_scratchpad,vu0_data or a numeric mask.\n");
					return false;
				}
			}
			else if (arg == "--vu-units")
			{
				if (++i >= argc || !ParseVuUnitMask(argv[i], &options->vu_unit_mask))
				{
					std::fprintf(stderr,
						"--vu-units requires vu0,vu1,both/all or a numeric mask.\n");
					return false;
				}
			}
			else if (arg == "--ee-skip-records")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->ee_skip_records))
				{
					std::fprintf(stderr, "--ee-skip-records requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--trace-from")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--trace-from requires 'entry' or 'boot'.\n");
					return false;
				}

				const std::string_view mode(argv[i]);
				if (mode == "entry")
					options->wait_for_elf_entry = true;
				else if (mode == "boot")
					options->wait_for_elf_entry = false;
				else
				{
					std::fprintf(stderr, "--trace-from requires 'entry' or 'boot'.\n");
					return false;
				}
			}
			else if (arg == "--data-root")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--data-root requires a directory.\n");
					return false;
				}
				options->data_root = argv[i];
			}
			else if (arg == "--iop-dump")
			{
				if (i + 3 >= argc ||
					!ParseU64(argv[i + 1], &options->iop_dump_address) ||
					!ParseU64(argv[i + 2], &options->iop_dump_size))
				{
					std::fprintf(stderr, "--iop-dump requires ADDR SIZE PATH.\n");
					return false;
				}
				options->iop_dump_path = argv[i + 3];
				i += 3;
			}
			else if (!arg.empty() && arg.front() == '-')
			{
				std::fprintf(stderr, "Unknown option '%.*s'.\n", static_cast<int>(arg.size()), arg.data());
				return false;
			}
			else if (options->bios_path.empty())
			{
				options->bios_path = argv[i];
			}
			else if (options->elf_path.empty())
			{
				options->elf_path = argv[i];
			}
			else
			{
				std::fprintf(stderr, "Unexpected argument '%.*s'.\n", static_cast<int>(arg.size()), arg.data());
				return false;
			}
		}

		if (options->boot_bios_only && options->boot_disc)
		{
			std::fprintf(stderr, "--boot-bios and --boot-disc are mutually exclusive.\n");
			return false;
		}

		if (options->boot_bios_only)
		{
			options->fast_boot = false;
			if (options->wait_for_elf_entry)
				options->wait_for_elf_entry = false;
			if (!options->elf_path.empty())
			{
				std::fprintf(stderr, "--boot-bios does not accept a boot path positional argument.\n");
				return false;
			}
		}

		if (options->bios_path.empty() ||
			(options->output_path.empty() && options->iop_output_path.empty() &&
				options->mem_output_path.empty() && options->gs_output_path.empty() &&
				options->ipu_output_path.empty() && options->sif_output_path.empty() &&
				options->core_event_output_path.empty() &&
				options->spu2_output_path.empty() &&
				options->vif_output_path.empty() && options->vu_output_path.empty()) ||
			(!options->boot_bios_only && options->elf_path.empty()))
		{
			PrintUsage(argv[0]);
			return false;
		}

		if (options->gs_debug_dump && options->gs_debug_dump_directory.empty())
		{
			std::fprintf(stderr, "--gs-debug-dump-dir requires a non-empty path.\n");
			return false;
		}

		if (!options->ee_match_trace_path.empty() && options->output_path.empty())
		{
			std::fprintf(stderr, "--ee-match-trace requires --out.\n");
			return false;
		}
		if (options->ee_match_pc_only && options->ee_match_trace_path.empty())
		{
			std::fprintf(stderr, "--ee-match-pc-only requires --ee-match-trace.\n");
			return false;
		}

		if (options->mem_sample_ee_trace &&
			(options->ee_match_trace_path.empty() || options->output_path.empty() || options->mem_output_path.empty()))
		{
			std::fprintf(stderr, "--mem-sample-ee-trace requires --ee-match-trace, --out, and --mem-out.\n");
			return false;
		}

		if (options->core_event_after_sif_records != 0 && options->sif_output_path.empty())
		{
			std::fprintf(stderr, "--core-event-after-sif-records requires --sif-out.\n");
			return false;
		}
		if (options->core_event_after_sif_records != 0 && options->core_event_output_path.empty())
		{
			std::fprintf(stderr, "--core-event-after-sif-records requires --core-event-out.\n");
			return false;
		}
		if (options->ee_after_sif_records != 0 && options->sif_output_path.empty())
		{
			std::fprintf(stderr, "--ee-after-sif-records requires --sif-out.\n");
			return false;
		}
		if (options->ee_after_sif_records != 0 && options->output_path.empty())
		{
			std::fprintf(stderr, "--ee-after-sif-records requires --out.\n");
			return false;
		}
		if ((options->core_event_after_sif_records != 0 ||
			 options->ee_after_sif_records != 0) && options->sif_skip_records != 0)
		{
			std::fprintf(stderr,
				"after-SIF gates require --sif-skip-records 0 so counts are absolute.\n");
			return false;
		}
		const u64 required_sif_records = std::max(options->core_event_after_sif_records,
			options->ee_after_sif_records);
		if (required_sif_records != 0 && options->max_sif_records != 0 &&
			options->max_sif_records < required_sif_records)
		{
			std::fprintf(stderr,
				"--max-sif-records must be zero or reach the requested after-SIF gate.\n");
			return false;
		}

		if (!options->fast_boot && !options->boot_disc && !options->boot_bios_only)
		{
			std::fprintf(stderr, "--full-boot is only supported with --boot-disc.\n");
			return false;
		}

		if (!options->iop_dump_path.empty() &&
			(options->iop_dump_size == 0 || options->iop_dump_size > MAX_IOP_DUMP_SIZE ||
				options->iop_dump_address > 0xffffffffu ||
				options->iop_dump_size > (0x100000000ull - options->iop_dump_address)))
		{
			std::fprintf(stderr, "--iop-dump size/address out of range.\n");
			return false;
		}

		return true;
	}

	void SetBool(SettingsInterface& si, const char* section, const char* key, bool value)
	{
		si.SetBoolValue(section, key, value);
	}

	void SetInt(SettingsInterface& si, const char* section, const char* key, int value)
	{
		si.SetIntValue(section, key, value);
	}

	void ConfigureDeterministicSettings(SettingsInterface& si, const TraceOptions& options)
	{
		VMManager::SetDefaultSettings(si, true, true, true, true, true);

		if (FileSystem::DirectoryExists(options.bios_path.c_str()))
		{
			si.SetStringValue("Folders", "Bios", options.bios_path.c_str());
			si.SetStringValue("Filenames", "BIOS", "");
		}
		else
		{
			const std::string bios_directory(Path::GetDirectory(options.bios_path));
			const std::string bios_filename(Path::GetFileName(options.bios_path));
			si.SetStringValue("Folders", "Bios", bios_directory.c_str());
			si.SetStringValue("Filenames", "BIOS", bios_filename.c_str());
		}

		SetBool(si, "Logging", "EnableSystemConsole", true);
		SetBool(si, "Logging", "EnableFileLogging", false);
		SetBool(si, "Logging", "EnableVerbose", false);

		SetBool(si, "EmuCore", "EnableFastBoot", options.fast_boot && !options.boot_bios_only);
		SetBool(si, "EmuCore", "EnableFastBootFastForward", false);
		SetBool(si, "EmuCore", "EnablePatches", false);
		SetBool(si, "EmuCore", "EnableCheats", false);
		SetBool(si, "EmuCore", "EnableWideScreenPatches", false);
		SetBool(si, "EmuCore", "EnableNoInterlacingPatches", false);
		SetBool(si, "EmuCore", "EnableDiscordPresence", false);
		SetBool(si, "EmuCore", "EnablePINE", false);
		SetBool(si, "EmuCore", "EnableGameFixes", false);
		SetBool(si, "EmuCore", "InhibitScreensaver", false);
		SetBool(si, "EmuCore", "WarnAboutUnsafeSettings", false);
		SetBool(si, "EmuCore", "HostFs", false);
		SetBool(si, "EmuCore", "ManuallySetRealTimeClock", true);
		SetInt(si, "EmuCore", "RtcYear", 20);
		SetInt(si, "EmuCore", "RtcMonth", 3);
		SetInt(si, "EmuCore", "RtcDay", 4);
		SetInt(si, "EmuCore", "RtcHour", 0);
		SetInt(si, "EmuCore", "RtcMinute", 0);
		SetInt(si, "EmuCore", "RtcSecond", 0);

		const GSRendererType trace_renderer =
			!options.gs_output_path.empty() ? GSRendererType::SW : GSRendererType::Null;
		SetInt(si, "EmuCore/GS", "Renderer", static_cast<int>(trace_renderer));
		SetInt(si, "EmuCore/GS", "extrathreads", 0);
		SetInt(si, "EmuCore/GS", "extrathreads_height", 0);
		SetBool(si, "EmuCore/GS", "SynchronousMTGS", true);
		SetBool(si, "EmuCore/GS", "VsyncEnable", false);
		if (options.gs_debug_dump)
		{
			FileSystem::EnsureDirectoryExists(options.gs_debug_dump_directory.c_str(), false);
			const std::string gs_debug_dump_directory = Path::RealPath(options.gs_debug_dump_directory);
			si.SetStringValue("EmuCore/GS", "HWDumpDirectory", gs_debug_dump_directory.c_str());
			si.SetStringValue("EmuCore/GS", "SWDumpDirectory", gs_debug_dump_directory.c_str());
			SetBool(si, "EmuCore/GS", "DumpGSData", true);
			// Keep the trace Debug/GS dump log-first. Texture/RT/depth/alpha
			// PNGs can explode into thousands of files and are not the oracle.
			SetBool(si, "EmuCore/GS", "SaveRT", false);
			SetBool(si, "EmuCore/GS", "SaveFrame", false);
			SetBool(si, "EmuCore/GS", "SaveTexture", false);
			SetBool(si, "EmuCore/GS", "SaveDepth", false);
			SetBool(si, "EmuCore/GS", "SaveAlpha", false);
			SetBool(si, "EmuCore/GS", "SaveInfo", true);
			SetBool(si, "EmuCore/GS", "SaveTransferImages", false);
			SetBool(si, "EmuCore/GS", "SaveDrawStats", true);
			SetBool(si, "EmuCore/GS", "SaveFrameStats", true);
			SetInt(si, "EmuCore/GS", "SaveDrawStart", 0);
			SetInt(si, "EmuCore/GS", "SaveDrawCount", 5000);
			SetInt(si, "EmuCore/GS", "SaveDrawBy", 1);
			SetInt(si, "EmuCore/GS", "SaveFrameStart", 0);
			SetInt(si, "EmuCore/GS", "SaveFrameCount", -1);
			SetInt(si, "EmuCore/GS", "SaveFrameBy", 1);
		}

		SetBool(si, "EmuCore/CPU/Recompiler", "EnableEE", false);
		SetBool(si, "EmuCore/CPU/Recompiler", "EnableIOP", false);
		SetBool(si, "EmuCore/CPU/Recompiler", "EnableVU0", false);
		SetBool(si, "EmuCore/CPU/Recompiler", "EnableVU1", false);
		SetBool(si, "EmuCore/CPU/Recompiler", "EnableFastmem", false);
		SetBool(si, "EmuCore/CPU/Recompiler", "EnableEECache", false);

		si.ClearSection("Hotkeys");
		const Pad::ControllerInfo* disconnected_pad_info = Pad::GetControllerInfo(Pad::ControllerType::NotConnected);
		const Pad::ControllerInfo* ds2_pad_info = Pad::GetControllerInfo(Pad::ControllerType::DualShock2);
		const char* disconnected_pad_type = disconnected_pad_info ? disconnected_pad_info->name : "None";
		const char* ds2_pad_type = ds2_pad_info ? ds2_pad_info->name : "DualShock2";
		for (u32 i = 0; i < Pad::NUM_CONTROLLER_PORTS; i++)
		{
			const std::string section = Pad::GetConfigSection(i);
			si.SetStringValue(section.c_str(), "Type",
				(options.pad_pulse_script && i == 0) ? ds2_pad_type : disconnected_pad_type);
		}

		SetInt(si, "EmuCore/Speedhacks", "EECycleRate", 0);
		SetInt(si, "EmuCore/Speedhacks", "EECycleSkip", 0);
		SetBool(si, "EmuCore/Speedhacks", "fastCDVD", false);
		SetBool(si, "EmuCore/Speedhacks", "IntcStat", false);
		SetBool(si, "EmuCore/Speedhacks", "WaitLoop", false);
		SetBool(si, "EmuCore/Speedhacks", "vuFlagHack", false);
		SetBool(si, "EmuCore/Speedhacks", "vuThread", false);
		SetBool(si, "EmuCore/Speedhacks", "vu1Instant", false);

		SetBool(si, "Achievements", "Enabled", false);
		si.SetStringValue("SPU2/Output", "Backend", "Null");
		si.SetStringValue("SPU2/Output", "SyncMode", "Disabled");
	}

	bool InitializeConfig(const TraceOptions& options, Error* error)
	{
		EmuFolders::SetAppRoot();
		EmuFolders::Resources = Path::Canonicalize(Path::Combine(PCSX2_TRACE_SOURCE_ROOT, "bin/resources"));
		if (!FileSystem::DirectoryExists(EmuFolders::Resources.c_str()))
		{
			Error::SetStringFmt(error, "PCSX2 resources directory '{}' does not exist.", EmuFolders::Resources);
			return false;
		}

		std::string output_path_for_defaults = options.output_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = options.iop_output_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = options.mem_output_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = options.gs_output_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = options.ipu_output_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = options.core_event_output_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = options.spu2_output_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = options.vif_output_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = options.vu_output_path;
		EmuFolders::DataRoot = options.data_root.empty() ?
			Path::Combine(Path::GetDirectory(output_path_for_defaults), "pcsx2-trace-data") :
			options.data_root;
		EmuFolders::Settings = Path::Combine(EmuFolders::DataRoot, "inis");

		if (!FileSystem::EnsureDirectoryExists(EmuFolders::DataRoot.c_str(), false, error) ||
			!FileSystem::EnsureDirectoryExists(EmuFolders::Settings.c_str(), false, error))
		{
			return false;
		}

		s_base_settings = std::make_unique<MemorySettingsInterface>();
		s_secrets_settings = std::make_unique<MemorySettingsInterface>();
		Host::Internal::SetBaseSettingsLayer(s_base_settings.get());
		Host::Internal::SetSecretsSettingsLayer(s_secrets_settings.get());

		ConfigureDeterministicSettings(*s_base_settings, options);
		EmuFolders::LoadConfig(*s_base_settings);
		return EmuFolders::EnsureFoldersExist();
	}

	bool DumpIopMemory(const TraceOptions& options, Error* error)
	{
		if (options.iop_dump_path.empty())
			return true;

		const std::string dump_directory(Path::GetDirectory(options.iop_dump_path));
		if (!dump_directory.empty() && !FileSystem::EnsureDirectoryExists(dump_directory.c_str(), false, error))
			return false;

		std::unique_ptr<u8[]> dump(new u8[static_cast<size_t>(options.iop_dump_size)]);
		if (!iopMemSafeReadBytes(static_cast<u32>(options.iop_dump_address), dump.get(),
				static_cast<u32>(options.iop_dump_size)))
		{
			Error::SetStringFmt(error, "Failed to read IOP dump span 0x{:08x}+0x{:x}.",
				static_cast<u32>(options.iop_dump_address), static_cast<u32>(options.iop_dump_size));
			return false;
		}

		FILE* file = FileSystem::OpenCFile(options.iop_dump_path.c_str(), "wb");
		if (!file)
		{
			Error::SetStringFmt(error, "Failed to open IOP dump output '{}'.", options.iop_dump_path);
			return false;
		}

		const bool wrote = std::fwrite(dump.get(), static_cast<size_t>(options.iop_dump_size), 1, file) == 1;
		std::fclose(file);
		if (!wrote)
		{
			Error::SetStringFmt(error, "Failed to write IOP dump output '{}'.", options.iop_dump_path);
			return false;
		}

		return true;
	}

	int RunTrace(const TraceOptions& options)
	{
		Error error;
		if (!options.boot_bios_only && !FileSystem::FileExists(options.elf_path.c_str()))
		{
			std::fprintf(stderr, "Boot path does not exist: %s\n", options.elf_path.c_str());
			return 2;
		}

		if (!FileSystem::FileExists(options.bios_path.c_str()) && !FileSystem::DirectoryExists(options.bios_path.c_str()))
		{
			std::fprintf(stderr, "BIOS path does not exist: %s\n", options.bios_path.c_str());
			return 2;
		}

		if (!InitializeConfig(options, &error))
		{
			std::fprintf(stderr, "Failed to initialize headless config: %s\n", error.GetDescription().c_str());
			return 2;
		}

		if (!VMManager::Internal::CPUThreadInitialize())
		{
			std::fprintf(stderr, "Failed to initialize PCSX2 CPU thread.\n");
			VMManager::Internal::CPUThreadShutdown();
			return 2;
		}

		bool ee_trace_started = false;
		bool iop_trace_started = false;
		bool mem_trace_started = false;
		bool gs_trace_started = false;
		bool ipu_trace_started = false;
		bool sif_trace_started = false;
		bool core_event_trace_started = false;
		bool spu2_trace_started = false;
		bool vif_trace_started = false;
		bool vu_trace_started = false;
		if (!options.output_path.empty())
		{
			Pcsx2Trace::EeTraceConfig trace_config;
			trace_config.output_path = options.output_path;
			trace_config.match_trace_path = options.ee_match_trace_path;
			trace_config.max_records = options.ee_match_trace_path.empty() ? options.max_instructions : 0;
			trace_config.max_instruction_records = options.ee_match_trace_path.empty() ? 0 : options.max_instructions;
			trace_config.skip_records = options.ee_skip_records;
			trace_config.after_sif_records = options.ee_after_sif_records;
			trace_config.match_ignore_timing_state = options.ee_match_ignore_timing_state;
			trace_config.match_pc_only = options.ee_match_pc_only;
			trace_config.defer_match_limit_until_mem_trace = options.mem_sample_ee_trace && !options.mem_output_path.empty();
			trace_config.wait_for_elf_entry = options.wait_for_elf_entry;
			if (!Pcsx2Trace::StartEeTrace(trace_config, &error))
			{
				std::fprintf(stderr, "Failed to start EE trace: %s\n", error.GetDescription().c_str());
				VMManager::Internal::CPUThreadShutdown();
				return 2;
			}
			ee_trace_started = true;
		}
		if (!options.iop_output_path.empty())
		{
			Pcsx2Trace::IopTraceConfig trace_config;
			trace_config.output_path = options.iop_output_path;
			trace_config.max_records = options.max_iop_instructions;
			trace_config.skip_records = options.iop_skip_records;
			trace_config.wait_for_elf_entry = options.wait_for_elf_entry;
			if (!Pcsx2Trace::StartIopTrace(trace_config, &error))
			{
				std::fprintf(stderr, "Failed to start IOP trace: %s\n", error.GetDescription().c_str());
				if (ee_trace_started)
					Pcsx2Trace::StopEeTrace();
				VMManager::Internal::CPUThreadShutdown();
				return 2;
			}
			iop_trace_started = true;
		}
		if (!options.mem_output_path.empty())
		{
			Pcsx2Trace::MemTraceConfig trace_config;
			trace_config.output_path = options.mem_output_path;
			trace_config.max_records = options.max_mem_records;
			trace_config.max_instruction_records = options.max_mem_instructions;
			trace_config.skip_records = options.mem_skip_records;
			trace_config.sample_interval = options.mem_hash_interval;
			trace_config.region_mask = options.mem_region_mask;
			trace_config.sample_on_ee_trace_record = options.mem_sample_ee_trace;
			trace_config.wait_for_elf_entry = options.wait_for_elf_entry;
			if (!Pcsx2Trace::StartMemTrace(trace_config, &error))
			{
				std::fprintf(stderr, "Failed to start MEM trace: %s\n", error.GetDescription().c_str());
				if (iop_trace_started)
					Pcsx2Trace::StopIopTrace();
				if (ee_trace_started)
					Pcsx2Trace::StopEeTrace();
				VMManager::Internal::CPUThreadShutdown();
				return 2;
			}
			mem_trace_started = true;
		}
		if (!options.gs_output_path.empty())
		{
			Pcsx2Trace::GsTraceConfig trace_config;
			trace_config.output_path = options.gs_output_path;
			trace_config.max_records = options.max_gs_records;
			trace_config.max_instruction_records = options.max_gs_instructions;
			trace_config.skip_records = options.gs_skip_records;
			trace_config.wait_for_elf_entry = options.wait_for_elf_entry;
			trace_config.state_snapshots = options.gs_state_snapshots;
			trace_config.state_full_dumps = options.gs_state_full_dumps;
			if (!Pcsx2Trace::StartGsTrace(trace_config, &error))
			{
				std::fprintf(stderr, "Failed to start GS trace: %s\n", error.GetDescription().c_str());
				if (mem_trace_started)
					Pcsx2Trace::StopMemTrace();
				if (iop_trace_started)
					Pcsx2Trace::StopIopTrace();
				if (ee_trace_started)
					Pcsx2Trace::StopEeTrace();
				VMManager::Internal::CPUThreadShutdown();
				return 2;
			}
			gs_trace_started = true;
		}
		if (!options.ipu_output_path.empty())
		{
			Pcsx2Trace::IpuTraceConfig trace_config;
			trace_config.output_path = options.ipu_output_path;
			trace_config.max_records = options.max_ipu_records;
			trace_config.skip_records = options.ipu_skip_records;
			trace_config.wait_for_elf_entry = options.wait_for_elf_entry;
			if (!Pcsx2Trace::StartIpuTrace(trace_config, &error))
			{
				std::fprintf(stderr, "Failed to start IPU trace: %s\n", error.GetDescription().c_str());
				if (gs_trace_started)
					Pcsx2Trace::StopGsTrace();
				if (mem_trace_started)
					Pcsx2Trace::StopMemTrace();
				if (iop_trace_started)
					Pcsx2Trace::StopIopTrace();
				if (ee_trace_started)
					Pcsx2Trace::StopEeTrace();
				VMManager::Internal::CPUThreadShutdown();
				return 2;
			}
			ipu_trace_started = true;
		}
		if (!options.sif_output_path.empty())
		{
			Pcsx2Trace::SifTraceConfig trace_config;
			trace_config.output_path = options.sif_output_path;
			trace_config.max_records = options.max_sif_records;
			trace_config.skip_records = options.sif_skip_records;
			trace_config.wait_for_elf_entry = options.wait_for_elf_entry;
			if (!Pcsx2Trace::StartSifTrace(trace_config, &error))
			{
				std::fprintf(stderr, "Failed to start SIF trace: %s\n", error.GetDescription().c_str());
				if (ipu_trace_started)
					Pcsx2Trace::StopIpuTrace();
				if (gs_trace_started)
					Pcsx2Trace::StopGsTrace();
				if (mem_trace_started)
					Pcsx2Trace::StopMemTrace();
				if (iop_trace_started)
					Pcsx2Trace::StopIopTrace();
				if (ee_trace_started)
					Pcsx2Trace::StopEeTrace();
				VMManager::Internal::CPUThreadShutdown();
				return 2;
			}
			sif_trace_started = true;
		}
		if (!options.core_event_output_path.empty())
		{
			Pcsx2Trace::CoreEventTraceConfig trace_config;
			trace_config.output_path = options.core_event_output_path;
			trace_config.max_records = options.max_core_event_records;
			trace_config.skip_records = options.core_event_skip_records;
			trace_config.after_sif_records = options.core_event_after_sif_records;
			trace_config.wait_for_elf_entry = options.wait_for_elf_entry;
			if (!Pcsx2Trace::StartCoreEventTrace(trace_config, &error))
			{
				std::fprintf(stderr, "Failed to start core-event trace: %s\n", error.GetDescription().c_str());
				if (sif_trace_started)
					Pcsx2Trace::StopSifTrace();
				if (ipu_trace_started)
					Pcsx2Trace::StopIpuTrace();
				if (gs_trace_started)
					Pcsx2Trace::StopGsTrace();
				if (mem_trace_started)
					Pcsx2Trace::StopMemTrace();
				if (iop_trace_started)
					Pcsx2Trace::StopIopTrace();
				if (ee_trace_started)
					Pcsx2Trace::StopEeTrace();
				VMManager::Internal::CPUThreadShutdown();
				return 2;
			}
			core_event_trace_started = true;
		}
		if (!options.spu2_output_path.empty())
		{
			Pcsx2Trace::Spu2TraceConfig trace_config;
			trace_config.output_path = options.spu2_output_path;
			trace_config.max_records = options.max_spu2_records;
			trace_config.skip_records = options.spu2_skip_records;
			trace_config.wait_for_elf_entry = options.wait_for_elf_entry;
			if (!Pcsx2Trace::StartSpu2Trace(trace_config, &error))
			{
				std::fprintf(stderr, "Failed to start SPU2 trace: %s\n", error.GetDescription().c_str());
				if (core_event_trace_started)
					Pcsx2Trace::StopCoreEventTrace();
				if (sif_trace_started)
					Pcsx2Trace::StopSifTrace();
				if (ipu_trace_started)
					Pcsx2Trace::StopIpuTrace();
				if (gs_trace_started)
					Pcsx2Trace::StopGsTrace();
				if (mem_trace_started)
					Pcsx2Trace::StopMemTrace();
				if (iop_trace_started)
					Pcsx2Trace::StopIopTrace();
				if (ee_trace_started)
					Pcsx2Trace::StopEeTrace();
				VMManager::Internal::CPUThreadShutdown();
				return 2;
			}
			spu2_trace_started = true;
		}
		if (!options.vif_output_path.empty())
		{
			Pcsx2Trace::VifTraceConfig trace_config;
			trace_config.output_path = options.vif_output_path;
			trace_config.max_records = options.max_vif_records;
			trace_config.skip_records = options.vif_skip_records;
			trace_config.wait_for_elf_entry = options.wait_for_elf_entry;
			if (!Pcsx2Trace::StartVifTrace(trace_config, &error))
			{
				std::fprintf(stderr, "Failed to start VIF trace: %s\n", error.GetDescription().c_str());
				if (spu2_trace_started)
					Pcsx2Trace::StopSpu2Trace();
				if (core_event_trace_started)
					Pcsx2Trace::StopCoreEventTrace();
				if (sif_trace_started)
					Pcsx2Trace::StopSifTrace();
				if (ipu_trace_started)
					Pcsx2Trace::StopIpuTrace();
				if (gs_trace_started)
					Pcsx2Trace::StopGsTrace();
				if (mem_trace_started)
					Pcsx2Trace::StopMemTrace();
				if (iop_trace_started)
					Pcsx2Trace::StopIopTrace();
				if (ee_trace_started)
					Pcsx2Trace::StopEeTrace();
				VMManager::Internal::CPUThreadShutdown();
				return 2;
			}
			vif_trace_started = true;
		}
		if (!options.vu_output_path.empty())
		{
			Pcsx2Trace::VuTraceConfig trace_config;
			trace_config.output_path = options.vu_output_path;
			trace_config.max_records = options.max_vu_records;
			trace_config.max_instruction_records = options.max_vu_instructions;
			trace_config.skip_records = options.vu_skip_records;
			trace_config.unit_mask = options.vu_unit_mask;
			trace_config.wait_for_elf_entry = options.wait_for_elf_entry;
			if (!Pcsx2Trace::StartVuTrace(trace_config, &error))
			{
				std::fprintf(stderr, "Failed to start VU trace: %s\n", error.GetDescription().c_str());
				if (vif_trace_started)
					Pcsx2Trace::StopVifTrace();
				if (spu2_trace_started)
					Pcsx2Trace::StopSpu2Trace();
				if (core_event_trace_started)
					Pcsx2Trace::StopCoreEventTrace();
				if (sif_trace_started)
					Pcsx2Trace::StopSifTrace();
				if (ipu_trace_started)
					Pcsx2Trace::StopIpuTrace();
				if (gs_trace_started)
					Pcsx2Trace::StopGsTrace();
				if (mem_trace_started)
					Pcsx2Trace::StopMemTrace();
				if (iop_trace_started)
					Pcsx2Trace::StopIopTrace();
				if (ee_trace_started)
					Pcsx2Trace::StopEeTrace();
				VMManager::Internal::CPUThreadShutdown();
				return 2;
			}
			vu_trace_started = true;
		}

		VMBootParameters boot;
		if (options.boot_disc)
		{
			boot.filename = options.elf_path;
			boot.elf_override.clear();
			boot.source_type = CDVD_SourceType::Iso;
		}
		else
		{
			boot.filename.clear();
			boot.elf_override = options.boot_bios_only ? std::string() : options.elf_path;
			boot.source_type = CDVD_SourceType::NoDisc;
		}
		boot.fast_boot = options.fast_boot && !options.boot_bios_only;
		boot.fullscreen = false;
		boot.start_unlimited = false;
		boot.disable_achievements_hardcore_mode = true;

		const VMBootResult boot_result = VMManager::Initialize(boot, &error);
		if (boot_result != VMBootResult::StartupSuccess)
		{
			std::fprintf(stderr, "Failed to boot %s: %s\n",
				options.boot_bios_only ? "BIOS" : (options.boot_disc ? "disc" : "ELF"),
				error.GetDescription().c_str());
			if (vu_trace_started)
				Pcsx2Trace::StopVuTrace();
			if (vif_trace_started)
				Pcsx2Trace::StopVifTrace();
			if (spu2_trace_started)
				Pcsx2Trace::StopSpu2Trace();
			if (core_event_trace_started)
				Pcsx2Trace::StopCoreEventTrace();
			if (sif_trace_started)
				Pcsx2Trace::StopSifTrace();
			if (ipu_trace_started)
				Pcsx2Trace::StopIpuTrace();
			if (gs_trace_started)
				Pcsx2Trace::StopGsTrace();
			if (mem_trace_started)
				Pcsx2Trace::StopMemTrace();
			if (iop_trace_started)
				Pcsx2Trace::StopIopTrace();
			if (ee_trace_started)
				Pcsx2Trace::StopEeTrace();
			VMManager::Internal::CPUThreadShutdown();
			return 3;
		}

		VMManager::SetState(VMState::Running);
		s_pad_pulse_script_enabled = options.pad_pulse_script;
		if (options.pad_pulse_script || core_event_trace_started)
			Pcsx2Trace::SetEePreInstructionCallback(UpdatePadPulseScript);
		VMManager::Execute();
		Pcsx2Trace::SetEePreInstructionCallback(nullptr);

		const u64 ee_records = Pcsx2Trace::GetEeTraceRecordsWritten();
		const bool ee_hit_limit = Pcsx2Trace::DidEeTraceHitLimit();
		const std::string ee_trace_error = Pcsx2Trace::GetEeTraceError();
		const u64 iop_records = Pcsx2Trace::GetIopTraceRecordsWritten();
		const bool iop_hit_limit = Pcsx2Trace::DidIopTraceHitLimit();
		const std::string iop_trace_error = Pcsx2Trace::GetIopTraceError();
		const u64 mem_records = Pcsx2Trace::GetMemTraceRecordsWritten();
		const bool mem_hit_limit = Pcsx2Trace::DidMemTraceHitLimit();
		const std::string mem_trace_error = Pcsx2Trace::GetMemTraceError();
		const u64 gs_records = Pcsx2Trace::GetGsTraceRecordsWritten();
		const bool gs_hit_limit = Pcsx2Trace::DidGsTraceHitLimit();
		const std::string gs_trace_error = Pcsx2Trace::GetGsTraceError();
		const u64 ipu_records = Pcsx2Trace::GetIpuTraceRecordsWritten();
		const bool ipu_hit_limit = Pcsx2Trace::DidIpuTraceHitLimit();
		const std::string ipu_trace_error = Pcsx2Trace::GetIpuTraceError();
		const u64 sif_records = Pcsx2Trace::GetSifTraceRecordsWritten();
		const bool sif_hit_limit = Pcsx2Trace::DidSifTraceHitLimit();
		const std::string sif_trace_error = Pcsx2Trace::GetSifTraceError();
		const u64 core_event_records = Pcsx2Trace::GetCoreEventTraceRecordsWritten();
		const bool core_event_hit_limit = Pcsx2Trace::DidCoreEventTraceHitLimit();
		const std::string core_event_trace_error = Pcsx2Trace::GetCoreEventTraceError();
		const u64 spu2_records = Pcsx2Trace::GetSpu2TraceRecordsWritten();
		const bool spu2_hit_limit = Pcsx2Trace::DidSpu2TraceHitLimit();
		const std::string spu2_trace_error = Pcsx2Trace::GetSpu2TraceError();
		const u64 vif_records = Pcsx2Trace::GetVifTraceRecordsWritten();
		const bool vif_hit_limit = Pcsx2Trace::DidVifTraceHitLimit();
		const std::string vif_trace_error = Pcsx2Trace::GetVifTraceError();
		const u64 vu_records = Pcsx2Trace::GetVuTraceRecordsWritten();
		const u64 vu_instructions_seen = Pcsx2Trace::GetVuTraceInstructionRecordsSeen();
		const bool vu_hit_limit = Pcsx2Trace::DidVuTraceHitLimit();
		const std::string vu_trace_error = Pcsx2Trace::GetVuTraceError();
		if (vu_trace_started)
			Pcsx2Trace::StopVuTrace();
		if (vif_trace_started)
			Pcsx2Trace::StopVifTrace();
		if (spu2_trace_started)
			Pcsx2Trace::StopSpu2Trace();
		if (core_event_trace_started)
			Pcsx2Trace::StopCoreEventTrace();
		if (sif_trace_started)
			Pcsx2Trace::StopSifTrace();
		if (ipu_trace_started)
			Pcsx2Trace::StopIpuTrace();
		if (gs_trace_started)
			Pcsx2Trace::StopGsTrace();
		if (mem_trace_started)
			Pcsx2Trace::StopMemTrace();
		if (iop_trace_started)
			Pcsx2Trace::StopIopTrace();
		if (ee_trace_started)
			Pcsx2Trace::StopEeTrace();

		if (!DumpIopMemory(options, &error))
		{
			std::fprintf(stderr, "Failed to dump IOP memory: %s\n", error.GetDescription().c_str());
			VMManager::Shutdown(false);
			VMManager::Internal::CPUThreadShutdown();
			return 4;
		}

		VMManager::Shutdown(false);
		VMManager::Internal::CPUThreadShutdown();

		if (!ee_trace_error.empty())
		{
			std::fprintf(stderr, "EE trace failed after %llu records: %s\n",
				static_cast<unsigned long long>(ee_records), ee_trace_error.c_str());
			return 4;
		}
		if (!iop_trace_error.empty())
		{
			std::fprintf(stderr, "IOP trace failed after %llu records: %s\n",
				static_cast<unsigned long long>(iop_records), iop_trace_error.c_str());
			return 4;
		}
		if (!mem_trace_error.empty())
		{
			std::fprintf(stderr, "MEM trace failed after %llu records: %s\n",
				static_cast<unsigned long long>(mem_records), mem_trace_error.c_str());
			return 4;
		}
		if (!gs_trace_error.empty())
		{
			std::fprintf(stderr, "GS trace failed after %llu records: %s\n",
				static_cast<unsigned long long>(gs_records), gs_trace_error.c_str());
			return 4;
		}
		if (!ipu_trace_error.empty())
		{
			std::fprintf(stderr, "IPU trace failed after %llu records: %s\n",
				static_cast<unsigned long long>(ipu_records), ipu_trace_error.c_str());
			return 4;
		}
		if (!sif_trace_error.empty())
		{
			std::fprintf(stderr, "SIF trace failed after %llu records: %s\n",
				static_cast<unsigned long long>(sif_records), sif_trace_error.c_str());
			return 4;
		}
		if (!core_event_trace_error.empty())
		{
			std::fprintf(stderr, "Core-event trace failed after %llu records: %s\n",
				static_cast<unsigned long long>(core_event_records), core_event_trace_error.c_str());
			return 4;
		}
		if (!spu2_trace_error.empty())
		{
			std::fprintf(stderr, "SPU2 trace failed after %llu records: %s\n",
				static_cast<unsigned long long>(spu2_records), spu2_trace_error.c_str());
			return 4;
		}
		if (!vif_trace_error.empty())
		{
			std::fprintf(stderr, "VIF trace failed after %llu records: %s\n",
				static_cast<unsigned long long>(vif_records), vif_trace_error.c_str());
			return 4;
		}
		if (!vu_trace_error.empty())
		{
			std::fprintf(stderr, "VU trace failed after %llu records: %s\n",
				static_cast<unsigned long long>(vu_records), vu_trace_error.c_str());
			return 4;
		}

		if (ee_trace_started)
		{
			std::fprintf(stdout, "wrote %llu EE pre-instruction records to %s%s\n",
				static_cast<unsigned long long>(ee_records), options.output_path.c_str(),
				ee_hit_limit ? " (hit limit)" : "");
		}
		if (iop_trace_started)
		{
			std::fprintf(stdout, "wrote %llu IOP pre-instruction records to %s%s\n",
				static_cast<unsigned long long>(iop_records), options.iop_output_path.c_str(),
				iop_hit_limit ? " (hit limit)" : "");
		}
		if (mem_trace_started)
		{
			std::fprintf(stdout, "wrote %llu MEM memory hash records to %s%s\n",
				static_cast<unsigned long long>(mem_records), options.mem_output_path.c_str(),
				mem_hit_limit ? " (hit limit)" : "");
		}
		if (gs_trace_started)
		{
			std::fprintf(stdout, "wrote %llu GS decoded records to %s%s\n",
				static_cast<unsigned long long>(gs_records), options.gs_output_path.c_str(),
				gs_hit_limit ? " (hit limit)" : "");
		}
		if (ipu_trace_started)
		{
			std::fprintf(stdout, "wrote %llu IPU command/output records to %s%s\n",
				static_cast<unsigned long long>(ipu_records), options.ipu_output_path.c_str(),
				ipu_hit_limit ? " (hit limit)" : "");
		}
		if (sif_trace_started)
		{
			std::fprintf(stdout, "wrote %llu SIF FIFO transfer records to %s%s\n",
				static_cast<unsigned long long>(sif_records), options.sif_output_path.c_str(),
				sif_hit_limit ? " (hit limit)" : "");
		}
		if (core_event_trace_started)
		{
			std::fprintf(stdout, "wrote %llu core-event records to %s%s\n",
				static_cast<unsigned long long>(core_event_records), options.core_event_output_path.c_str(),
				core_event_hit_limit ? " (hit limit)" : "");
		}
		if (spu2_trace_started)
		{
			std::fprintf(stdout, "wrote %llu SPU2 mixer records to %s%s\n",
				static_cast<unsigned long long>(spu2_records), options.spu2_output_path.c_str(),
				spu2_hit_limit ? " (hit limit)" : "");
		}
		if (vif_trace_started)
		{
			std::fprintf(stdout, "wrote %llu VIF command/unpack records to %s%s\n",
				static_cast<unsigned long long>(vif_records), options.vif_output_path.c_str(),
				vif_hit_limit ? " (hit limit)" : "");
		}
		if (vu_trace_started)
		{
			std::fprintf(stdout, "wrote %llu VU interpreter records after %llu EE instructions to %s%s\n",
				static_cast<unsigned long long>(vu_records),
				static_cast<unsigned long long>(vu_instructions_seen), options.vu_output_path.c_str(),
				vu_hit_limit ? " (hit limit)" : "");
		}
		if (!options.iop_dump_path.empty())
		{
			std::fprintf(stdout, "dumped %llu IOP bytes at 0x%08llx to %s\n",
				static_cast<unsigned long long>(options.iop_dump_size),
				static_cast<unsigned long long>(options.iop_dump_address),
				options.iop_dump_path.c_str());
		}
		return (ee_hit_limit || iop_hit_limit || mem_hit_limit || gs_hit_limit || ipu_hit_limit ||
			sif_hit_limit || core_event_hit_limit || spu2_hit_limit || vif_hit_limit || vu_hit_limit) ? 0 : 1;
	}
} // namespace

int main(int argc, char** argv)
{
	TraceOptions options;
	if (!ParseCommandLine(argc, argv, &options))
		return 2;
	if (options.show_help)
		return 0;

	return RunTrace(options);
}
