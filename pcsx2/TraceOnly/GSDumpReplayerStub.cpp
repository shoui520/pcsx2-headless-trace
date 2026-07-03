// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GSDumpReplayer.h"

#include "R5900.h"

namespace
{
	void CpuNoop()
	{
	}

	void CpuClearNoop(u32 addr, u32 size)
	{
	}
} // namespace

R5900cpu GSDumpReplayerCpu = {
	CpuNoop,
	CpuNoop,
	CpuNoop,
	CpuNoop,
	CpuNoop,
	CpuNoop,
	CpuNoop,
	CpuClearNoop};

bool GSDumpReplayer::IsReplayingDump()
{
	return false;
}

void GSDumpReplayer::SetLoopCount(s32 loop_count)
{
}

int GSDumpReplayer::GetLoopCount()
{
	return 0;
}

bool GSDumpReplayer::IsRunner()
{
	return false;
}

void GSDumpReplayer::SetIsDumpRunner(bool is_runner)
{
}

bool GSDumpReplayer::Initialize(const char* filename, Error* error)
{
	if (error)
		Error::SetString(error, "GS dump replay is disabled in PCSX2 trace-only builds.");
	return false;
}

bool GSDumpReplayer::ChangeDump(const char* filename)
{
	return false;
}

void GSDumpReplayer::Shutdown()
{
}

std::string GSDumpReplayer::GetDumpSerial()
{
	return std::string();
}

u32 GSDumpReplayer::GetDumpCRC()
{
	return 0;
}

u32 GSDumpReplayer::GetFrameNumber()
{
	return 0;
}

void GSDumpReplayer::RenderUI()
{
}
