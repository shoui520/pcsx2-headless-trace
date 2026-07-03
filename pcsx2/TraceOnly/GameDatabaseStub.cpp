// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GameDatabase.h"

#include <cstdio>
#include <cstring>

std::string GameDatabaseSchema::GameEntry::memcardFiltersAsString() const
{
	std::string ret;
	for (const std::string& filter : memcardFilters)
	{
		if (!ret.empty())
			ret += '/';
		ret += filter;
	}
	return ret;
}

const std::string* GameDatabaseSchema::GameEntry::findPatch(u32 crc) const
{
	const auto it = patches.find(crc);
	return (it != patches.end()) ? &it->second : nullptr;
}

const char* GameDatabaseSchema::GameEntry::compatAsString() const
{
	return "Unknown";
}

void GameDatabaseSchema::GameEntry::applyGameFixes(Pcsx2Config& config, bool applyAuto) const
{
}

void GameDatabaseSchema::GameEntry::applyGSHardwareFixes(Pcsx2Config::GSOptions& config) const
{
}

bool GameDatabaseSchema::GameEntry::configMatchesHWFix(const Pcsx2Config::GSOptions& config, GSHWFixId id, int value)
{
	return false;
}

void GameDatabase::ensureLoaded()
{
}

const GameDatabaseSchema::GameEntry* GameDatabase::findGame(const std::string_view serial)
{
	return nullptr;
}

bool GameDatabase::TrackHash::parseHash(const std::string_view str)
{
	constexpr u32 expected_length = SIZE * 2;
	if (str.length() != expected_length)
		return false;

	std::memset(data, 0, sizeof(data));
	for (u32 i = 0; i < SIZE * 2; i++)
	{
		const char ch = str[i];
		u8 b;
		if (ch >= '0' && ch <= '9')
			b = static_cast<u8>(ch - '0');
		else if (ch >= 'a' && ch <= 'f')
			b = static_cast<u8>(ch - 'a') + 0xa;
		else if (ch >= 'A' && ch <= 'F')
			b = static_cast<u8>(ch - 'A') + 0xa;
		else
			return false;

		data[i / 2] |= ((i % 2) == 0) ? (b << 4) : b;
	}

	return true;
}

std::string GameDatabase::TrackHash::toString() const
{
	char ret[(SIZE * 2) + 1];
	for (u32 i = 0; i < SIZE; i++)
		std::snprintf(&ret[i * 2], 3, "%02x", data[i]);
	ret[SIZE * 2] = '\0';
	return std::string(ret);
}

bool GameDatabase::loadHashDatabase()
{
	return false;
}

void GameDatabase::unloadHashDatabase()
{
}

const GameDatabase::HashDatabaseEntry* GameDatabase::lookupHash(
	const TrackHash* tracks, size_t num_tracks, bool* tracks_matched, std::string* match_error)
{
	if (tracks_matched)
		*tracks_matched = false;
	return nullptr;
}
