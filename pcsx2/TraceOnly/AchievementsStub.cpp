// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Achievements.h"

#include "common/Error.h"

namespace
{
	std::recursive_mutex s_lock;
	const std::string s_empty_string;
} // namespace

std::unique_lock<std::recursive_mutex> Achievements::GetLock()
{
	return std::unique_lock<std::recursive_mutex>(s_lock);
}

bool Achievements::Initialize()
{
	return true;
}

void Achievements::UpdateSettings(const Pcsx2Config::AchievementsOptions& old_config)
{
}

void Achievements::ResetClient()
{
}

bool Achievements::ConfirmSystemReset()
{
	return true;
}

bool Achievements::Shutdown(bool allow_cancel)
{
	return true;
}

void Achievements::OnVMPaused(bool paused)
{
}

void Achievements::FrameUpdate()
{
}

void Achievements::IdleUpdate()
{
}

void Achievements::LoadState(std::span<const u8> data)
{
}

void Achievements::SaveState(SaveStateBase& writer)
{
}

bool Achievements::Login(const char* username, const char* password, Error* error)
{
	if (error)
		Error::SetString(error, "Achievements are disabled in PCSX2 trace-only builds.");
	return false;
}

void Achievements::Logout()
{
}

void Achievements::GameChanged(u32 disc_crc, u32 crc)
{
}

void Achievements::PlayAchievementSound(bool is_specific_sound_enabled, const std::string& custom_sound_name,
	const std::string& default_sound_name)
{
}

bool Achievements::ResetHardcoreMode(bool is_booting)
{
	return false;
}

void Achievements::DisableHardcoreMode()
{
}

const char* Achievements::GetHardcoreModeDisableTitle()
{
	return "";
}

std::string Achievements::GetHardcoreModeDisableText(const char* reason)
{
	return std::string();
}

bool Achievements::IsHardcoreModeActive()
{
	return false;
}

bool Achievements::IsUsingRAIntegration()
{
	return false;
}

bool Achievements::IsActive()
{
	return false;
}

bool Achievements::HasActiveGame()
{
	return false;
}

u32 Achievements::GetGameID()
{
	return 0;
}

bool Achievements::HasAchievementsOrLeaderboards()
{
	return false;
}

bool Achievements::HasAchievements()
{
	return false;
}

bool Achievements::HasLeaderboards()
{
	return false;
}

bool Achievements::HasRichPresence()
{
	return false;
}

const std::string& Achievements::GetRichPresenceString()
{
	return s_empty_string;
}

const std::string& Achievements::GetGameIconURL()
{
	return s_empty_string;
}

const std::string& Achievements::GetGameTitle()
{
	return s_empty_string;
}

const char* Achievements::GetLoggedInUserName()
{
	return "";
}

std::string Achievements::GetLoggedInUserBadgePath()
{
	return std::string();
}

void Achievements::ClearUIState()
{
}

void Achievements::DrawGameOverlays()
{
}

void Achievements::DrawPauseMenuOverlays()
{
}

bool Achievements::PrepareAchievementsWindow()
{
	return false;
}

void Achievements::DrawAchievementsWindow()
{
}

bool Achievements::PrepareLeaderboardsWindow()
{
	return false;
}

void Achievements::DrawLeaderboardsWindow()
{
}
