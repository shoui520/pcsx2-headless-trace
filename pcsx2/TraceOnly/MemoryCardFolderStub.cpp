// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "SIO/Memcard/MemoryCardFile.h"
#include "SIO/Memcard/MemoryCardFolder.h"

#include <algorithm>
#include <cstring>

MemoryCardFileEntryDateTime MemoryCardFileEntryDateTime::FromTime(time_t time)
{
	struct tm converted = {};
#ifdef _MSC_VER
	gmtime_s(&converted, &time);
#else
	gmtime_r(&time, &converted);
#endif

	MemoryCardFileEntryDateTime ret = {};
	ret.second = converted.tm_sec;
	ret.minute = converted.tm_min;
	ret.hour = converted.tm_hour;
	ret.day = converted.tm_mday;
	ret.month = converted.tm_mon + 1;
	ret.year = converted.tm_year + 1900;
	return ret;
}

time_t MemoryCardFileEntryDateTime::ToTime() const
{
	struct tm converted = {};
	converted.tm_sec = second;
	converted.tm_min = minute;
	converted.tm_hour = hour;
	converted.tm_mday = day;
	converted.tm_mon = std::max(static_cast<int>(month) - 1, 0);
	converted.tm_year = std::max(static_cast<int>(year) - 1900, 0);

#ifdef _MSC_VER
	return _mkgmtime(&converted);
#else
	return timegm(&converted);
#endif
}

FileAccessHelper::FileAccessHelper() = default;

FileAccessHelper::~FileAccessHelper() = default;

std::FILE* FileAccessHelper::ReOpen(const std::string_view folderName, MemoryCardFileMetadataReference* fileRef, bool writeMetadata)
{
	return nullptr;
}

void FileAccessHelper::CloseMatching(const std::string_view path)
{
}

void FileAccessHelper::CloseAll()
{
}

void FileAccessHelper::FlushAll()
{
}

void FileAccessHelper::ClearMetadataWriteState()
{
}

bool FileAccessHelper::CleanMemcardFilename(char* name)
{
	return false;
}

void FileAccessHelper::WriteIndex(
	const std::string& baseFolderName, MemoryCardFileEntry* const entry, MemoryCardFileMetadataReference* const parent)
{
}

std::FILE* FileAccessHelper::Open(const std::string_view folderName, MemoryCardFileMetadataReference* fileRef, bool writeMetadata)
{
	return nullptr;
}

void FileAccessHelper::CloseFileHandle(std::FILE*& file, const MemoryCardFileEntry* entry)
{
	file = nullptr;
}

void FileAccessHelper::WriteMetadata(const std::string_view folderName, const MemoryCardFileMetadataReference* fileRef)
{
}

bool FileAccessHelper::CleanMemcardFilenameEndDotOrSpace(char* name, size_t length)
{
	return false;
}

bool MemoryCardFileMetadataReference::GetPath(std::string* fileName) const
{
	if (fileName)
		fileName->clear();
	return false;
}

void MemoryCardFileMetadataReference::GetInternalPath(std::string* fileName) const
{
	if (fileName)
		fileName->clear();
}

FolderMemoryCard::FolderMemoryCard()
	: m_framesUntilFlush(0)
	, m_timeLastWritten(0)
	, m_slot(0)
	, m_isEnabled(false)
	, m_performFileWrites(false)
	, m_filteringEnabled(false)
{
}

void FolderMemoryCard::Lock()
{
}

void FolderMemoryCard::Unlock()
{
}

void FolderMemoryCard::Open(const bool enableFiltering, std::string filter)
{
	m_isEnabled = false;
}

void FolderMemoryCard::Open(std::string fullPath, const Pcsx2Config::McdOptions& mcdOptions, const u32 sizeInClusters,
	const bool enableFiltering, std::string filter, bool simulateFileWrites)
{
	m_isEnabled = false;
}

void FolderMemoryCard::Close(bool flush)
{
	m_isEnabled = false;
}

bool FolderMemoryCard::IsFormatted() const
{
	return false;
}

bool FolderMemoryCard::ReIndex(bool enableFiltering, const std::string& filter)
{
	return false;
}

s32 FolderMemoryCard::IsPresent() const
{
	return 0;
}

void FolderMemoryCard::GetSizeInfo(McdSizeInfo& outways) const
{
	outways = {};
}

bool FolderMemoryCard::IsPSX() const
{
	return false;
}

s32 FolderMemoryCard::Read(u8* dest, u32 adr, int size)
{
	if (dest && size > 0)
		std::memset(dest, 0, static_cast<size_t>(size));
	return 0;
}

s32 FolderMemoryCard::Save(const u8* src, u32 adr, int size)
{
	return 0;
}

s32 FolderMemoryCard::EraseBlock(u32 adr)
{
	return 0;
}

u64 FolderMemoryCard::GetCRC() const
{
	return 0;
}

void FolderMemoryCard::SetSlot(uint slot)
{
	m_slot = slot;
}

u32 FolderMemoryCard::GetSizeInClusters() const
{
	return 0;
}

void FolderMemoryCard::SetSizeInClusters(u32 clusters)
{
}

void FolderMemoryCard::SetSizeInMB(u32 megaBytes)
{
}

void FolderMemoryCard::NextFrame()
{
}

void FolderMemoryCard::CalculateECC(u8* ecc, const u8* data)
{
	if (ecc)
		std::memset(ecc, 0, EccSize);
}

void FolderMemoryCard::WriteToFile(const std::string& filename)
{
}

const std::string& FolderMemoryCard::GetFolderName()
{
	return m_folderName;
}

FolderMemoryCardAggregator::FolderMemoryCardAggregator() = default;

void FolderMemoryCardAggregator::Open()
{
}

void FolderMemoryCardAggregator::Close()
{
}

void FolderMemoryCardAggregator::SetFiltering(const bool enableFiltering)
{
	m_enableFiltering = enableFiltering;
}

s32 FolderMemoryCardAggregator::IsPresent(uint slot)
{
	return 0;
}

void FolderMemoryCardAggregator::GetSizeInfo(uint slot, McdSizeInfo& outways)
{
	outways = {};
}

bool FolderMemoryCardAggregator::IsPSX(uint slot)
{
	return false;
}

s32 FolderMemoryCardAggregator::Read(uint slot, u8* dest, u32 adr, int size)
{
	if (dest && size > 0)
		std::memset(dest, 0, static_cast<size_t>(size));
	return 0;
}

s32 FolderMemoryCardAggregator::Save(uint slot, const u8* src, u32 adr, int size)
{
	return 0;
}

s32 FolderMemoryCardAggregator::EraseBlock(uint slot, u32 adr)
{
	return 0;
}

u64 FolderMemoryCardAggregator::GetCRC(uint slot)
{
	return 0;
}

void FolderMemoryCardAggregator::NextFrame(uint slot)
{
}

bool FolderMemoryCardAggregator::ReIndex(uint slot, const bool enableFiltering, const std::string& filter)
{
	return false;
}
