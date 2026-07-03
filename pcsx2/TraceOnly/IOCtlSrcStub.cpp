// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "CDVD/CDVDdiscReader.h"

#include "common/Error.h"

#include <cstring>
#include <utility>

IOCtlSrc::IOCtlSrc(std::string filename)
	: m_filename(std::move(filename))
{
}

IOCtlSrc::~IOCtlSrc() = default;

bool IOCtlSrc::ReadDVDInfo()
{
	return false;
}

bool IOCtlSrc::ReadCDInfo()
{
	return false;
}

bool IOCtlSrc::Reopen(Error* error)
{
	Error::SetStringView(error, "Physical optical drives are unavailable in PCSX2 trace-only builds.");
	return false;
}

u32 IOCtlSrc::GetSectorCount() const
{
	return 0;
}

const std::vector<toc_entry>& IOCtlSrc::ReadTOC() const
{
	return m_toc;
}

bool IOCtlSrc::ReadSectors2048(u32 sector, u32 count, u8* buffer) const
{
	if (buffer && count != 0)
		std::memset(buffer, 0, static_cast<size_t>(count) * 2048);
	return false;
}

bool IOCtlSrc::ReadSectors2352(u32 sector, u32 count, u8* buffer) const
{
	if (buffer && count != 0)
		std::memset(buffer, 0, static_cast<size_t>(count) * 2352);
	return false;
}

bool IOCtlSrc::ReadTrackSubQ(cdvdSubQ* subq) const
{
	if (subq)
		std::memset(subq, 0, sizeof(*subq));
	return false;
}

u32 IOCtlSrc::GetLayerBreakAddress() const
{
	return 0;
}

s32 IOCtlSrc::GetMediaType() const
{
	return CDVD_TYPE_NODISC;
}

void IOCtlSrc::SetSpindleSpeed(bool restore_defaults) const
{
}

bool IOCtlSrc::DiscReady()
{
	return false;
}

std::vector<std::string> GetOpticalDriveList()
{
	return {};
}

void GetValidDrive(std::string& drive)
{
	drive.clear();
}
