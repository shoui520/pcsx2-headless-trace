// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ImGui/FullscreenUI.h"
#include "ImGui/ImGuiManager.h"
#include "ImGui/ImGuiOverlays.h"
#include "Input/InputManager.h"

InputRecordingUI::InputRecordingData g_InputRecordingData;

bool FullscreenUI::Initialize()
{
	return true;
}

bool FullscreenUI::IsInitialized()
{
	return false;
}

void FullscreenUI::ReloadSvgResources()
{
}

bool FullscreenUI::HasActiveWindow()
{
	return false;
}

void FullscreenUI::CheckForConfigChanges(const Pcsx2Config& old_config)
{
}

void FullscreenUI::OnVMStarted()
{
}

void FullscreenUI::OnVMDestroyed()
{
}

void FullscreenUI::GameChanged(std::string title, std::string path, std::string serial, u32 disc_crc, u32 crc)
{
}

void FullscreenUI::OpenPauseMenu()
{
}

bool FullscreenUI::OpenAchievementsWindow()
{
	return false;
}

bool FullscreenUI::OpenLeaderboardsWindow()
{
	return false;
}

void FullscreenUI::ReportStateLoadError(const std::string& message, std::optional<s32> slot, bool backup)
{
}

void FullscreenUI::ReportStateSaveError(const std::string& message, std::optional<s32> slot)
{
}

bool FullscreenUI::IsAchievementsWindowOpen()
{
	return false;
}

bool FullscreenUI::IsLeaderboardsWindowOpen()
{
	return false;
}

void FullscreenUI::ReturnToPreviousWindow()
{
}

void FullscreenUI::ReturnToMainWindow()
{
}

void FullscreenUI::SetStandardSelectionFooterText(bool back_instead_of_cancel)
{
}

void FullscreenUI::LocaleChanged()
{
}

void FullscreenUI::GamepadLayoutChanged()
{
}

void FullscreenUI::PreferEnglishGameListChanged()
{
}

void FullscreenUI::Shutdown(bool clear_state)
{
}

void FullscreenUI::Render()
{
}

void FullscreenUI::InvalidateCoverCache()
{
}

TinyString FullscreenUI::TimeToPrintableString(time_t t)
{
	return TinyString();
}

bool FullscreenUI::CreateHardDriveWithProgress(const std::string& filePath, int sizeInGB, bool use48BitLBA)
{
	return false;
}

void FullscreenUI::CancelAllHddOperations()
{
}

void ImGuiManager::SetFonts(std::vector<FontInfo> info)
{
}

bool ImGuiManager::Initialize()
{
	return true;
}

bool ImGuiManager::InitializeFullscreenUI()
{
	return false;
}

void ImGuiManager::Shutdown(bool clear_state)
{
}

float ImGuiManager::GetWindowWidth()
{
	return 640.0f;
}

float ImGuiManager::GetWindowHeight()
{
	return 480.0f;
}

void ImGuiManager::WindowResized()
{
}

void ImGuiManager::RequestScaleUpdate()
{
}

void ImGuiManager::ReloadFonts()
{
}

void ImGuiManager::NewFrame()
{
}

void ImGuiManager::SkipFrame()
{
}

void ImGuiManager::RenderOSD()
{
}

float ImGuiManager::GetGlobalScale()
{
	return 1.0f;
}

ImFont* ImGuiManager::GetStandardFont()
{
	return nullptr;
}

ImFont* ImGuiManager::GetFixedFont()
{
	return nullptr;
}

ImFont* ImGuiManager::GetOSDFont()
{
	return nullptr;
}

float ImGuiManager::GetFontSizeStandard()
{
	return 13.0f;
}

float ImGuiManager::GetFontSizeMedium()
{
	return 14.0f;
}

float ImGuiManager::GetFontSizeLarge()
{
	return 22.0f;
}

bool ImGuiManager::WantsTextInput()
{
	return false;
}

bool ImGuiManager::WantsMouseInput()
{
	return false;
}

void ImGuiManager::AddTextInput(std::string str)
{
}

void ImGuiManager::UpdateMousePosition(float x, float y)
{
}

bool ImGuiManager::ProcessPointerButtonEvent(InputBindingKey key, float value)
{
	return false;
}

bool ImGuiManager::ProcessPointerAxisEvent(InputBindingKey key, float value)
{
	return false;
}

bool ImGuiManager::ProcessHostKeyEvent(InputBindingKey key, float value)
{
	return false;
}

bool ImGuiManager::ProcessGenericInputEvent(GenericInputBinding key, InputLayout layout, float value, u32 controller_id)
{
	return false;
}

void ImGuiManager::ProcessGenericAxisEvent(
	GenericInputBinding negative_key, GenericInputBinding positive_key, InputLayout layout, float value, u32 controller_id)
{
}

void ImGuiManager::SwapGamepadNorthWest(bool value)
{
}

bool ImGuiManager::IsGamepadNorthWestSwapped()
{
	return false;
}

void ImGuiManager::SetSoftwareCursor(u32 index, std::string image_path, float image_scale, u32 multiply_color)
{
}

bool ImGuiManager::HasSoftwareCursor(u32 index)
{
	return false;
}

void ImGuiManager::ClearSoftwareCursor(u32 index)
{
}

void ImGuiManager::SetSoftwareCursorPosition(u32 index, float pos_x, float pos_y)
{
}

std::string ImGuiManager::StripIconCharacters(std::string_view str)
{
	return std::string(str);
}

void ImGuiManager::RenderOverlays()
{
}

void SaveStateSelectorUI::Open(float open_time)
{
}

void SaveStateSelectorUI::RefreshList(const std::string& serial, u32 crc)
{
}

void SaveStateSelectorUI::DestroyTextures()
{
}

void SaveStateSelectorUI::Clear()
{
}

void SaveStateSelectorUI::Close()
{
}

bool SaveStateSelectorUI::IsOpen()
{
	return false;
}

void SaveStateSelectorUI::SelectNextSlot(bool open_selector)
{
}

void SaveStateSelectorUI::SelectPreviousSlot(bool open_selector)
{
}

s32 SaveStateSelectorUI::GetCurrentSlot()
{
	return 0;
}

void SaveStateSelectorUI::LoadCurrentSlot()
{
}

void SaveStateSelectorUI::LoadCurrentBackupSlot()
{
}

void SaveStateSelectorUI::SaveCurrentSlot()
{
}
