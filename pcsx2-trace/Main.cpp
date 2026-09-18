// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "pcsx2/CDVD/CDVDcommon.h"
#include "pcsx2/Config.h"
#include "pcsx2/Counters.h"
#include "pcsx2/DebugTools/CoreEventTrace.h"
#include "pcsx2/DebugTools/EeTrace.h"
#include "pcsx2/DebugTools/GsTrace.h"
#include "pcsx2/DebugTools/IopTrace.h"
#include "pcsx2/DebugTools/IpuTrace.h"
#include "pcsx2/DebugTools/MachineCheckpointTrace.h"
#include "pcsx2/DebugTools/MemTrace.h"
#include "pcsx2/DebugTools/SifTrace.h"
#include "pcsx2/DebugTools/Spu2Trace.h"
#include "pcsx2/DebugTools/VifTrace.h"
#include "pcsx2/DebugTools/VuTrace.h"
#include "pcsx2/Host.h"
#include "pcsx2/IopMem.h"
#include "pcsx2/R3000A.h"
#include "pcsx2/R5900.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/SIO/Pad/PadDualshock2.h"
#include "pcsx2/SaveState.h"
#include "pcsx2/SaveStateRaw.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/VUmicro.h"

#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"

#include <array>
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
	static constexpr u32 DEFAULT_PAD_AUTO_FIRE_PRESSED_FRAMES = 2;
	static constexpr u32 DEFAULT_PAD_AUTO_FIRE_RELEASED_FRAMES = 6;
	static constexpr u32 MAX_PAD_AUTO_FIRE_CADENCE_FRAMES = 600;

	enum class PadAutoFireButton : u8
	{
		None,
		Cross,
		Circle,
	};

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
		std::string machine_checkpoint_output_path;
		std::string machine_checkpoint_diagnostic_directory;
		std::string sif_output_path;
		std::string core_event_output_path;
		std::string spu2_output_path;
		std::string vif_output_path;
		std::string vu_output_path;
		std::string pcsx2_state_input_path;
		std::string replay_state_input_path;
		std::string replay_state_output_path;
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
		u64 max_machine_checkpoint_records = 1;
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
		u64 machine_checkpoint_skip_records = 0;
		u64 machine_checkpoint_after_sif_records = 0;
		u64 machine_checkpoint_after_vif_records = 0;
		u64 machine_checkpoint_after_vsync_frames = 0;
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
		u32 pad_auto_fire_pressed_frames = DEFAULT_PAD_AUTO_FIRE_PRESSED_FRAMES;
		u32 pad_auto_fire_released_frames = DEFAULT_PAD_AUTO_FIRE_RELEASED_FRAMES;
		PadAutoFireButton pad_auto_fire_button = PadAutoFireButton::None;
		bool wait_for_elf_entry = true;
		bool ee_entry_state = false;
		bool ee_vu0_state = false;
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
		bool pad1_dualshock2 = false;
		bool pad_pulse_script = false;
		bool recompiler_ee = false;
		bool recompiler_iop = false;
		bool recompiler_vu = false;
		bool replay_state_checkpoint_start = false;
		bool stop_after_sif_limit = false;
	};

	std::unique_ptr<MemorySettingsInterface> s_base_settings;
	std::unique_ptr<MemorySettingsInterface> s_secrets_settings;
	u64 s_pad_script_entry_cycle = 0;
	u32 s_pad_script_state = 0;
	bool s_pad_pulse_script_enabled = false;
	PadAutoFireButton s_pad_auto_fire_button = PadAutoFireButton::None;
	u32 s_pad_auto_fire_pressed_frames = DEFAULT_PAD_AUTO_FIRE_PRESSED_FRAMES;
	u32 s_pad_auto_fire_released_frames = DEFAULT_PAD_AUTO_FIRE_RELEASED_FRAMES;
	u32 s_pad_auto_fire_entry_frame = 0;
	u32 s_pad_auto_fire_last_frame = 0;
	u32 s_pad_auto_fire_press_edges = 0;
	u32 s_pad_auto_fire_release_edges = 0;
	bool s_pad_auto_fire_initialized = false;
	bool s_pad_auto_fire_pressed = false;
	bool s_stop_after_sif_limit = false;
	bool s_machine_checkpoint_trace_enabled = false;

	void UpdatePadPulseScript()
	{
		// A requested machine checkpoint is the stronger terminal condition. Its
		// completed-VU seam can occur after bounded CORE/SIF diagnostics fill, so
		// preserve execution until the checkpoint itself is recorded.
		if (s_machine_checkpoint_trace_enabled)
		{
			if (Pcsx2Trace::DidMachineCheckpointTraceHitLimit())
			{
				Cpu->ExitExecution();
				return;
			}
		}
		else if (Pcsx2Trace::DidCoreEventTraceHitLimit() ||
			(s_stop_after_sif_limit && Pcsx2Trace::DidSifTraceHitLimit()))
		{
			Cpu->ExitExecution();
			return;
		}

		if (!VMManager::Internal::HasBootedELF())
			return;

		if (s_pad_pulse_script_enabled)
		{
			if (s_pad_script_entry_cycle == 0)
				s_pad_script_entry_cycle = cpuRegs.cycle;

			static constexpr u64 EE_CYCLES_PER_SECOND = 294912000;
			static constexpr u64 PERIOD = EE_CYCLES_PER_SECOND * 2;
			static constexpr u64 PULSE = EE_CYCLES_PER_SECOND / 5;
			const u64 elapsed = cpuRegs.cycle - s_pad_script_entry_cycle;
			const u64 event = elapsed / PERIOD;
			const bool pressed = (elapsed % PERIOD) < PULSE;
			const u32 state = pressed ? ((event & 1) ? 2u : 1u) : 0u;
			if (state != s_pad_script_state)
			{
				s_pad_script_state = state;
				Pad::SetControllerState(0, PadDualshock2::Inputs::PAD_START, state == 1 ? 1.0f : 0.0f);
				Pad::SetControllerState(0, PadDualshock2::Inputs::PAD_CROSS, state == 2 ? 1.0f : 0.0f);
			}
		}

		if (s_pad_auto_fire_button == PadAutoFireButton::None)
			return;
		if (!s_pad_auto_fire_initialized)
		{
			s_pad_auto_fire_entry_frame = g_FrameCount;
			s_pad_auto_fire_last_frame = g_FrameCount;
			s_pad_auto_fire_initialized = true;
		}
		else if (s_pad_auto_fire_last_frame == g_FrameCount)
		{
			return;
		}
		else
		{
			s_pad_auto_fire_last_frame = g_FrameCount;
		}

		const u32 period =
			s_pad_auto_fire_pressed_frames + s_pad_auto_fire_released_frames;
		const u32 phase = (g_FrameCount - s_pad_auto_fire_entry_frame) % period;
		const bool pressed = phase < s_pad_auto_fire_pressed_frames;
		if (pressed == s_pad_auto_fire_pressed &&
			(s_pad_auto_fire_press_edges != 0 || s_pad_auto_fire_release_edges != 0))
		{
			return;
		}

		s_pad_auto_fire_pressed = pressed;
		const u32 input = s_pad_auto_fire_button == PadAutoFireButton::Cross ?
			PadDualshock2::Inputs::PAD_CROSS : PadDualshock2::Inputs::PAD_CIRCLE;
		Pad::SetControllerState(0, input, pressed ? 1.0f : 0.0f);
		if (pressed)
			s_pad_auto_fire_press_edges++;
		else
			s_pad_auto_fire_release_edges++;
	}

	void PrintUsage(const char* program)
	{
		std::fprintf(stderr,
			"usage: %s <bios-file-or-dir> [elf-or-disc] (--out trace.bin | --iop-out trace.bin | --mem-out trace.bin | --gs-out trace.bin | --ipu-out trace.bin | --machine-checkpoint-out trace.bin | --sif-out trace.bin | --core-event-out trace.bin | --spu2-out trace.bin | --vif-out trace.bin | --vu-out trace.bin) [options]\n"
			"\n"
			"options:\n"
			"  --boot-bios           Boot the BIOS with no disc and no ELF fast-boot override.\n"
			"  --boot-disc           Boot the positional path as an ISO/disc image.\n"
			"  --full-boot           Disable PCSX2 fast boot for --boot-disc.\n"
			"  --out trace.bin       Write an EE/R5900 pre-instruction trace.\n"
			"  --ee-entry-state      Write one complete EE trace-schema state record at ELF entry; valid with recompilers.\n"
			"  --ee-vu0-state        Add byte-exact VU0 architectural state to each EE trace record.\n"
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
			"  --machine-checkpoint-out trace.bin\n"
			"                         Write quiescent whole-machine checkpoints after VU1 completion.\n"
			"  --machine-checkpoint-diagnostic-dir DIR\n"
			"                         Write bounded raw EE/IOP RAM and projection-detail checkpoint diagnostics.\n"
			"  --sif-out trace.bin   Write SIF0/SIF1 FIFO data/tag transfer records.\n"
			"  --core-event-out trace.bin\n"
			"                         Write EE/IOP scheduler and device-event records.\n"
			"  --spu2-out trace.bin  Write SPU2 48 kHz mixer output records.\n"
			"  --vif-out trace.bin   Write VIF command and unpack effect records.\n"
			"  --vu-out trace.bin    Write VU0/VU1 interpreter micro-step records.\n"
			"  --pcsx2-state-in state.p2s\n"
			"                         Load an ordinary same-build PCSX2 savestate during VM initialization.\n"
			"  --replay-state-in state.pcsx2raw\n"
			"                         Load a validated PCSX2 named-entry state after VM initialization.\n"
			"  --replay-state-out state.pcsx2raw\n"
			"                         Save a validated named-entry state at the terminal machine checkpoint.\n"
			"  --replay-state-checkpoint-start\n"
			"                         Record the loaded state before the first guest instruction.\n"
			"  --pad1-dualshock2     Keep a neutral DualShock 2 connected in port 1.\n"
			"  --pad-pulse-script    Alternate deterministic EE-cycle START/CROSS pulses after ELF entry.\n"
			"  --pad-autofire BUTTON Repeatedly press and release Cross or Circle after ELF entry.\n"
			"  --pad-autofire-pressed-frames N\n"
			"                         Keep each autofire press active for N guest frames (default: 2).\n"
			"  --pad-autofire-released-frames N\n"
			"                         Keep each autofire release active for N guest frames (default: 6).\n"
			"  --recompiler-ee       Run the native EE recompiler (CORE/SIF traces do not require EE instruction hooks).\n"
			"  --recompiler-iop      Run the native IOP recompiler.\n"
			"  --recompiler-vu       Run the native microVU0 and microVU1 recompilers with MTVU disabled.\n"
			"  --stop-after-sif-limit\n"
			"                         Stop safely after the scheduler containing the final bounded SIF record.\n"
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
			"  --machine-checkpoint-max N\n"
			"                         Stop after N machine checkpoints (default: 1; zero is unlimited).\n"
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
			"  --machine-checkpoint-skip N\n"
			"                         Skip N eligible VU1 completions before checkpointing.\n"
			"  --machine-checkpoint-after-sif N\n"
			"                         Gate checkpoints until N observed SIF records.\n"
			"  --machine-checkpoint-after-vif N\n"
			"                         Gate checkpoints until N observed VIF records.\n"
			"  --machine-checkpoint-after-vsync N\n"
			"                         Gate checkpoints until N completed VSync frames since trace start (normally ELF entry).\n"
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

	bool ParsePadAutoFireButton(std::string_view text, PadAutoFireButton* button)
	{
		if (MemRegionNameMatches(text, "cross"))
			*button = PadAutoFireButton::Cross;
		else if (MemRegionNameMatches(text, "circle"))
			*button = PadAutoFireButton::Circle;
		else
			return false;
		return true;
	}

	const char* PadAutoFireButtonName(PadAutoFireButton button)
	{
		switch (button)
		{
			case PadAutoFireButton::Cross:
				return "Cross";
			case PadAutoFireButton::Circle:
				return "Circle";
			default:
				return "None";
		}
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
			else if (arg == "--ee-entry-state")
			{
				options->ee_entry_state = true;
			}
			else if (arg == "--ee-vu0-state")
			{
				options->ee_vu0_state = true;
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
			else if (arg == "--machine-checkpoint-out")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--machine-checkpoint-out requires a path.\n");
					return false;
				}
				options->machine_checkpoint_output_path = argv[i];
			}
			else if (arg == "--machine-checkpoint-diagnostic-dir")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--machine-checkpoint-diagnostic-dir requires a path.\n");
					return false;
				}
				options->machine_checkpoint_diagnostic_directory = argv[i];
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
			else if (arg == "--pcsx2-state-in")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--pcsx2-state-in requires a path.\n");
					return false;
				}
				options->pcsx2_state_input_path = argv[i];
			}
			else if (arg == "--replay-state-in")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--replay-state-in requires a path.\n");
					return false;
				}
				options->replay_state_input_path = argv[i];
			}
			else if (arg == "--replay-state-out")
			{
				if (++i >= argc)
				{
					std::fprintf(stderr, "--replay-state-out requires a path.\n");
					return false;
				}
				options->replay_state_output_path = argv[i];
			}
			else if (arg == "--replay-state-checkpoint-start")
			{
				options->replay_state_checkpoint_start = true;
			}
			else if (arg == "--pad1-dualshock2")
			{
				options->pad1_dualshock2 = true;
			}
			else if (arg == "--pad-pulse-script")
			{
				options->pad_pulse_script = true;
			}
			else if (arg == "--pad-autofire")
			{
				if (++i >= argc ||
					!ParsePadAutoFireButton(argv[i], &options->pad_auto_fire_button))
				{
					std::fprintf(stderr, "--pad-autofire requires Cross or Circle.\n");
					return false;
				}
			}
			else if (arg == "--pad-autofire-pressed-frames")
			{
				u64 value = 0;
				if (++i >= argc || !ParseU64(argv[i], &value) || value == 0 ||
					value > MAX_PAD_AUTO_FIRE_CADENCE_FRAMES)
				{
					std::fprintf(stderr,
						"--pad-autofire-pressed-frames requires an integer from 1 to %u.\n",
						MAX_PAD_AUTO_FIRE_CADENCE_FRAMES);
					return false;
				}
				options->pad_auto_fire_pressed_frames = static_cast<u32>(value);
			}
			else if (arg == "--pad-autofire-released-frames")
			{
				u64 value = 0;
				if (++i >= argc || !ParseU64(argv[i], &value) || value == 0 ||
					value > MAX_PAD_AUTO_FIRE_CADENCE_FRAMES)
				{
					std::fprintf(stderr,
						"--pad-autofire-released-frames requires an integer from 1 to %u.\n",
						MAX_PAD_AUTO_FIRE_CADENCE_FRAMES);
					return false;
				}
				options->pad_auto_fire_released_frames = static_cast<u32>(value);
			}
			else if (arg == "--recompiler-ee")
			{
				options->recompiler_ee = true;
			}
			else if (arg == "--recompiler-iop")
			{
				options->recompiler_iop = true;
			}
			else if (arg == "--recompiler-vu")
			{
				options->recompiler_vu = true;
			}
			else if (arg == "--stop-after-sif-limit")
			{
				options->stop_after_sif_limit = true;
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
			else if (arg == "--machine-checkpoint-max")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->max_machine_checkpoint_records))
				{
					std::fprintf(stderr, "--machine-checkpoint-max requires an integer.\n");
					return false;
				}
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
			else if (arg == "--machine-checkpoint-skip")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->machine_checkpoint_skip_records))
				{
					std::fprintf(stderr, "--machine-checkpoint-skip requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--machine-checkpoint-after-sif")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->machine_checkpoint_after_sif_records))
				{
					std::fprintf(stderr, "--machine-checkpoint-after-sif requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--machine-checkpoint-after-vif")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->machine_checkpoint_after_vif_records))
				{
					std::fprintf(stderr, "--machine-checkpoint-after-vif requires an integer.\n");
					return false;
				}
			}
			else if (arg == "--machine-checkpoint-after-vsync")
			{
				if (++i >= argc || !ParseU64(argv[i], &options->machine_checkpoint_after_vsync_frames))
				{
					std::fprintf(stderr, "--machine-checkpoint-after-vsync requires an integer.\n");
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
		if (options->pad_pulse_script &&
			options->pad_auto_fire_button != PadAutoFireButton::None)
		{
			std::fprintf(stderr,
				"--pad-pulse-script and --pad-autofire are mutually exclusive.\n");
			return false;
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
				options->ipu_output_path.empty() && options->machine_checkpoint_output_path.empty() &&
				options->sif_output_path.empty() &&
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
		if (options->ee_vu0_state && options->output_path.empty())
		{
			std::fprintf(stderr, "--ee-vu0-state requires --out.\n");
			return false;
		}
		if (options->ee_match_pc_only && options->ee_match_trace_path.empty())
		{
			std::fprintf(stderr, "--ee-match-pc-only requires --ee-match-trace.\n");
			return false;
		}
		if (options->ee_entry_state &&
			(options->output_path.empty() || !options->wait_for_elf_entry ||
			 options->max_instructions != 1 || options->ee_skip_records != 0 ||
			 options->ee_after_sif_records != 0 ||
			 !options->ee_match_trace_path.empty() || options->mem_sample_ee_trace))
		{
			std::fprintf(stderr,
				"--ee-entry-state requires --out, --trace-from entry, --max-instructions 1, and no EE matching, skipping, SIF gate, or sampled MEM trace.\n");
			return false;
		}
		if (options->ee_entry_state &&
			(!options->pcsx2_state_input_path.empty() ||
			 !options->replay_state_input_path.empty()))
		{
			std::fprintf(stderr,
				"--ee-entry-state cannot be used with a restored-state input; state restore does not cross the ELF-entry owner seam.\n");
			return false;
		}

		if (options->mem_sample_ee_trace &&
			(options->ee_match_trace_path.empty() || options->output_path.empty() || options->mem_output_path.empty()))
		{
			std::fprintf(stderr, "--mem-sample-ee-trace requires --ee-match-trace, --out, and --mem-out.\n");
			return false;
		}
		if (options->recompiler_ee &&
			((!options->output_path.empty() && !options->ee_entry_state) ||
			 !options->ee_match_trace_path.empty() ||
			 !options->mem_output_path.empty() || options->mem_sample_ee_trace ||
			 options->ee_after_sif_records != 0))
		{
			std::fprintf(stderr,
				"--recompiler-ee is incompatible with EE pre-instruction trace modes; use --ee-entry-state, CORE/SIF, or another backend-neutral trace.\n");
			return false;
		}
		if (options->recompiler_vu && !options->vu_output_path.empty())
		{
			std::fprintf(stderr,
				"--recompiler-vu is incompatible with the interpreter micro-step VU trace.\n");
			return false;
		}
		if (options->recompiler_iop && !options->iop_output_path.empty())
		{
			std::fprintf(stderr,
				"--recompiler-iop is incompatible with the IOP pre-instruction trace.\n");
			return false;
		}

		if (options->machine_checkpoint_after_sif_records != 0 && options->sif_output_path.empty())
		{
			std::fprintf(stderr, "--machine-checkpoint-after-sif requires --sif-out.\n");
			return false;
		}
		if (!options->replay_state_output_path.empty() &&
			options->machine_checkpoint_output_path.empty())
		{
			std::fprintf(stderr,
				"--replay-state-out requires --machine-checkpoint-out so capture occurs at a quiescent PCSX2-owned seam.\n");
			return false;
		}
		if (!options->replay_state_output_path.empty() &&
			options->max_machine_checkpoint_records == 0)
		{
			std::fprintf(stderr,
				"--replay-state-out requires nonzero --machine-checkpoint-max.\n");
			return false;
		}
		if (!options->pcsx2_state_input_path.empty() &&
			!options->replay_state_input_path.empty())
		{
			std::fprintf(stderr,
				"--pcsx2-state-in and --replay-state-in are mutually exclusive.\n");
			return false;
		}
		if (!options->pcsx2_state_input_path.empty() &&
			!FileSystem::FileExists(options->pcsx2_state_input_path.c_str()))
		{
			std::fprintf(stderr, "PCSX2 savestate does not exist: %s\n",
				options->pcsx2_state_input_path.c_str());
			return false;
		}
		if (!options->replay_state_input_path.empty() &&
			!FileSystem::FileExists(options->replay_state_input_path.c_str()))
		{
			std::fprintf(stderr, "Replay state does not exist: %s\n",
				options->replay_state_input_path.c_str());
			return false;
		}
		if ((!options->pcsx2_state_input_path.empty() ||
			 !options->replay_state_input_path.empty()) &&
			!options->wait_for_elf_entry)
		{
			std::fprintf(stderr,
				"Restored-state input requires --trace-from entry so initialization cannot enter the trace.\n");
			return false;
		}
		if (options->replay_state_checkpoint_start &&
			((options->pcsx2_state_input_path.empty() &&
			  options->replay_state_input_path.empty()) ||
			 options->machine_checkpoint_output_path.empty()))
		{
			std::fprintf(stderr,
				"--replay-state-checkpoint-start requires --pcsx2-state-in or --replay-state-in, and --machine-checkpoint-out.\n");
			return false;
		}
		if (!options->replay_state_input_path.empty() &&
			!options->replay_state_output_path.empty() &&
			Path::Canonicalize(options->replay_state_input_path) ==
				Path::Canonicalize(options->replay_state_output_path))
		{
			std::fprintf(stderr, "Replay state input and output paths must be distinct.\n");
			return false;
		}
		const auto normalize_path = [](const std::string& path) {
			if (path.empty())
				return std::string();
			return Path::Canonicalize(Path::IsAbsolute(path) ? path :
				Path::Combine(FileSystem::GetWorkingDirectory(), path));
		};
		const std::array<const std::string*, 12> writable_artifacts = {{
			&options->output_path, &options->iop_output_path,
			&options->mem_output_path, &options->gs_output_path,
			&options->ipu_output_path, &options->machine_checkpoint_output_path,
			&options->sif_output_path, &options->core_event_output_path,
			&options->spu2_output_path, &options->vif_output_path,
			&options->vu_output_path, &options->iop_dump_path,
		}};
		const std::string replay_input_normalized =
			normalize_path(options->replay_state_input_path);
		const std::string pcsx2_input_normalized =
			normalize_path(options->pcsx2_state_input_path);
		const std::string replay_output_normalized =
			normalize_path(options->replay_state_output_path);
		if (!replay_input_normalized.empty() &&
			replay_input_normalized == replay_output_normalized)
		{
			std::fprintf(stderr, "Replay state input and output paths must be distinct.\n");
			return false;
		}
		for (const std::string* artifact : writable_artifacts)
		{
			if (!pcsx2_input_normalized.empty() && !artifact->empty() &&
				pcsx2_input_normalized == normalize_path(*artifact))
			{
				std::fprintf(stderr,
					"PCSX2 savestate input aliases a writable trace artifact: %s\n",
					artifact->c_str());
				return false;
			}
			if (!replay_input_normalized.empty() && !artifact->empty() &&
				replay_input_normalized == normalize_path(*artifact))
			{
				std::fprintf(stderr,
					"Replay state input aliases a writable trace artifact: %s\n",
					artifact->c_str());
				return false;
			}
			if (!replay_output_normalized.empty() && !artifact->empty() &&
				replay_output_normalized == normalize_path(*artifact))
			{
				std::fprintf(stderr,
					"Replay state output aliases another writable artifact: %s\n",
					artifact->c_str());
				return false;
			}
		}
		for (const std::string* protected_input :
			std::array<const std::string*, 4>{{
				&options->bios_path, &options->elf_path, &options->ee_match_trace_path,
				&options->pcsx2_state_input_path}})
		{
			if (!replay_output_normalized.empty() && !protected_input->empty() &&
				replay_output_normalized == normalize_path(*protected_input))
			{
				std::fprintf(stderr,
					"Replay state output aliases a required input: %s\n",
					protected_input->c_str());
				return false;
			}
		}
		if (!options->machine_checkpoint_diagnostic_directory.empty() &&
			options->machine_checkpoint_output_path.empty())
		{
			std::fprintf(stderr,
				"--machine-checkpoint-diagnostic-dir requires --machine-checkpoint-out.\n");
			return false;
		}
		if (!options->machine_checkpoint_diagnostic_directory.empty() &&
			options->max_machine_checkpoint_records == 0)
		{
			std::fprintf(stderr,
				"--machine-checkpoint-diagnostic-dir requires nonzero --machine-checkpoint-max.\n");
			return false;
		}
		if (options->machine_checkpoint_after_vif_records != 0 && options->vif_output_path.empty())
		{
			std::fprintf(stderr, "--machine-checkpoint-after-vif requires --vif-out.\n");
			return false;
		}
		if (options->machine_checkpoint_after_sif_records != 0 && options->sif_skip_records != 0)
		{
			std::fprintf(stderr,
				"--machine-checkpoint-after-sif requires --sif-skip-records 0.\n");
			return false;
		}
		if (options->machine_checkpoint_after_vif_records != 0 && options->vif_skip_records != 0)
		{
			std::fprintf(stderr,
				"--machine-checkpoint-after-vif requires --vif-skip-records 0.\n");
			return false;
		}
		if (options->machine_checkpoint_after_sif_records != 0 && options->max_sif_records != 0 &&
			options->max_sif_records < options->machine_checkpoint_after_sif_records)
		{
			std::fprintf(stderr,
				"--max-sif-records must be zero or reach --machine-checkpoint-after-sif.\n");
			return false;
		}
		if (options->machine_checkpoint_after_vif_records != 0 && options->max_vif_records != 0 &&
			options->max_vif_records < options->machine_checkpoint_after_vif_records)
		{
			std::fprintf(stderr,
				"--max-vif-records must be zero or reach --machine-checkpoint-after-vif.\n");
			return false;
		}

		if (options->core_event_after_sif_records != 0 && options->sif_output_path.empty())
		{
			std::fprintf(stderr, "--core-event-after-sif-records requires --sif-out.\n");
			return false;
		}
		if (options->stop_after_sif_limit &&
			(options->sif_output_path.empty() || options->max_sif_records == 0))
		{
			std::fprintf(stderr,
				"--stop-after-sif-limit requires --sif-out and a nonzero --max-sif-records.\n");
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
		SetBool(si, "DEV9/Eth", "EthEnable", false);
		SetBool(si, "DEV9/Hdd", "HddEnable", false);
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

		SetBool(si, "EmuCore/CPU/Recompiler", "EnableEE", options.recompiler_ee);
		SetBool(si, "EmuCore/CPU/Recompiler", "EnableIOP", options.recompiler_iop);
		SetBool(si, "EmuCore/CPU/Recompiler", "EnableVU0", options.recompiler_vu);
		SetBool(si, "EmuCore/CPU/Recompiler", "EnableVU1", options.recompiler_vu);
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
				((options.pad1_dualshock2 || options.pad_pulse_script ||
					options.pad_auto_fire_button != PadAutoFireButton::None) &&
					i == 0) ?
					ds2_pad_type : disconnected_pad_type);
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
			output_path_for_defaults = options.machine_checkpoint_output_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = options.core_event_output_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = options.spu2_output_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = options.vif_output_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = options.vu_output_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = !options.replay_state_output_path.empty() ?
				options.replay_state_output_path : options.replay_state_input_path;
		if (output_path_for_defaults.empty())
			output_path_for_defaults = options.pcsx2_state_input_path;
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

	void NotifyRestoredStateTraceStart();

	bool BeginPcsx2StateContinuation(const TraceOptions& options, Error* error)
	{
		if (options.pcsx2_state_input_path.empty())
			return true;

		// VMManager::Initialize() has already restored the native state through
		// VMBootParameters::save_state. Begin the portable-export observation
		// window before one guest instruction can execute, then arm traces at the
		// restored architectural PC because the ELF-entry hook will not recur.
		if (EmuConfig.DEV9.EthEnable || EmuConfig.DEV9.HddEnable)
		{
			Error::SetString(error,
				"PCSX2 savestate staging requires DEV9 Ethernet and HDD disabled.");
			return false;
		}
		Pcsx2Trace::BeginPortableReplayExternalDeviceAccessWindow();
		NotifyRestoredStateTraceStart();
		if (options.replay_state_checkpoint_start &&
			!Pcsx2Trace::RecordMachineCheckpointAtReplayStart())
		{
			Error::SetString(error, Pcsx2Trace::GetMachineCheckpointTraceError());
			return false;
		}
		return true;
	}

	bool LoadReplayState(const TraceOptions& options, Error* error)
	{
		if (options.replay_state_input_path.empty())
			return true;

		const std::optional<std::vector<u8>> raw_bytes =
			FileSystem::ReadBinaryFile(options.replay_state_input_path.c_str());
		if (!raw_bytes.has_value())
		{
			Error::SetStringFmt(error, "Failed to read replay state '{}'.",
				options.replay_state_input_path);
			return false;
		}

		std::unique_ptr<ArchiveEntryList> entries =
			SaveStateRaw::Decode(raw_bytes.value(), error);
		if (!entries)
			return false;

		const PortableStateLoadResult load_result =
			SaveState_LoadPortableState(*entries, error,
				options.pad1_dualshock2 || options.pad_pulse_script ||
					options.pad_auto_fire_button != PadAutoFireButton::None);
		if (load_result != PortableStateLoadResult::Loaded)
			return false;

		if (EmuConfig.DEV9.EthEnable || EmuConfig.DEV9.HddEnable)
		{
			Error::SetString(error,
				"Portable replay requires DEV9 Ethernet and HDD disabled.");
			return false;
		}
		Pcsx2Trace::BeginPortableReplayExternalDeviceAccessWindow();
		NotifyRestoredStateTraceStart();
		if (options.replay_state_checkpoint_start &&
			!Pcsx2Trace::RecordMachineCheckpointAtReplayStart())
		{
			Error::SetString(error, Pcsx2Trace::GetMachineCheckpointTraceError());
			return false;
		}
		return true;
	}

	bool FinishPortableReplayExternalDeviceAccessWindow(Error* error)
	{
		const Pcsx2Trace::PortableReplayExternalDeviceAccessCounts counts =
			Pcsx2Trace::GetPortableReplayExternalDeviceAccessCounts();
		Pcsx2Trace::EndPortableReplayExternalDeviceAccessWindow();
		std::fprintf(stdout,
			"portable_replay external_device_accesses dev9_reads=%llu dev9_writes=%llu "
			"dev9_dma=%llu dev9_irq_scheduled=%llu dev9_irq_delivered=%llu "
			"firewire_reads=%llu firewire_writes=%llu firewire_irq=%llu status=%s\n",
			static_cast<unsigned long long>(counts.dev9_reads),
			static_cast<unsigned long long>(counts.dev9_writes),
			static_cast<unsigned long long>(counts.dev9_dma),
			static_cast<unsigned long long>(counts.dev9_irq_scheduled),
			static_cast<unsigned long long>(counts.dev9_irq_delivered),
			static_cast<unsigned long long>(counts.firewire_reads),
			static_cast<unsigned long long>(counts.firewire_writes),
			static_cast<unsigned long long>(counts.firewire_irq),
			counts.IsZero() ? "pass" : "rejected");
		if (!counts.IsZero())
		{
			Error::SetString(error,
				"Portable replay touched unserialized DEV9 or FireWire state during the bounded continuation.");
			return false;
		}
		return true;
	}

	void NotifyRestoredStateTraceStart()
	{
		// The restored state already marks the ELF as executed, so the normal ELF
		// hook will not run again. Arm every trace at the loaded architectural PC
		// before VMManager::Execute() can execute one guest instruction.
		const u32 pc = cpuRegs.pc;
		Pcsx2Trace::NotifyCoreEventElfEntry(pc);
		Pcsx2Trace::NotifyEeElfEntry(pc);
		Pcsx2Trace::NotifyMemElfEntry(pc);
		Pcsx2Trace::NotifyGsElfEntry(pc);
		Pcsx2Trace::NotifyIopElfEntry(pc);
		Pcsx2Trace::NotifyIpuElfEntry(pc);
		Pcsx2Trace::NotifyMachineCheckpointElfEntry(pc);
		Pcsx2Trace::NotifySifElfEntry(pc);
		Pcsx2Trace::NotifySpu2ElfEntry(pc);
		Pcsx2Trace::NotifyVifElfEntry(pc);
		Pcsx2Trace::NotifyVuElfEntry(pc);
	}

	bool SaveReplayState(const TraceOptions& options, Error* error)
	{
		if (options.replay_state_output_path.empty())
			return true;

		std::vector<u8> raw_bytes;
		const auto encode_current_state = [&](std::vector<u8>* destination) {
			std::unique_ptr<ArchiveEntryList> entries =
				SaveState_DownloadPortableState(error);
			return entries && SaveStateRaw::Encode(*entries, destination, error);
		};
		if (!encode_current_state(&raw_bytes))
			return false;

		// Every portable component is required to be observationally read-only
		// while saving. Serialize twice at the same stopped event seam so a
		// component which publishes/reset its own live state after writing that
		// field cannot create a replay seed different from the checkpointed VM.
		std::vector<u8> verification_bytes;
		if (!encode_current_state(&verification_bytes))
			return false;
		if (verification_bytes != raw_bytes)
		{
			Error::SetString(error,
				"Portable replay serialization mutated or sampled unstable machine state.");
			return false;
		}

		const std::string output_directory(
			Path::GetDirectory(options.replay_state_output_path));
		if (!output_directory.empty() &&
			!FileSystem::EnsureDirectoryExists(output_directory.c_str(), false, error))
		{
			return false;
		}
		if (!FileSystem::WriteBinaryFile(options.replay_state_output_path.c_str(),
				raw_bytes.data(), raw_bytes.size()))
		{
			Error::SetStringFmt(error, "Failed to write replay state '{}'.",
				options.replay_state_output_path);
			return false;
		}

		// Re-read the durable artifact rather than trusting only the in-memory
		// encoder result. Partial/trailing output must not be published as a seed.
		const std::optional<std::vector<u8>> durable_bytes =
			FileSystem::ReadBinaryFile(options.replay_state_output_path.c_str());
		if (!durable_bytes.has_value() || !SaveStateRaw::Decode(durable_bytes.value(), error))
		{
			FileSystem::DeleteFilePath(options.replay_state_output_path.c_str());
			if (!error->IsValid())
				Error::SetString(error, "Failed to validate the durable replay state.");
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
		if (options.ee_entry_state)
		{
			std::fprintf(stdout,
				"requested execution providers: ee=%s iop=%s vu0=%s vu1=%s\n",
				options.recompiler_ee ? "recompiler" : "interpreter",
				options.recompiler_iop ? "recompiler" : "interpreter",
				options.recompiler_vu ? "recompiler" : "interpreter",
				options.recompiler_vu ? "recompiler" : "interpreter");
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
		bool machine_checkpoint_trace_started = false;
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
			trace_config.capture_vu0_state = options.ee_vu0_state;
			trace_config.record_elf_entry_state = options.ee_entry_state;
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
			trace_config.execution_provider_mask =
				(options.recompiler_ee ?
					static_cast<u32>(Pcsx2Trace::CoreEventTraceExecutionEeRecompiler) : 0u) |
				(options.recompiler_iop ?
					static_cast<u32>(Pcsx2Trace::CoreEventTraceExecutionIopRecompiler) : 0u) |
				(options.recompiler_vu ?
					static_cast<u32>(Pcsx2Trace::CoreEventTraceExecutionVu0Recompiler |
						Pcsx2Trace::CoreEventTraceExecutionVu1Recompiler) : 0u);
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
		if (!options.machine_checkpoint_output_path.empty())
		{
			Pcsx2Trace::MachineCheckpointTraceConfig trace_config;
			trace_config.output_path = options.machine_checkpoint_output_path;
			trace_config.diagnostic_dump_directory =
				options.machine_checkpoint_diagnostic_directory;
			trace_config.max_records = options.max_machine_checkpoint_records;
			trace_config.skip_records = options.machine_checkpoint_skip_records;
			trace_config.after_sif_records = options.machine_checkpoint_after_sif_records;
			trace_config.after_vif_records = options.machine_checkpoint_after_vif_records;
			trace_config.after_vsync_frames = options.machine_checkpoint_after_vsync_frames;
			trace_config.wait_for_elf_entry = options.wait_for_elf_entry;
			if (!Pcsx2Trace::StartMachineCheckpointTrace(trace_config, &error))
			{
				std::fprintf(stderr, "Failed to start machine checkpoint trace: %s\n",
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
				return 2;
			}
			machine_checkpoint_trace_started = true;
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
		boot.save_state = options.pcsx2_state_input_path;

		const VMBootResult boot_result = VMManager::Initialize(boot, &error);
		if (boot_result != VMBootResult::StartupSuccess)
		{
			std::fprintf(stderr, "Failed to boot %s: %s\n",
				options.boot_bios_only ? "BIOS" : (options.boot_disc ? "disc" : "ELF"),
				error.GetDescription().c_str());
			if (machine_checkpoint_trace_started)
				Pcsx2Trace::StopMachineCheckpointTrace();
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
		if (!BeginPcsx2StateContinuation(options, &error) ||
			!LoadReplayState(options, &error))
		{
			std::fprintf(stderr, "Failed to start restored state: %s\n",
				error.GetDescription().c_str());
			if (machine_checkpoint_trace_started)
				Pcsx2Trace::StopMachineCheckpointTrace();
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
			VMManager::Shutdown(false);
			VMManager::Internal::CPUThreadShutdown();
			return 3;
		}
		VMManager::SetState(VMState::Running);
		s_pad_script_entry_cycle = 0;
		s_pad_script_state = 0;
		s_pad_pulse_script_enabled = options.pad_pulse_script;
		s_pad_auto_fire_button = options.pad_auto_fire_button;
		s_pad_auto_fire_pressed_frames = options.pad_auto_fire_pressed_frames;
		s_pad_auto_fire_released_frames = options.pad_auto_fire_released_frames;
		s_pad_auto_fire_entry_frame = 0;
		s_pad_auto_fire_last_frame = 0;
		s_pad_auto_fire_press_edges = 0;
		s_pad_auto_fire_release_edges = 0;
		s_pad_auto_fire_initialized = false;
		s_pad_auto_fire_pressed = false;
		s_stop_after_sif_limit = options.stop_after_sif_limit;
		s_machine_checkpoint_trace_enabled = machine_checkpoint_trace_started;
		// Vita's workload loader presents phase zero immediately after the
		// portable state is restored and before one guest instruction can run.
		// Do not defer the oracle's first edge to its next scheduler callback.
		if (!options.replay_state_input_path.empty() &&
			options.pad_auto_fire_button != PadAutoFireButton::None)
		{
			UpdatePadPulseScript();
		}
		if (options.pad_pulse_script ||
			options.pad_auto_fire_button != PadAutoFireButton::None ||
			core_event_trace_started ||
			machine_checkpoint_trace_started || s_stop_after_sif_limit)
			Pcsx2Trace::SetCoreEventSchedulerCallback(UpdatePadPulseScript);
		if (options.pad_auto_fire_button != PadAutoFireButton::None)
		{
			std::fprintf(stdout,
				"pad autofire: button=%s pressed_frames=%u released_frames=%u start=%s\n",
				PadAutoFireButtonName(options.pad_auto_fire_button),
				options.pad_auto_fire_pressed_frames,
				options.pad_auto_fire_released_frames,
				options.replay_state_input_path.empty() ? "game-elf" : "replay-state");
		}
		const auto auxiliary_trace_failed = [&]() {
			return
				(ee_trace_started && !Pcsx2Trace::GetEeTraceError().empty()) ||
				(iop_trace_started && !Pcsx2Trace::GetIopTraceError().empty()) ||
				(mem_trace_started && !Pcsx2Trace::GetMemTraceError().empty()) ||
				(gs_trace_started && !Pcsx2Trace::GetGsTraceError().empty()) ||
				(ipu_trace_started && !Pcsx2Trace::GetIpuTraceError().empty()) ||
				(sif_trace_started && !Pcsx2Trace::GetSifTraceError().empty()) ||
				(core_event_trace_started && !Pcsx2Trace::GetCoreEventTraceError().empty()) ||
				(spu2_trace_started && !Pcsx2Trace::GetSpu2TraceError().empty()) ||
				(vif_trace_started && !Pcsx2Trace::GetVifTraceError().empty()) ||
				(vu_trace_started && !Pcsx2Trace::GetVuTraceError().empty());
		};
		// PCSX2's CPU-thread owner calls Execute() again after a runtime provider
		// switch. Fast boot applies the game configuration at ELF entry, so a
		// recompiler-enabled trace must survive that first intentional return.
		if (!machine_checkpoint_trace_started ||
			!Pcsx2Trace::DidMachineCheckpointTraceHitLimit())
		{
			do
			{
				VMManager::Execute();
			} while (VMManager::GetState() == VMState::Running && !auxiliary_trace_failed() &&
				(machine_checkpoint_trace_started ?
					!Pcsx2Trace::DidMachineCheckpointTraceHitLimit() :
					(!(ee_trace_started && Pcsx2Trace::DidEeTraceHitLimit()) &&
					 !(iop_trace_started && Pcsx2Trace::DidIopTraceHitLimit()) &&
					 !(mem_trace_started && Pcsx2Trace::DidMemTraceHitLimit()) &&
					 !(gs_trace_started && Pcsx2Trace::DidGsTraceHitLimit()) &&
					 !(ipu_trace_started && Pcsx2Trace::DidIpuTraceHitLimit()) &&
					 !(core_event_trace_started && Pcsx2Trace::DidCoreEventTraceHitLimit()) &&
					 !(spu2_trace_started && Pcsx2Trace::DidSpu2TraceHitLimit()) &&
					 !(vif_trace_started && Pcsx2Trace::DidVifTraceHitLimit()) &&
					 !(vu_trace_started && Pcsx2Trace::DidVuTraceHitLimit()) &&
					 !(s_stop_after_sif_limit && Pcsx2Trace::DidSifTraceHitLimit()))));
		}
		Pcsx2Trace::SetCoreEventSchedulerCallback(nullptr);
		if (s_pad_auto_fire_button != PadAutoFireButton::None)
		{
			const u32 input = s_pad_auto_fire_button == PadAutoFireButton::Cross ?
				PadDualshock2::Inputs::PAD_CROSS : PadDualshock2::Inputs::PAD_CIRCLE;
			Pad::SetControllerState(0, input, 0.0f);
			std::fprintf(stdout,
				"pad autofire edges: press=%u release=%u\n",
				s_pad_auto_fire_press_edges, s_pad_auto_fire_release_edges);
			s_pad_auto_fire_button = PadAutoFireButton::None;
		}
		s_machine_checkpoint_trace_enabled = false;
		if (options.ee_entry_state)
		{
			std::fprintf(stdout,
				"active execution providers: ee=%s iop=%s vu0=%s vu1=%s\n",
				Cpu == &recCpu ? "recompiler" : "interpreter",
				psxCpu == &psxRec ? "recompiler" : "interpreter",
				CpuVU0 == &CpuMicroVU0 ? "recompiler" : "interpreter",
				CpuVU1 == &CpuMicroVU1 ? "recompiler" : "interpreter");
		}
		bool replay_external_device_accesses_valid = true;
		std::string replay_external_device_accesses_error;
		if (!options.pcsx2_state_input_path.empty() ||
			!options.replay_state_input_path.empty())
		{
			Error access_error;
			replay_external_device_accesses_valid =
				FinishPortableReplayExternalDeviceAccessWindow(&access_error);
			if (!replay_external_device_accesses_valid)
				replay_external_device_accesses_error = access_error.GetDescription();
		}

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
		const u64 machine_checkpoint_records =
			Pcsx2Trace::GetMachineCheckpointTraceRecordsWritten();
		const bool machine_checkpoint_hit_limit =
			Pcsx2Trace::DidMachineCheckpointTraceHitLimit();
		const std::string machine_checkpoint_trace_error =
			Pcsx2Trace::GetMachineCheckpointTraceError();
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
		bool replay_state_saved = true;
		std::string replay_state_error;
		if (!options.replay_state_output_path.empty())
		{
			if (!replay_external_device_accesses_valid)
			{
				replay_state_saved = false;
				replay_state_error = replay_external_device_accesses_error;
			}
			else if (!machine_checkpoint_hit_limit || machine_checkpoint_records == 0 ||
				(options.max_machine_checkpoint_records != 0 &&
				 machine_checkpoint_records != options.max_machine_checkpoint_records))
			{
				replay_state_saved = false;
				replay_state_error =
					"terminal machine checkpoint was not reached; refusing to publish a replay seed";
			}
			else
			{
				Error save_error;
				replay_state_saved = SaveReplayState(options, &save_error);
				if (!replay_state_saved)
					replay_state_error = save_error.GetDescription();
			}
		}
		if (machine_checkpoint_trace_started)
			Pcsx2Trace::StopMachineCheckpointTrace();
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
		if (!machine_checkpoint_trace_error.empty())
		{
			std::fprintf(stderr, "Machine checkpoint trace failed after %llu records: %s\n",
				static_cast<unsigned long long>(machine_checkpoint_records),
				machine_checkpoint_trace_error.c_str());
			return 4;
		}
		if (!replay_external_device_accesses_valid)
		{
			std::fprintf(stderr, "Portable replay external-device gate failed: %s\n",
				replay_external_device_accesses_error.c_str());
			return 4;
		}
		if (!replay_state_saved)
		{
			std::fprintf(stderr, "Failed to save replay state: %s\n",
				replay_state_error.c_str());
			return 4;
		}
		if (machine_checkpoint_trace_started && options.max_machine_checkpoint_records != 0 &&
			(!machine_checkpoint_hit_limit ||
			 machine_checkpoint_records != options.max_machine_checkpoint_records))
		{
			std::fprintf(stderr,
				"Machine checkpoint trace stopped before its required limit: wrote %llu of %llu records.\n",
				static_cast<unsigned long long>(machine_checkpoint_records),
				static_cast<unsigned long long>(options.max_machine_checkpoint_records));
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
			if (options.ee_entry_state)
			{
				std::fprintf(stdout, "wrote %llu EE ELF-entry state records to %s%s\n",
					static_cast<unsigned long long>(ee_records), options.output_path.c_str(),
					ee_hit_limit ? " (hit limit)" : "");
			}
			else
			{
				std::fprintf(stdout, "wrote %llu EE pre-instruction records to %s%s\n",
					static_cast<unsigned long long>(ee_records), options.output_path.c_str(),
					ee_hit_limit ? " (hit limit)" : "");
			}
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
		if (machine_checkpoint_trace_started)
		{
			std::fprintf(stdout, "wrote %llu machine checkpoint records to %s%s\n",
				static_cast<unsigned long long>(machine_checkpoint_records),
				options.machine_checkpoint_output_path.c_str(),
				machine_checkpoint_hit_limit ? " (hit limit)" : "");
		}
		if (!options.replay_state_output_path.empty())
		{
			std::fprintf(stdout, "wrote replay state to %s\n",
				options.replay_state_output_path.c_str());
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
		if (machine_checkpoint_trace_started)
		{
			return (machine_checkpoint_hit_limit &&
				(options.max_machine_checkpoint_records == 0 ||
				 machine_checkpoint_records == options.max_machine_checkpoint_records)) ? 0 : 1;
		}
		return (ee_hit_limit || iop_hit_limit || mem_hit_limit || gs_hit_limit || ipu_hit_limit ||
			machine_checkpoint_hit_limit ||
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
