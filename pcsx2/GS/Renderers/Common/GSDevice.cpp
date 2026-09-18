// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Common/GSDevice.h"
#include "GS/GSGL.h"
#include "GS/GS.h"
#include "GS/GSUtil.h"
#include "Host.h"

#ifdef PCSX2_TRACE_ONLY
#include "DebugTools/GsTrace.h"
#endif

#include "common/Console.h"
#include "common/BitUtils.h"
#include "common/FileSystem.h"
#include "common/HostSys.h"
#include "common/Path.h"
#include "common/SmallString.h"
#include "common/StringUtil.h"
#include "common/Threading.h"

#ifndef PCSX2_TRACE_ONLY
#include "imgui.h"
#endif

#include <algorithm>
#include <fstream>
#include <limits>
#include <ostream>
#ifdef PCSX2_TRACE_ONLY
#include <unordered_map>
#include <vector>
#endif

const char* ShaderEntryPoint(ShaderConvert value)
{
	switch (value)
	{
		// clang-format off
		case ShaderConvert::COPY:                   return "ps_copy";
		case ShaderConvert::RGB5A1_TO_16_BITS:      return "ps_convert_rgb5a1_16bits";
		case ShaderConvert::DATM_1:                 return "ps_datm1";
		case ShaderConvert::DATM_0:                 return "ps_datm0";
		case ShaderConvert::DATM_1_RTA_CORRECTION:  return "ps_datm1_rta_correction";
		case ShaderConvert::DATM_0_RTA_CORRECTION:  return "ps_datm0_rta_correction";
		case ShaderConvert::COLCLIP_INIT:           return "ps_colclip_init";
		case ShaderConvert::COLCLIP_RESOLVE:        return "ps_colclip_resolve";
		case ShaderConvert::RTA_CORRECTION:         return "ps_rta_correction";
		case ShaderConvert::RTA_DECORRECTION:       return "ps_rta_decorrection";
		case ShaderConvert::TRANSPARENCY_FILTER:    return "ps_filter_transparency";
		case ShaderConvert::DEPTH32_TO_16_BITS:     return "ps_convert_depth32_32bits";
		case ShaderConvert::DEPTH32_TO_32_BITS:     return "ps_convert_depth32_32bits";
		case ShaderConvert::DEPTH32_TO_RGBA8:       return "ps_convert_depth32_rgba8";
		case ShaderConvert::DEPTH32_TO_RGB8:        return "ps_convert_depth32_rgba8";
		case ShaderConvert::DEPTH16_TO_RGB5A1:      return "ps_convert_depth16_rgb5a1";
		case ShaderConvert::RGBA8_TO_DEPTH32:       return "ps_convert_rgba8_depth32";
		case ShaderConvert::RGBA8_TO_DEPTH24:       return "ps_convert_rgba8_depth24";
		case ShaderConvert::RGBA8_TO_DEPTH16:       return "ps_convert_rgba8_depth16";
		case ShaderConvert::RGB5A1_TO_DEPTH16:      return "ps_convert_rgb5a1_depth16";
		case ShaderConvert::DEPTH32_TO_DEPTH24:     return "ps_convert_depth32_depth24";
		case ShaderConvert::DEPTH_COPY:             return "ps_depth_copy";
		case ShaderConvert::DOWNSAMPLE_COPY:        return "ps_downsample_copy";
		case ShaderConvert::RGBA_TO_8I:             return "ps_convert_rgba_8i";
		case ShaderConvert::RGB5A1_TO_8I:           return "ps_convert_rgb5a1_8i";
		case ShaderConvert::CLUT_4:                 return "ps_convert_clut_4";
		case ShaderConvert::CLUT_8:                 return "ps_convert_clut_8";
		case ShaderConvert::YUV:                    return "ps_yuv";
		// clang-format on
		default:
			pxAssert(0);
			return "ShaderConvertUnknownShader";
	}
}

const char* ShaderEntryPoint(PresentShader value)
{
	switch (value)
	{
		// clang-format off
		case PresentShader::COPY:               return "ps_copy";
		case PresentShader::SCANLINE:           return "ps_filter_scanlines";
		case PresentShader::DIAGONAL_FILTER:    return "ps_filter_diagonal";
		case PresentShader::TRIANGULAR_FILTER:  return "ps_filter_triangular";
		case PresentShader::COMPLEX_FILTER:     return "ps_filter_complex";
		case PresentShader::LOTTES_FILTER:      return "ps_filter_lottes";
		case PresentShader::SUPERSAMPLE_4xRGSS: return "ps_4x_rgss";
		case PresentShader::SUPERSAMPLE_AUTO:   return "ps_automagical_supersampling";
		// clang-format on
		default:
			pxAssert(0);
			return "DisplayShaderUnknownShader";
	}
}

const char* ShaderConvertName(ShaderConvert shader)
{
	#define ENTRY(x) case ShaderConvert::x: return #x
	switch (shader)
	{
		ENTRY(COPY);
		ENTRY(DEPTH_COPY);
		ENTRY(RGB5A1_TO_16_BITS);
		ENTRY(DATM_1);
		ENTRY(DATM_0);
		ENTRY(DATM_1_RTA_CORRECTION);
		ENTRY(DATM_0_RTA_CORRECTION);
		ENTRY(COLCLIP_INIT);
		ENTRY(COLCLIP_RESOLVE);
		ENTRY(RTA_CORRECTION);
		ENTRY(RTA_DECORRECTION);
		ENTRY(TRANSPARENCY_FILTER);
		ENTRY(DEPTH32_TO_16_BITS);
		ENTRY(DEPTH32_TO_32_BITS);
		ENTRY(DEPTH32_TO_RGBA8);
		ENTRY(DEPTH32_TO_RGB8);
		ENTRY(DEPTH16_TO_RGB5A1);
		ENTRY(RGBA8_TO_DEPTH32);
		ENTRY(RGBA8_TO_DEPTH24);
		ENTRY(RGBA8_TO_DEPTH16);
		ENTRY(RGB5A1_TO_DEPTH16);
		ENTRY(DEPTH32_TO_DEPTH24);
		ENTRY(DOWNSAMPLE_COPY);
		ENTRY(RGBA_TO_8I);
		ENTRY(RGB5A1_TO_8I);
		ENTRY(CLUT_4);
		ENTRY(CLUT_8);
		ENTRY(YUV);
		case ShaderConvert::Count: break;
	}
	#undef ENTRY
	pxAssert(false);
	return nullptr;
}

#ifdef PCSX2_DEVBUILD

enum class TextureLabel
{
	ColorRT,
	ColorHQRT,
	ColorHDRRT,
	ColorClipRT,
	U16RT,
	U32RT,
	DepthStencil,
	PrimIDTexture,
	RWTexture,
	CLUTTexture,
	Texture,
	ReplacementTexture,
	Other,
	Last = Other,
};

static std::array<u32, static_cast<u32>(TextureLabel::Last) + 1> s_texture_counts;

static TextureLabel GetTextureLabel(GSTexture::Type type, GSTexture::Format format)
{
	switch (type)
	{
		case GSTexture::Type::RenderTarget:
			switch (format)
			{
				case GSTexture::Format::Color:
					return TextureLabel::ColorRT;
				case GSTexture::Format::ColorHQ:
					return TextureLabel::ColorHQRT;
				case GSTexture::Format::ColorHDR:
					return TextureLabel::ColorHDRRT;
				case GSTexture::Format::ColorClip:
					return TextureLabel::ColorClipRT;
				case GSTexture::Format::UInt16:
					return TextureLabel::U16RT;
				case GSTexture::Format::UInt32:
					return TextureLabel::U32RT;
				case GSTexture::Format::PrimID:
					return TextureLabel::PrimIDTexture;
				default:
					return TextureLabel::Other;
			}
		case GSTexture::Type::Texture:
			switch (format)
			{
				case GSTexture::Format::Color:
					return TextureLabel::Texture;
				case GSTexture::Format::UNorm8:
					return TextureLabel::CLUTTexture;
				case GSTexture::Format::BC1:
				case GSTexture::Format::BC2:
				case GSTexture::Format::BC3:
				case GSTexture::Format::BC7:
				case GSTexture::Format::ColorHDR:
					return TextureLabel::ReplacementTexture;
				default:
					return TextureLabel::Other;
			}
		case GSTexture::Type::DepthStencil:
			return TextureLabel::DepthStencil;
		case GSTexture::Type::RWTexture:
			return TextureLabel::RWTexture;
		case GSTexture::Type::Invalid:
		default:
			return TextureLabel::Other;
	}
}

// Debug names
static const char* TextureLabelString(TextureLabel label)
{
	switch (label)
	{
		case TextureLabel::ColorRT:
			return "Color RT";
		case TextureLabel::ColorHQRT:
			return "Color HQ RT";
		case TextureLabel::ColorHDRRT:
			return "Color HDR RT";
		case TextureLabel::ColorClipRT:
			return "Color Clip RT";
		case TextureLabel::U16RT:
			return "U16 RT";
		case TextureLabel::U32RT:
			return "U32 RT";
		case TextureLabel::DepthStencil:
			return "Depth Stencil";
		case TextureLabel::PrimIDTexture:
			return "PrimID";
		case TextureLabel::RWTexture:
			return "RW Texture";
		case TextureLabel::CLUTTexture:
			return "CLUT Texture";
		case TextureLabel::Texture:
			return "Texture";
		case TextureLabel::ReplacementTexture:
			return "Replacement Texture";
		case TextureLabel::Other:
		default:
			return "Unknown Texture";
	}
}

#endif

std::unique_ptr<GSDevice> g_gs_device;

GSDevice::GSDevice()
{
#ifdef PCSX2_DEVBUILD
	s_texture_counts.fill(0);
#endif
}

GSDevice::~GSDevice()
{
	// should've been cleaned up in Destroy()
	pxAssert(m_pool[0].empty() && m_pool[1].empty() && !m_merge && !m_weavebob && !m_blend && !m_mad && !m_target_tmp && !m_cas);
}

const char* GSDevice::RenderAPIToString(RenderAPI api)
{
	switch (api)
	{
		// clang-format off
#define CASE(x) case RenderAPI::x: return #x
		CASE(None);
		CASE(D3D11);
		CASE(D3D12);
		CASE(Metal);
		CASE(Vulkan);
		CASE(OpenGL);
#undef CASE
		// clang-format on
	default:
		return "Unknown";
	}
}

bool GSDevice::GetRequestedExclusiveFullscreenMode(u32* width, u32* height, float* refresh_rate)
{
	const std::string mode = Host::GetStringSettingValue("EmuCore/GS", "FullscreenMode", "");
	if (!mode.empty())
	{
		const std::string_view mode_view = mode;
		std::string_view::size_type sep1 = mode.find('x');
		if (sep1 != std::string_view::npos)
		{
			std::optional<u32> owidth = StringUtil::FromChars<u32>(mode_view.substr(0, sep1));
			sep1++;

			while (sep1 < mode.length() && std::isspace(mode[sep1]))
				sep1++;

			if (owidth.has_value() && sep1 < mode.length())
			{
				std::string_view::size_type sep2 = mode.find('@', sep1);
				if (sep2 != std::string_view::npos)
				{
					std::optional<u32> oheight = StringUtil::FromChars<u32>(mode_view.substr(sep1, sep2 - sep1));
					sep2++;

					while (sep2 < mode.length() && std::isspace(mode[sep2]))
						sep2++;

					if (oheight.has_value() && sep2 < mode.length())
					{
						std::optional<float> orefresh_rate = StringUtil::FromChars<float>(mode_view.substr(sep2));
						if (orefresh_rate.has_value())
						{
							*width = owidth.value();
							*height = oheight.value();
							*refresh_rate = orefresh_rate.value();
							return true;
						}
					}
				}
			}
		}
	}

	*width = 0;
	*height = 0;
	*refresh_rate = 0;
	return false;
}

std::string GSDevice::GetFullscreenModeString(u32 width, u32 height, float refresh_rate)
{
	return StringUtil::StdStringFromFormat("%u x %u @ %f Hz", width, height, refresh_rate);
}

void GSDevice::GenerateExpansionIndexBuffer(void* buffer)
{
	static constexpr u32 MAX_INDEX = EXPAND_BUFFER_SIZE / 6 / sizeof(u16);

	u16* idx_buffer = static_cast<u16*>(buffer);
	for (u32 i = 0; i < MAX_INDEX; i++)
	{
		const u32 base = i * 4;
		*(idx_buffer++) = base + 0;
		*(idx_buffer++) = base + 1;
		*(idx_buffer++) = base + 2;
		*(idx_buffer++) = base + 1;
		*(idx_buffer++) = base + 2;
		*(idx_buffer++) = base + 3;
	}
}

GSVector4i GSDevice::ProcessCopyArea(const GSVector4i& rtsize, const GSVector4i& drawarea)
{
	GSVector4i snapped_drawarea(drawarea);
	// We don't want the snapped box adjustments when the rect is empty as it might make the copy to pass.
	// The empty rect itself needs to be handled in renderer properly.
	if (snapped_drawarea.rempty())
		return snapped_drawarea;

	// If copy area exceeds 95% coverage then we can do a full copy instead which should be faster.
	const float rt_area = static_cast<float>(rtsize.width() * rtsize.height());
	const float copy_area = static_cast<float>(drawarea.width() * drawarea.height());
	constexpr float coverage = 0.95f;
	if ((copy_area / rt_area) >= coverage)
	{
		snapped_drawarea = rtsize;
		return snapped_drawarea;
	}

	// Aligning bbox to 4 pixel boundaries so copies will be faster using Direct Memory Access,
	// otherwise it may stall as more commands need to be issued.
	snapped_drawarea = snapped_drawarea.ralign<Align_Outside>(GSVector2i(4, 4)).rintersect(rtsize);

	return snapped_drawarea;
}

std::optional<std::string> GSDevice::ReadShaderSource(const char* filename)
{
	return FileSystem::ReadFileToString(Path::Combine(EmuFolders::Resources, filename).c_str());
}

int GSDevice::GetMipmapLevelsForSize(int width, int height)
{
	return std::min(static_cast<int>(std::log2(std::max(width, height))) + 1, MAXIMUM_TEXTURE_MIPMAP_LEVELS);
}

bool GSDevice::Create(GSVSyncMode vsync_mode, bool allow_present_throttle)
{
	m_vsync_mode = vsync_mode;
	m_allow_present_throttle = allow_present_throttle;
	return true;
}

void GSDevice::Destroy()
{
	ClearCurrent();
	PurgePool();
}

bool GSDevice::AcquireWindow(bool recreate_window)
{
	std::optional<WindowInfo> wi = Host::AcquireRenderWindow(recreate_window);
	if (!wi.has_value())
	{
		Console.Error("Failed to acquire render window.");
		Host::ReportErrorAsync("Error", "Failed to acquire render window. The log may have more information.");
		return false;
	}

	m_window_info = std::move(wi.value());
	return true;
}

bool GSDevice::ShouldSkipPresentingFrame()
{
	// Only needed with FIFO.
	if (!m_allow_present_throttle || m_vsync_mode != GSVSyncMode::FIFO)
		return false;

	const float throttle_rate = (m_window_info.surface_refresh_rate > 0.0f) ? m_window_info.surface_refresh_rate : 60.0f;
	const u64 throttle_period = static_cast<u64>(static_cast<double>(GetTickFrequency()) / static_cast<double>(throttle_rate));

	const u64 now = GetCPUTicks();
	const double diff = now - m_last_frame_displayed_time;
	if (diff < throttle_period)
		return true;

	m_last_frame_displayed_time = now;
	return false;
}

void GSDevice::ThrottlePresentation()
{
	// Manually throttle presentation when vsync isn't enabled, so we don't try to render the
	// fullscreen UI at thousands of FPS and make the gpu go brrrrrrrr.
	const float throttle_rate = (m_window_info.surface_refresh_rate > 0.0f) ? m_window_info.surface_refresh_rate : 60.0f;

	const u64 sleep_period = static_cast<u64>(static_cast<double>(GetTickFrequency()) / static_cast<double>(throttle_rate));
	const u64 current_ts = GetCPUTicks();

	// Allow it to fall behind/run ahead up to 2*period. Sleep isn't that precise, plus we need to
	// allow time for the actual rendering.
	const u64 max_variance = sleep_period * 2;
	if (static_cast<u64>(std::abs(static_cast<s64>(current_ts - m_last_frame_displayed_time))) > max_variance)
		m_last_frame_displayed_time = current_ts + sleep_period;
	else
		m_last_frame_displayed_time += sleep_period;

	Threading::SleepUntil(m_last_frame_displayed_time);
}

void GSDevice::ClearRenderTarget(GSTexture* t, u32 c)
{
	t->SetClearColor(c);
#ifdef PCSX2_TRACE_ONLY
	GSRecordTextureTraceRenderTargetWrite(t, 0);
#endif
}

void GSDevice::ClearDepth(GSTexture* t, float d)
{
	t->SetClearDepth(d);
}

bool GSDevice::ProcessClearsBeforeCopy(GSTexture* sTex, GSTexture* dTex, const bool full_copy)
{
	pxAssert(sTex->GetState() == GSTexture::State::Cleared && dTex->IsRenderTargetOrDepthStencil());

	// Pass it forward if we're clearing the whole thing.
	if (full_copy)
	{
		if (dTex->IsDepthStencil())
			dTex->SetClearDepth(sTex->GetClearDepth());
		else
			dTex->SetClearColor(sTex->GetClearColor());

		dTex->SetState(GSTexture::State::Cleared);

		return true;
	}

	// Destination is cleared, if it's the same colour and rect, we can just avoid this entirely.
	if (dTex->GetState() == GSTexture::State::Cleared)
	{
		if (dTex->IsDepthStencil())
		{
			if (dTex->GetClearDepth() == sTex->GetClearDepth())
				return true;
		}
		else
		{
			if (dTex->GetClearColor() == sTex->GetClearColor())
				return true;
		}
	}

	return false;
}

void GSDevice::InvalidateRenderTarget(GSTexture* t)
{
	t->SetState(GSTexture::State::Invalidated);
}

void GSDevice::UpdateImGuiTextures()
{
#ifdef PCSX2_TRACE_ONLY
	return;
#else
	// TODO, use ImDrawData https://github.com/ocornut/imgui/issues/8597#issuecomment-2871835598
	for (ImTextureData* im_tex : ImGui::GetPlatformIO().Textures)
	{
		switch (im_tex->Status)
		{
			case ImTextureStatus_OK:
			case ImTextureStatus_Destroyed:
				continue;
			case ImTextureStatus_WantCreate:
			{
				GSTexture* gs_tex = g_gs_device->CreateTexture(im_tex->Width, im_tex->Height, 1, GSTexture::Format::Color);
				if (!gs_tex)
					pxFailRel("Failed to create ImGui texture");

				im_tex->SetTexID(reinterpret_cast<ImTextureID>(gs_tex->GetNativeHandle()));
				im_tex->BackendUserData = gs_tex;
				[[fallthrough]];
			}
			case ImTextureStatus_WantUpdates:
			{
				// If we fell through from WantCreate, then we are uploading the full size
				// Otherwise, we are just updating the specified region
				// clange-format off
				const int upload_x = (im_tex->Status == ImTextureStatus_WantCreate) ? 0 : im_tex->UpdateRect.x;
				const int upload_y = (im_tex->Status == ImTextureStatus_WantCreate) ? 0 : im_tex->UpdateRect.y;
				const int upload_w = (im_tex->Status == ImTextureStatus_WantCreate) ? im_tex->Width : im_tex->UpdateRect.w;
				const int upload_h = (im_tex->Status == ImTextureStatus_WantCreate) ? im_tex->Height : im_tex->UpdateRect.h;
				const int upload_pitch = upload_w * im_tex->BytesPerPixel;
				// clange-format on

				const GSVector4i rect{
					upload_x,
					upload_y,
					upload_x + upload_w,
					upload_y + upload_h,
				};

				GSTexture* gs_tex = static_cast<GSTexture*>(im_tex->BackendUserData);
				GSTexture::GSMap map;
				if (gs_tex->Map(map, &rect))
				{
					for (int y = 0; y < upload_h; y++)
						std::memcpy(map.bits + map.pitch * y, im_tex->GetPixelsAt(rect.x, rect.y + y), upload_pitch);

					gs_tex->Unmap();
				}
				else
				{
					for (int y = 0; y < upload_h; y++)
						gs_tex->Update({rect.left, rect.top + y, rect.right, rect.top + y + 1},
							im_tex->GetPixelsAt(rect.x, rect.y + y), upload_pitch);
				}

				im_tex->Status = ImTextureStatus_OK;
				break;
			}
			case ImTextureStatus_WantDestroy:
			{
				GSTexture* gs_tex = static_cast<GSTexture*>(im_tex->BackendUserData);
				if (gs_tex == nullptr)
					break;

				// While it's unlikely we're going to reuse the same size as imgui for rendering,
				// imgui may request a new atlas of the same size if old font sizes are evicted.
				Recycle(gs_tex);

				im_tex->SetTexID(ImTextureID_Invalid);
				im_tex->BackendUserData = nullptr;
				im_tex->Status = ImTextureStatus_Destroyed;
				break;
			}
			default:
				pxAssert(false);
				break;
			}
	}
#endif
}

void GSDevice::DestroyImGuiTextures()
{
#ifdef PCSX2_TRACE_ONLY
	return;
#else
	if (!ImGui::GetCurrentContext())
		return;

	for (ImTextureData* im_tex : ImGui::GetPlatformIO().Textures)
	{
		if (im_tex->Status != ImTextureStatus_Destroyed)
		{
			GSTexture* gs_tex = static_cast<GSTexture*>(im_tex->BackendUserData);
			if (gs_tex == nullptr)
				continue;

			Recycle(gs_tex);

			pxAssert(im_tex->RefCount == 1);

			im_tex->SetTexID(ImTextureID_Invalid);
			im_tex->BackendUserData = nullptr;
			im_tex->Status = ImTextureStatus_Destroyed;
		}
	}
#endif
}

void GSDevice::TextureRecycleDeleter::operator()(GSTexture* const tex)
{
	g_gs_device->Recycle(tex);
}

GSTexture* GSDevice::FetchSurface(GSTexture::Type type, int width, int height, int levels, GSTexture::Format format, bool clear, bool prefer_unused_texture)
{
	const GSVector2i size(std::clamp(width, 1, static_cast<int>(g_gs_device->GetMaxTextureSize())),
		std::clamp(height, 1, static_cast<int>(g_gs_device->GetMaxTextureSize())));
	FastList<GSTexture*>& pool = m_pool[type != GSTexture::Type::Texture];

	GSTexture* t = nullptr;
	auto fallback = pool.end();

	for (auto i = pool.begin(); i != pool.end(); ++i)
	{
		t = *i;

		pxAssert(t);

		if (t->GetType() == type && t->GetFormat() == format && t->GetSize() == size && t->GetMipmapLevels() == levels)
		{
			if (!prefer_unused_texture || t->GetLastFrameUsed() != m_frame)
			{
				m_pool_memory_usage -= t->GetMemUsage();
				pool.erase(i);
				break;
			}
			else if (fallback == pool.end())
			{
				fallback = i;
			}
		}

		t = nullptr;
	}

	if (!t)
	{
		if (pool.size() >= ((type == GSTexture::Type::Texture) ? MAX_POOLED_TEXTURES : MAX_POOLED_TARGETS) &&
			fallback != pool.end())
		{
			t = *fallback;
			m_pool_memory_usage -= t->GetMemUsage();
			pool.erase(fallback);
		}
		else
		{
			t = CreateSurface(type, size.x, size.y, levels, format);
			if (!t)
			{
				ERROR_LOG("GS: Memory allocation failure for {}x{} texture. Purging pool and retrying.", size.x, size.y);
				PurgePool();
				t = CreateSurface(type, size.x, size.y, levels, format);
				if (!t)
				{
					ERROR_LOG("GS: Memory allocation failure for {}x{} texture after purging pool.", size.x, size.y);
					return nullptr;
				}
			}

#ifdef PCSX2_DEVBUILD
			if (GSConfig.UseDebugDevice)
			{
				const TextureLabel label = GetTextureLabel(type, format);
				const u32 id = ++s_texture_counts[static_cast<u32>(label)];
				t->SetDebugName(TinyString::from_format("{} {}", TextureLabelString(label), id));
			}
#endif
		}
	}

	switch (type)
	{
	case GSTexture::Type::RenderTarget:
		{
			if (clear)
				ClearRenderTarget(t, 0);
			else
				InvalidateRenderTarget(t);
		}
		break;
	case GSTexture::Type::DepthStencil:
		{
			if (clear)
				ClearDepth(t, 0.0f);
			else
				InvalidateRenderTarget(t);
		}
		break;
	default:
		break;
	}

	return t;
}

void GSDevice::Recycle(GSTexture* t)
{
	if (!t)
		return;

	t->SetLastFrameUsed(m_frame);

	t->ClearUnorderedAccess();
	
#ifdef PCSX2_DEVBUILD
	t->SetDebugName("");
#endif

	FastList<GSTexture*>& pool = m_pool[!t->IsTexture()];
	pool.push_front(t);
	m_pool_memory_usage += t->GetMemUsage();

	const u32 max_size = t->IsTexture() ? MAX_POOLED_TEXTURES : MAX_POOLED_TARGETS;
	const u32 max_age = t->IsTexture() ? MAX_TEXTURE_AGE : MAX_TARGET_AGE;
	while (pool.size() > max_size)
	{
		// Don't toss when the texture was last used in this frame.
		// Because we're going to need to keep it alive anyway.
		GSTexture* back = pool.back();
		if ((m_frame - back->GetLastFrameUsed()) < max_age)
			break;

		m_pool_memory_usage -= back->GetMemUsage();
		delete back;

		pool.pop_back();
	}
}

bool GSDevice::UsesLowerLeftOrigin() const
{
	const RenderAPI api = GetRenderAPI();
	return (api == RenderAPI::OpenGL);
}

void GSDevice::AgePool()
{
	m_frame++;

	// Toss out textures when they're not too-recently used.
	for (u32 pool_idx = 0; pool_idx < m_pool.size(); pool_idx++)
	{
		const u32 max_age = (pool_idx == 0) ? MAX_TEXTURE_AGE : MAX_TARGET_AGE;
		FastList<GSTexture*>& pool = m_pool[pool_idx];
		while (!pool.empty())
		{
			GSTexture* back = pool.back();
			if ((m_frame - back->GetLastFrameUsed()) < max_age)
				break;

			m_pool_memory_usage -= back->GetMemUsage();
			delete back;

			pool.pop_back();
		}
	}
}

void GSDevice::PurgePool()
{
	for (FastList<GSTexture*>& pool : m_pool)
	{
		for (GSTexture* t : pool)
			delete t;
		pool.clear();
	}
	m_pool_memory_usage = 0;
}

GSTexture* GSDevice::CreateRenderTarget(int w, int h, GSTexture::Format format, bool clear, bool prefer_reuse)
{
	return FetchSurface(GSTexture::Type::RenderTarget, w, h, 1, format, clear, !prefer_reuse);
}

GSTexture* GSDevice::CreateRenderTarget(const GSVector2i& size, GSTexture::Format format, bool clear, bool prefer_reuse)
{
	return FetchSurface(GSTexture::Type::RenderTarget, size.x, size.y, 1, format, clear, !prefer_reuse);
}

GSTexture* GSDevice::CreateDepthStencil(int w, int h, bool clear, bool prefer_reuse)
{
	return FetchSurface(GSTexture::Type::DepthStencil, w, h, 1, GSTexture::Format::DepthStencil,
		clear, !prefer_reuse);
}

GSTexture* GSDevice::CreateDepthStencil(const GSVector2i& size, bool clear, bool prefer_reuse)
{
	return FetchSurface(GSTexture::Type::DepthStencil, size.x, size.y, 1, GSTexture::Format::DepthStencil,
		clear, !prefer_reuse);
}

GSTexture* GSDevice::CreateDepthColor(int w, int h, bool clear, bool prefer_reuse)
{
	return FetchSurface(GSTexture::Type::RenderTarget, w, h, 1, GSTexture::Format::DepthColor,
		clear, !prefer_reuse);
}

GSTexture* GSDevice::CreateDepthColor(const GSVector2i& size, bool clear, bool prefer_reuse)
{
	return FetchSurface(GSTexture::Type::RenderTarget, size.x, size.y, 1, GSTexture::Format::DepthColor,
		clear, !prefer_reuse);
}

GSTexture* GSDevice::CreateTexture(int w, int h, int mipmap_levels, GSTexture::Format format, bool prefer_reuse /* = false */)
{
	pxAssert(mipmap_levels != 0 && (mipmap_levels < 0 || mipmap_levels <= GetMipmapLevelsForSize(w, h)));
	const int levels = mipmap_levels < 0 ? GetMipmapLevelsForSize(w, h) : mipmap_levels;
	return FetchSurface(GSTexture::Type::Texture, w, h, levels, format, false, m_features.prefer_new_textures && !prefer_reuse);
}

GSTexture* GSDevice::CreateTexture(const GSVector2i& size, int mipmap_levels, GSTexture::Format format, bool prefer_reuse /* = false */)
{
	return CreateTexture(size.x, size.y, mipmap_levels, format, prefer_reuse);
}

GSTexture* GSDevice::CreateCompatible(GSTexture* tex, bool clear, bool prefer_reuse)
{
	return CreateCompatible(tex, tex->GetWidth(), tex->GetHeight(), clear, prefer_reuse);
}

GSTexture* GSDevice::CreateCompatible(GSTexture* tex, const GSVector2i& size, bool clear, bool prefer_reuse)
{
	return CreateCompatible(tex, size.x, size.y, clear, prefer_reuse);
}

GSTexture* GSDevice::CreateCompatible(GSTexture* tex, int w, int h, bool clear, bool prefer_reuse)
{
	return FetchSurface(tex->GetType(), w, h, 1, tex->GetFormat(), clear, !prefer_reuse);
}

void GSDevice::DoStretchRectWithAssertions(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex,
	const GSVector4& dRect, ShaderConvertSelector shader, Filter filter)
{
	pxAssert((dTex && dTex->IsDepthLike()) == shader.Float32Output());
	pxAssert(!(filter == Biln && shader.SupportsBilinear())); // Don't allow HW bilinear if SW bilinear is required.
	GL_INS("StretchRect(%s) {%d,%d} %dx%d -> {%d,%d) %dx%d", ShaderConvertName(shader.Shader()),
		int(sRect.left), int(sRect.top),
		int(sRect.right - sRect.left), int(sRect.bottom - sRect.top), int(dRect.left), int(dRect.top),
		int(dRect.right - dRect.left), int(dRect.bottom - dRect.top));
	DoStretchRect(sTex, sRect, dTex, dRect, shader, filter);
}

void GSDevice::StretchRect(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex, const GSVector4& dRect,
	ShaderConvertSelector shader, Filter filter)
{
	DoStretchRectWithAssertions(sTex, sRect, dTex, dRect, shader, filter);
#ifdef PCSX2_TRACE_ONLY
	GSRecordTextureTraceRenderTargetWrite(dTex, 0);
#endif
}

void GSDevice::StretchRect(GSTexture* sTex, GSTexture* dTex, const GSVector4& dRect, ShaderConvertSelector shader, Filter filter)
{
	StretchRect(sTex, GSVector4(0, 0, 1, 1), dTex, dRect, shader, filter);
}

void GSDevice::StretchRect(GSTexture* sTex, GSTexture* dTex, ShaderConvertSelector shader, Filter filter)
{
	StretchRect(sTex, dTex, GSVector4(dTex->GetRect()), shader, filter);
}

void GSDevice::StretchRectAuto(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex, const GSVector4& dRect,
	Filter filter, u32 src_bpp, u32 dst_bpp)
{
	ShaderConvertSelector shader = GetConvertShader(sTex, dTex, src_bpp, dst_bpp);
	if (shader.SupportsBilinear() && filter == Biln)
	{
		// Bilinear is emulated in the shader.
		shader.SetFilter(Biln);
		filter = Nearest;
	}
	StretchRect(sTex, sRect, dTex, dRect, shader, filter);
}

void GSDevice::StretchRectAuto(GSTexture* sTex, GSTexture* dTex, const GSVector4& dRect, Filter filter, u32 src_bpp, u32 dst_bpp)
{
	StretchRectAuto(sTex, GSVector4(0, 0, 1, 1), dTex, dRect, filter, src_bpp, dst_bpp);
}

void GSDevice::StretchRectAuto(GSTexture* sTex, GSTexture* dTex, Filter filter, u32 src_bpp, u32 dst_bpp)
{
	StretchRectAuto(sTex, dTex, GSVector4(dTex->GetRect()), filter, src_bpp, dst_bpp);
}

void GSDevice::StretchRectAutoMask(GSTexture* sTex, const GSVector4& sRect, GSTexture* dTex, const GSVector4& dRect,
	bool red, bool green, bool blue, bool alpha, u32 src_bpp, u32 dst_bpp)
{
	StretchRect(sTex, sRect, dTex, dRect, GetConvertShaderMask(sTex, dTex, src_bpp, dst_bpp, red, green, blue, alpha), Nearest);
}

void GSDevice::StretchRectAutoMask(GSTexture* sTex, GSTexture* dTex, const GSVector4& dRect, bool red, bool green, bool blue, bool alpha, u32 src_bpp, u32 dst_bpp)
{
	StretchRectAutoMask(sTex, GSVector4(0, 0, 1, 1), dTex, dRect, red, green, blue, alpha, src_bpp, dst_bpp);
}

void GSDevice::StretchRectAutoMask(GSTexture* sTex, GSTexture* dTex, bool red, bool green, bool blue, bool alpha,
	u32 src_bpp, u32 dst_bpp)
{
	StretchRectAutoMask(sTex, dTex, GSVector4(dTex->GetRect()), red, green, blue, alpha, src_bpp, dst_bpp);
}

void GSDevice::DrawMultiStretchRects(
	const MultiStretchRect* rects, u32 num_rects, GSTexture* dTex, ShaderConvertSelector shader)
{
	for (u32 i = 0; i < num_rects; i++)
	{
		const MultiStretchRect& sr = rects[i];
		g_gs_device->StretchRect(sr.src, sr.src_rect, dTex, sr.dst_rect,
			shader.SetMask(rects[0].wmask.wrgba).SetFilter(sr.filter), sr.filter);
	}
}

void GSDevice::SortMultiStretchRects(MultiStretchRect* rects, u32 num_rects)
{
	// Depending on num_rects, insertion sort may be better here.
	std::sort(rects, rects + num_rects, [](const MultiStretchRect& lhs, const MultiStretchRect& rhs) {
		return lhs.src < rhs.src || lhs.filter < rhs.filter;
	});
}

void GSDevice::ClearCurrent()
{
	m_current = nullptr;

	delete m_merge;
	delete m_weavebob;
	delete m_blend;
	delete m_mad;
	delete m_target_tmp;
	delete m_cas;

	m_merge = nullptr;
	m_weavebob = nullptr;
	m_blend = nullptr;
	m_mad = nullptr;
	m_target_tmp = nullptr;
	m_cas = nullptr;
}

void GSDevice::Merge(GSTexture* sTex[3], GSVector4* sRect, GSVector4* dRect, const GSVector2i& fs, const GSRegPMODE& PMODE, const GSRegEXTBUF& EXTBUF, u32 c)
{
	if (ResizeRenderTarget(&m_merge, fs.x, fs.y, false, false))
		DoMerge(sTex, sRect, m_merge, dRect, PMODE, EXTBUF, c, BilnIf(GSConfig.PCRTCOffsets));

	m_current = m_merge;
}

void GSDevice::Interlace(const GSVector2i& ds, int field, int mode, float yoffset)
{
	static int bufIdx = 0;
	float offset = yoffset * static_cast<float>(field);
	offset = GSConfig.DisableInterlaceOffset ? 0.0f : offset;

	auto do_interlace = [this](GSTexture* sTex, GSTexture* dTex, ShaderInterlace shader, Filter filter, float yoffset, int bufIdx) {
		const GSVector2i ds_i = dTex->GetSize();
		const GSVector2 ds = GSVector2(static_cast<float>(ds_i.x), static_cast<float>(ds_i.y));

		GSVector4 sRect = GSVector4(0.0f, 0.0f, 1.0f, 1.0f);
		GSVector4 dRect = GSVector4(0.0f, yoffset, ds.x, ds.y + yoffset);

		// Select the top or bottom half for MAD buffering.
		if (shader == ShaderInterlace::MAD_BUFFER)
		{
			const float half_size = ds.y * 0.5f;
			if ((bufIdx >> 1) == 1)
				dRect.y += half_size;
			else
				dRect.w -= half_size;
		}

		const InterlaceConstantBuffer cb = {
			GSVector4(static_cast<float>(bufIdx), 1.0f / ds.y, ds.y, MAD_SENSITIVITY)
		};

		GL_PUSH("DoInterlace %dx%d Shader:%d Filter:%d", ds_i.x, ds_i.y, static_cast<int>(shader), filter);
		DoInterlace(sTex, sRect, dTex, dRect, shader, filter, cb);
	};

	switch (mode)
	{
		case 0: // Weave
			ResizeRenderTarget(&m_weavebob, ds.x, ds.y, true, false);
			do_interlace(m_merge, m_weavebob, ShaderInterlace::WEAVE, Nearest, offset, field);
			m_current = m_weavebob;
			break;
		case 1: // Bob
			// Field is reversed here as we are countering the bounce.
			ResizeRenderTarget(&m_weavebob, ds.x, ds.y, true, false);
			do_interlace(m_merge, m_weavebob, ShaderInterlace::BOB, Biln, yoffset * (1 - field), 0);
			m_current = m_weavebob;
			break;
		case 2: // Blend
			ResizeRenderTarget(&m_weavebob, ds.x, ds.y, true, false);
			do_interlace(m_merge, m_weavebob, ShaderInterlace::WEAVE, Nearest, offset, field);
			ResizeRenderTarget(&m_blend, ds.x, ds.y, true, false);
			do_interlace(m_weavebob, m_blend, ShaderInterlace::BLEND, Biln, 0, 0);
			m_current = m_blend;
			break;
		case 3: // FastMAD Motion Adaptive Deinterlacing
			bufIdx++;
			bufIdx &= ~1;
			bufIdx |= field;
			bufIdx &= 3;
			ResizeRenderTarget(&m_mad, ds.x, ds.y * 2.0f, true, false);
			do_interlace(m_merge, m_mad, ShaderInterlace::MAD_BUFFER, Nearest, offset, bufIdx);
			ResizeRenderTarget(&m_weavebob, ds.x, ds.y, true, false);
			do_interlace(m_mad, m_weavebob, ShaderInterlace::MAD_RECONSTRUCT, Nearest, 0, bufIdx);
			m_current = m_weavebob;
			break;
		default:
			m_current = m_merge;
			break;
	}
}

void GSDevice::FXAA()
{
	// Combining FXAA+ShadeBoost can't share the same target.
	GSTexture*& dTex = (m_current == m_target_tmp) ? m_merge : m_target_tmp;
	if (ResizeRenderTarget(&dTex, m_current->GetWidth(), m_current->GetHeight(), false, false))
	{
		DoFXAA(m_current, dTex);
		m_current = dTex;
	}
}

void GSDevice::ShadeBoost()
{
	if (ResizeRenderTarget(&m_target_tmp, m_current->GetWidth(), m_current->GetHeight(), false, false))
	{
		// predivide to avoid the divide (multiply) in the shader
		const GSVector4 params(
			static_cast<float>(GSConfig.ShadeBoost_Brightness) * (1.0f / 50.0f),
			static_cast<float>(GSConfig.ShadeBoost_Contrast) * (1.0f / 50.0f),
			static_cast<float>(GSConfig.ShadeBoost_Saturation) * (1.0f / 50.0f),
			static_cast<float>(GSConfig.ShadeBoost_Gamma) * (1.0f / 50.0f));

		DoShadeBoost(m_current, m_target_tmp, params.v);

		m_current = m_target_tmp;
	}
}

void GSDevice::Resize(int width, int height)
{
	GSTexture*& dTex = (m_current == m_target_tmp) ? m_merge : m_target_tmp;
	GSVector2i s = m_current->GetSize();
	int multiplier = 1;

	if ((width > s.x || height > s.y))
	{
		while (width > s.x || height > s.y)
		{
			s = m_current->GetSize() * GSVector2i(++multiplier);
		}
	}
	else
		s = GSVector2i(width, height);

	if (ResizeRenderTarget(&dTex, s.x, s.y, false, false))
	{
		const GSVector4 sRect(0, 0, 1, 1);
		const GSVector4 dRect(0, 0, s.x, s.y);
		StretchRectAuto(m_current, sRect, dTex, dRect, Nearest);
		m_current = dTex;
	}
}

bool GSDevice::ResizeRenderTarget(GSTexture** t, int w, int h, bool preserve_contents, bool recycle)
{
	pxAssert(t);

	GSTexture* orig_tex = *t;
	if (orig_tex && orig_tex->GetWidth() == w && orig_tex->GetHeight() == h)
	{
		if (!preserve_contents)
			InvalidateRenderTarget(orig_tex);

		return true;
	}

	const GSTexture::Format fmt = orig_tex ? orig_tex->GetFormat() : GSTexture::Format::Color;
	const bool really_preserve_contents = (preserve_contents && orig_tex);
	GSTexture* new_tex = FetchSurface(GSTexture::Type::RenderTarget, w, h, 1, fmt, !really_preserve_contents, true);
	if (!new_tex)
	{
		Console.WriteLn("%dx%d texture allocation failed in ResizeTexture()", w, h);
		return false;
	}

	if (really_preserve_contents)
	{
		constexpr GSVector4 sRect = GSVector4::cxpr(0, 0, 1, 1);
		const GSVector4 dRect = GSVector4(orig_tex->GetRect());
		StretchRect(orig_tex, sRect, new_tex, dRect, ShaderConvert::COPY, Biln);
	}

	if (orig_tex)
	{
		if (recycle)
			Recycle(orig_tex);
		else
			delete orig_tex;
	}

	*t = new_tex;
	return true;
}

void GSDevice::BeginDSAsRT(GSTexture* ds, const GSVector4i& drawarea)
{
	// Create a temporary RT and copy the area needed for the draw.
	const int w = ds->GetWidth();
	const int h = ds->GetHeight();
	m_ds_as_rt = g_gs_device->CreateRenderTarget(w, h, GSTexture::Format::DepthColor, false, true);
	const GSVector4 dRect(drawarea);
	const GSVector4 sRect(dRect.x / w, dRect.y / h, dRect.z / w, dRect.w / h);
	StretchRectAuto(ds, sRect, m_ds_as_rt, dRect, Nearest);
}

void GSDevice::EndDSAsRT()
{
	Recycle(m_ds_as_rt);
	m_ds_as_rt = nullptr;
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wignored-qualifiers"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#endif

// Kinda grotty, but better than copy/pasting the relevant bits in..
#define A_CPU 1
#include "bin/resources/shaders/common/ffx_a.h"
#include "bin/resources/shaders/common/ffx_cas.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

bool GSDevice::GetCASShaderSource(std::string* source)
{
	std::optional<std::string> ffx_a_source = ReadShaderSource("shaders/common/ffx_a.h");
	std::optional<std::string> ffx_cas_source = ReadShaderSource("shaders/common/ffx_cas.h");
	if (!ffx_a_source.has_value() || !ffx_cas_source.has_value())
		return false;

	// Since our shader compilers don't support includes, and OpenGL doesn't at all... we'll do a really cheeky string replace.
	StringUtil::ReplaceAll(source, "#include \"ffx_a.h\"", ffx_a_source.value());
	StringUtil::ReplaceAll(source, "#include \"ffx_cas.h\"", ffx_cas_source.value());
	return true;
}

void GSDevice::CAS(GSTexture*& tex, GSVector4i& src_rect, GSVector4& src_uv, const GSVector4& draw_rect, bool sharpen_only)
{
	const int dst_width = sharpen_only ? src_rect.width() : static_cast<int>(std::ceil(draw_rect.z - draw_rect.x));
	const int dst_height = sharpen_only ? src_rect.height() : static_cast<int>(std::ceil(draw_rect.w - draw_rect.y));
	const int src_offset_x = static_cast<int>(src_rect.x);
	const int src_offset_y = static_cast<int>(src_rect.y);

	GSTexture* src_tex = tex;
	if (!m_cas || m_cas->GetWidth() != dst_width || m_cas->GetHeight() != dst_height)
	{
		delete m_cas;
		m_cas = CreateSurface(GSTexture::Type::RWTexture, dst_width, dst_height, 1, GSTexture::Format::Color);
		if (!m_cas)
		{
			Console.Error("Failed to allocate CAS RW texture.");
			return;
		}
	}

	std::array<u32, NUM_CAS_CONSTANTS> consts;
	CasSetup(&consts[0], &consts[4], static_cast<float>(GSConfig.CAS_Sharpness) * 0.01f,
		static_cast<AF1>(src_rect.width()), static_cast<AF1>(src_rect.height()),
		static_cast<AF1>(dst_width), static_cast<AF1>(dst_height));
	consts[8] = static_cast<u32>(src_offset_x);
	consts[9] = static_cast<u32>(src_offset_y);

	if (!DoCAS(src_tex, m_cas, sharpen_only, consts))
	{
		// leave textures intact if we failed
		Console.Warning("Applying CAS failed.");
		return;
	}

	tex = m_cas;
	src_rect = GSVector4i(0, 0, dst_width, dst_height);
	src_uv = GSVector4(0.0f, 0.0f, 1.0f, 1.0f);
}

bool GSHWDrawConfig::BlendState::IsEffective(ColorMaskSelector colormask) const
{
	return enable && (((colormask.key & 7u) && (src_factor != GSDevice::CONST_ONE || dst_factor != GSDevice::CONST_ZERO)) ||
	                  ((colormask.key & 8u) && (src_factor_alpha != GSDevice::CONST_ONE || dst_factor_alpha != GSDevice::CONST_ZERO)));
}

struct DrawConfigWriter
{
	fmt::memory_buffer buffer;
	u32 indent = 0;
	bool beginning_of_line = true;

	/// Uses RAII to add 1 to indent on construction and remove on destruction
	struct RAIIIndent
	{
		DrawConfigWriter& writer;
		RAIIIndent(RAIIIndent&&) = delete;
		explicit RAIIIndent(DrawConfigWriter& writer): writer(writer) { writer.PushIndent(); }
		~RAIIIndent() { writer.PopIndent(); }
		operator DrawConfigWriter&() { return writer; }
	};

	void PushIndent() { indent++; }
	void PopIndent() { indent--; }
	RAIIIndent WithIndent() { return RAIIIndent(*this); }

	template <typename... T>
	FMT_INLINE void WriteLn(fmt::format_string<T...> fmt, T&&... args)
	{
		fmt::vargs<T...> va = {{args...}};
		if (beginning_of_line)
			WriteIndent();
		beginning_of_line = true;
		fmt::detail::vformat_to(buffer, fmt.str, va);
		buffer.push_back('\n');
	}

	template <typename... T>
	FMT_INLINE void Write(fmt::format_string<T...> fmt, T&&... args)
	{
		fmt::vargs<T...> va = {{args...}};
		if (beginning_of_line)
			WriteIndent();
		beginning_of_line = false;
		fmt::detail::vformat_to(buffer, fmt.str, va);
	}

private:
	void WriteIndent()
	{
		size_t sz = buffer.size();
		buffer.resize(sz + indent);
		for (u32 i = 0; i < indent; i++)
			buffer[sz + i] = '\t';
	}
};

static const char* GetTopologyName(GSHWDrawConfig::Topology topology)
{
	switch (topology)
	{
		case GSHWDrawConfig::Topology::Point:    return "Point";
		case GSHWDrawConfig::Topology::Line:     return "Line";
		case GSHWDrawConfig::Topology::Triangle: return "Triangle";
	}
	return "Unknown";
}

static const char* GetVSExpandName(GSHWDrawConfig::VSExpand vsexpand)
{
	switch (vsexpand)
	{
		case GSHWDrawConfig::VSExpand::None:        return "None";
		case GSHWDrawConfig::VSExpand::Point:       return "Point";
		case GSHWDrawConfig::VSExpand::Line:        return "Line";
		case GSHWDrawConfig::VSExpand::Sprite:      return "Sprite";
		case GSHWDrawConfig::VSExpand::LineAA1:     return "LineAA1";
		case GSHWDrawConfig::VSExpand::TriangleAA1: return "TriangleAA1";
	}
	return "Unknown";
}

static const char* GetPSDateName(u32 date)
{
	switch (date)
	{
		case 0: return "Off";
		case 1: return "PrimID Init DATM=0";
		case 2: return "PrimID Init DATM=1";
		case 3: return "PrimID Main";
		case 5: return "Barrier DATM=0";
		case 6: return "Barrier DATM=1";
	}
	return "Unknown";
}

static const char* GetPSAlphaTestName(GSShader::PS_ATST atst)
{
	using GSShader::PS_ATST;
	switch (atst)
	{
		case PS_ATST::NONE:     return "NONE";
		case PS_ATST::LEQUAL:   return "LEQUAL";
		case PS_ATST::GEQUAL:   return "GEQUAL";
		case PS_ATST::EQUAL:    return "EQUAL";
		case PS_ATST::NOTEQUAL: return "NOTEQUAL";
	};
	return "UNKNOWN";
}

static const char* GetPSAFAILName(GSShader::PS_AFAIL afail)
{
	using GSShader::PS_AFAIL;
	switch (afail)
	{
		case PS_AFAIL::KEEP:          return "KEEP";
		case PS_AFAIL::FB_ONLY:       return "FB_ONLY";
		case PS_AFAIL::ZB_ONLY:       return "ZB_ONLY";
		case PS_AFAIL::RGB_ONLY:      return "RGB_ONLY";
		case PS_AFAIL::RGB_ONLY_DSB:  return "RGB_ONLY_DSB";
		case PS_AFAIL::RGB_ONLY_SW_Z: return "RGB_ONLY_SW_Z";
	};
	return "UNKNOWN";
}

static const char* GetPSDstFmtName(u32 dstfmt)
{
	switch (dstfmt)
	{
		case 0: return "32bit";
		case 1: return "24bit";
		case 2: return "16bit";
	}
	return "Unknown";
}

static const char* GetPSDepthFmtName(u32 depthfmt)
{
	switch (depthfmt)
	{
		case 0: return "None";
		case 1: return "32bit";
		case 2: return "16bit";
		case 3: return "RGBA";
	}
	return "Unknown";
}

static const char* GetPSBlendABDName(u32 abd)
{
	switch (abd)
	{
		case 0: return "Cs";
		case 1: return "Cd";
		case 2: return "0";
	}
	return "Unknown";
}

static const char* GetPSBlendCName(u32 c)
{
	switch (c)
	{
		case 0: return "As";
		case 1: return "Ad";
		case 2: return "Af";
	}
	return "Unknown";
}

static const char* GetPSBlendHWName(HWBlendType blendhw)
{
	switch (blendhw)
	{
		case HWBlendType::SRC_ONE_DST_FACTOR:      return "SRC_ONE_DST_FACTOR";
		case HWBlendType::SRC_ALPHA_DST_FACTOR:    return "SRC_ALPHA_DST_FACTOR";
		case HWBlendType::SRC_DOUBLE:              return "SRC_DOUBLE";
		case HWBlendType::SRC_HALF_ONE_DST_FACTOR: return "SRC_HALF_ONE_DST_FACTOR";
		case HWBlendType::SRC_INV_DST_BLEND_HALF:  return "SRC_INV_DST_BLEND_HALF";
		case HWBlendType::INV_SRC_DST_BLEND_HALF:  return "INV_SRC_DST_BLEND_HALF";
	}
	return "Unknown";
}

static const char* GetPSBlendMixName(HWBlendType blendmix)
{
	switch (blendmix)
	{
		case HWBlendType::BMIX1_ALPHA_HIGH_ONE: return "BMIX1_ALPHA_HIGH_ONE";
		case HWBlendType::BMIX1_SRC_HALF:       return "BMIX1_SRC_HALF";
		case HWBlendType::BMIX2_OVERFLOW:       return "BMIX2_OVERFLOW";
		case HWBlendType::SRC_HALF_ONE_DST_FACTOR:
		case HWBlendType::SRC_INV_DST_BLEND_HALF:
		case HWBlendType::INV_SRC_DST_BLEND_HALF:
			return "INVALID (Blend Mix)";
	}
	return "Unknown";
}

static const char* GetHWBlendTypeName(HWBlendType blend, bool mix)
{
	if (static_cast<u32>(blend) == 0)
		return "Off";
	return mix ? GetPSBlendMixName(blend) : GetPSBlendHWName(blend);
}

static const char* GetPSChannelName(ChannelFetch channel)
{
	switch (channel)
	{
		case ChannelFetch_NONE:  return "None";
		case ChannelFetch_RED:   return "FetchRed";
		case ChannelFetch_GREEN: return "FetchGreen";
		case ChannelFetch_BLUE:  return "FetchBlue";
		case ChannelFetch_ALPHA: return "FetchAlpha";
		case ChannelFetch_RGB:   return "FetchRGB";
		case ChannelFetch_GXBY:  return "FetchGXBY";
	}
	return "Unknown";
}

static const char* GetPSDitherName(u32 dither)
{
	switch (dither)
	{
		case 0: return "None";
		case 1: return "Standard";
		case 2: return "ReciprocalScaled";
	}
	return "Unknown";
}

static const char* GetSSTrilnName(GS_MIN_FILTER triln)
{
	switch (triln)
	{
		case GS_MIN_FILTER::Nearest: return "Nearest";
		case GS_MIN_FILTER::Linear: return "Linear";
		case GS_MIN_FILTER::Nearest_Mipmap_Nearest: return "Nearest_Mipmap_Nearest";
		case GS_MIN_FILTER::Nearest_Mipmap_Linear: return "Nearest_Mipmap_Linear";
		case GS_MIN_FILTER::Linear_Mipmap_Nearest: return "Linear_Mipmap_Nearest";
		case GS_MIN_FILTER::Linear_Mipmap_Linear: return "Linear_Mipmap_Linear";
	}
	return "Unknown";
}

static const char* GetBlendOpSymbol(GSDevice::BlendOp op)
{
	switch (op)
	{
		case GSDevice::OP_ADD:          return "+";
		case GSDevice::OP_SUBTRACT:     return "-";
		case GSDevice::OP_REV_SUBTRACT: return "-";
	}
	return "Unknown";
}

static const char* GetBlendFactorFormula(GSDevice::BlendFactor blendfactor)
{
	switch (blendfactor)
	{
		case GSDevice::SRC_COLOR:       return "Cs";
		case GSDevice::INV_SRC_COLOR:   return "(1 - Cs)";
		case GSDevice::DST_COLOR:       return "Cd";
		case GSDevice::INV_DST_COLOR:   return "(1 - Cd)";
		case GSDevice::SRC1_COLOR:      return "Cs1";
		case GSDevice::INV_SRC1_COLOR:  return "(1 - Cs1)";
		case GSDevice::SRC_ALPHA:       return "As";
		case GSDevice::INV_SRC_ALPHA:   return "(1 - As)";
		case GSDevice::DST_ALPHA:       return "Ad";
		case GSDevice::INV_DST_ALPHA:   return "(1 - Ad)";
		case GSDevice::SRC1_ALPHA:      return "As1";
		case GSDevice::INV_SRC1_ALPHA:  return "(1 - As1)";
		case GSDevice::CONST_COLOR:     return "Cf";
		case GSDevice::INV_CONST_COLOR: return "(1 - Cf)";
		case GSDevice::CONST_ONE:       return "1";
		case GSDevice::CONST_ZERO:      return "0";
	}
	return "Unknown";
}

static const char* GetDestinationAlphaModeName(GSHWDrawConfig::DestinationAlphaMode datm)
{
	switch (datm)
	{
		case GSHWDrawConfig::DestinationAlphaMode::Off:            return "Off";
		case GSHWDrawConfig::DestinationAlphaMode::Stencil:        return "Stencil";
		case GSHWDrawConfig::DestinationAlphaMode::StencilOne:     return "StencilOne";
		case GSHWDrawConfig::DestinationAlphaMode::PrimIDTracking: return "PrimIDTracking";
		case GSHWDrawConfig::DestinationAlphaMode::Full:           return "Full";
	}
	return "Unknown";
}

static const char* GetColClipModeName(GSHWDrawConfig::ColClipMode ccmode)
{
	switch (ccmode)
	{
		case GSHWDrawConfig::ColClipMode::NoModify:          return "NoModify";
		case GSHWDrawConfig::ColClipMode::ConvertOnly:       return "ConvertOnly";
		case GSHWDrawConfig::ColClipMode::ResolveOnly:       return "ResolveOnly";
		case GSHWDrawConfig::ColClipMode::ConvertAndResolve: return "ConvertAndResolve";
		case GSHWDrawConfig::ColClipMode::EarlyResolve:      return "EarlyResolve";
	}
	return "Unknown";
}

static const char* GetSetDATMName(SetDATM datm)
{
	switch (datm)
	{
		case SetDATM::DATM0: return "DATM0";
		case SetDATM::DATM1: return "DATM1";
		case SetDATM::DATM0_RTA_CORRECTION: return "DATM0_RTA_CORRECTION";
		case SetDATM::DATM1_RTA_CORRECTION: return "DATM1_RTA_CORRECTION";
	}
	return "Unknown";
}

static const char* GetPSAA1Name(GSHWDrawConfig::PS_AA1 aa1)
{
	switch (aa1)
	{
		case GSHWDrawConfig::PS_AA1::NONE: return "NONE";
		case GSHWDrawConfig::PS_AA1::LINE: return "LINE";
		case GSHWDrawConfig::PS_AA1::TRIANGLE: return "TRIANGLE";
		case GSHWDrawConfig::PS_AA1::TRIANGLE_SW_Z: return "TRIANGLE_SW_Z";
	}
	return "Unknown";
}

static const char* GetTexHazardName(u32 tex_hazard)
{
	switch (tex_hazard)
	{
		case GSHWDrawConfig::TEX_HAZARD_NONE: return "NONE";
		case GSHWDrawConfig::TEX_HAZARD_RT: return "RT";
		case GSHWDrawConfig::TEX_HAZARD_DEPTH: return "DEPTH";
	}
	return "Unknown";
}

static const char* GetTextureTypeName(GSTexture::Type type)
{
	switch (type)
	{
		case GSTexture::Type::Invalid: return "Invalid";
		case GSTexture::Type::RenderTarget: return "RenderTarget";
		case GSTexture::Type::DepthStencil: return "DepthStencil";
		case GSTexture::Type::Texture: return "Texture";
		case GSTexture::Type::RWTexture: return "RWTexture";
	}
	return "Unknown";
}

static void DumpTextureDescriptor(DrawConfigWriter& out, const char* name, const GSTexture* texture)
{
	if (!texture)
	{
		out.WriteLn("{}: none", name);
		return;
	}

	out.WriteLn("{}: [{}, {}, {} ({}), {} ({}), {}]", name, texture->GetWidth(), texture->GetHeight(),
		GetTextureTypeName(texture->GetType()), static_cast<u32>(texture->GetType()),
		GSTexture::GetFormatName(texture->GetFormat()), static_cast<u32>(texture->GetFormat()),
		texture->GetMipmapLevels());
}

static const char* GetPSROVDepthname(GSHWDrawConfig::PS_ROV_DEPTH rov_depth)
{
	switch (rov_depth)
	{
		case GSHWDrawConfig::PS_ROV_DEPTH::NONE: return "NONE";
		case GSHWDrawConfig::PS_ROV_DEPTH::READ_ONLY: return "READ_ONLY";
		case GSHWDrawConfig::PS_ROV_DEPTH::READ_WRITE: return "READ_WRITE";
	}
	return "Unknown";
}

static void DumpPSSelector(DrawConfigWriter& out, const GSHWDrawConfig::PSSelector& ps)
{
	out.WriteLn("aem_fmt: {}", ps.aem_fmt);
	out.WriteLn("pal_fmt: {}", ps.pal_fmt);
	out.WriteLn("dst_fmt: {} ({})", GetPSDstFmtName(ps.dst_fmt), ps.depth_fmt);
	out.WriteLn("depth_fmt: {} ({})", GetPSDepthFmtName(ps.depth_fmt), ps.depth_fmt);
	out.WriteLn("aem: {}", ps.aem);
	out.WriteLn("fba: {}", ps.fba);
	out.WriteLn("fog: {}", ps.fog);
	out.WriteLn("iip: {}", ps.iip);
	out.WriteLn("date: {} ({})", GetPSDateName(ps.date), ps.date);
	out.WriteLn("atst: {} ({})", GetPSAlphaTestName(ps.atst), static_cast<u32>(ps.atst));
	out.WriteLn("afail: {} ({})", GetPSAFAILName(ps.afail), static_cast<u32>(ps.afail));
	out.WriteLn("fst: {}", ps.fst);
	out.WriteLn("tfx: {}", ps.tfx);
	out.WriteLn("tcc: {} ({})", GSUtil::GetTCCName(ps.tcc), ps.tcc);
	out.WriteLn("wms: {} ({})", GSUtil::GetWMName(ps.wms), ps.wms);
	out.WriteLn("wmt: {} ({})", GSUtil::GetWMName(ps.wmt), ps.wmt);
	out.WriteLn("adjs: {}", ps.adjs);
	out.WriteLn("adjt: {}", ps.adjt);
	out.WriteLn("ltf: {}", ps.ltf);
	out.WriteLn("shuffle: {}", ps.shuffle);
	out.WriteLn("shuffle_same: {}", ps.shuffle_same);
	out.WriteLn("real16src: {}", ps.real16src);
	out.WriteLn("process_ba: {}", ps.process_ba);
	out.WriteLn("process_rg: {}", ps.process_rg);
	out.WriteLn("shuffle_across: {}", ps.shuffle_across);
	out.WriteLn("write_rg: {}", ps.write_rg);
	out.WriteLn("fbmask: {}", ps.fbmask);
	out.WriteLn("blend: ({} - {}) * {} + {}", GetPSBlendABDName(ps.blend_a), GetPSBlendABDName(ps.blend_b), GetPSBlendCName(ps.blend_c), GetPSBlendABDName(ps.blend_d));
	out.WriteLn("fixed_one_a: {}", ps.fixed_one_a);
	out.WriteLn("blend_hw: {} ({})", GetHWBlendTypeName(static_cast<HWBlendType>(ps.blend_hw), ps.blend_mix), ps.blend_hw);
	out.WriteLn("a_masked: {}", ps.a_masked);
	out.WriteLn("colclip_hw: {}", ps.colclip_hw);
	out.WriteLn("rta_correction: {}", ps.rta_correction);
	out.WriteLn("rta_source_correction: {}", ps.rta_source_correction);
	out.WriteLn("colclip: {}", ps.colclip);
	out.WriteLn("blend_mix: {}", ps.blend_mix);
	out.WriteLn("round_inv: {}", ps.round_inv);
	out.WriteLn("pabe: {}", ps.pabe);
	out.WriteLn("no_color: {}", ps.no_color);
	out.WriteLn("no_color1: {}", ps.no_color1);
	out.WriteLn("channel: {} ({})", GetPSChannelName(static_cast<ChannelFetch>(ps.channel)), ps.channel);
	out.WriteLn("dither: {} ({})", GetPSDitherName(ps.dither), ps.dither);
	out.WriteLn("dither_adjust: {}", ps.dither_adjust);
	out.WriteLn("zclamp: {}", ps.zclamp);
	out.WriteLn("tcoffsethack: {}", ps.tcoffsethack);
	out.WriteLn("urban_chaos_hle: {}", ps.urban_chaos_hle);
	out.WriteLn("tales_of_abyss_hle: {}", ps.tales_of_abyss_hle);
	out.WriteLn("tex_is_fb: {}", ps.tex_is_fb);
	out.WriteLn("automatic_lod: {}", ps.automatic_lod);
	out.WriteLn("manual_lod: {}", ps.manual_lod);
	out.WriteLn("point_sampler: {}", ps.point_sampler);
	out.WriteLn("region_rect: {}", ps.region_rect);
	out.WriteLn("scanmsk: {} ({})", GSUtil::GetSCANMSKName(ps.scanmsk), ps.scanmsk);
	out.WriteLn("aa1: {} ({})", GetPSAA1Name(ps.aa1), static_cast<u32>(ps.aa1));
	out.WriteLn("abe: {}", static_cast<u32>(ps.abe));
	out.WriteLn("sw_aniso: {}", ps.sw_aniso);
	out.WriteLn("rov_color: {}", ps.rov_color);
	out.WriteLn("rov_depth: {} ({})", GetPSROVDepthname(ps.rov_depth), static_cast<u32>(ps.rov_depth));
	out.WriteLn("ztst: {} ({})", GSUtil::GetZTSTName(ps.ztst), static_cast<u32>(ps.ztst));
	out.WriteLn("zfloor: {}", static_cast<u32>(ps.zfloor));
}

static void DumpVSSelector(DrawConfigWriter& out, const GSHWDrawConfig::VSSelector& vs)
{
	out.WriteLn("fst: {}", vs.fst);
	out.WriteLn("tme: {}", vs.tme);
	out.WriteLn("iip: {}", vs.iip);
	out.WriteLn("point_size: {}", vs.point_size);
	out.WriteLn("expand: {} ({})", GetVSExpandName(vs.expand), static_cast<u32>(vs.expand));
}

static void DumpBlendEquation(DrawConfigWriter& out, const char* name, u32 op, u32 src_factor, u32 dst_factor)
{
	const char* src_formula = GetBlendFactorFormula(static_cast<GSDevice::BlendFactor>(src_factor));
	const char* dst_formula = GetBlendFactorFormula(static_cast<GSDevice::BlendFactor>(dst_factor));
	const char* symbol = GetBlendOpSymbol(static_cast<GSDevice::BlendOp>(op));
	if (op == GSDevice::OP_REV_SUBTRACT)
		out.WriteLn("{}: Cd * {} - Cs * {}", name, dst_formula, src_formula);
	else
		out.WriteLn("{}: Cs * {} {} Cd * {}", name, src_formula, symbol, dst_formula);
}

static void DumpBlendState(DrawConfigWriter& out, const GSHWDrawConfig::BlendState& bs)
{
	out.WriteLn("enable: {}", bs.enable);
	out.WriteLn("constant_enable: {}", bs.constant_enable);
	out.WriteLn("constant: {}", bs.constant);
	DumpBlendEquation(out, "equation", bs.op, bs.src_factor, bs.dst_factor);
	DumpBlendEquation(out, "equation_alpha", GSDevice::OP_ADD, bs.src_factor_alpha, bs.dst_factor_alpha);
}

static void DumpDepthStencilSelctor(DrawConfigWriter& out, const GSHWDrawConfig::DepthStencilSelector& dss)
{
	out.WriteLn("ztst: {} ({})", GSUtil::GetZTSTName(dss.ztst), dss.ztst);
	out.WriteLn("zwe: {}", dss.zwe);
	out.WriteLn("date: {}", dss.date);
	out.WriteLn("date_one: {}", dss.date_one);
}

static void DumpSamplerSelector(DrawConfigWriter& out, const GSHWDrawConfig::SamplerSelector& ss)
{
	out.WriteLn("tau: {}", ss.tau);
	out.WriteLn("tav: {}", ss.tav);
	out.WriteLn("biln: {}", ss.biln);
	out.WriteLn("triln: {} ({})", GetSSTrilnName(static_cast<GS_MIN_FILTER>(ss.triln)), ss.triln);
	out.WriteLn("lodclamp: {}", ss.lodclamp);
}

static void DumpAlphaPass(DrawConfigWriter& out, const GSHWDrawConfig::AlphaPass& ap)
{
	out.WriteLn("enable: {}", ap.enable);
	out.WriteLn("require_one_barrier: {}", ap.require_one_barrier);
	out.WriteLn("require_full_barrier: {}", ap.require_full_barrier);
	out.WriteLn("colormask: {:x}", ap.colormask.wrgba);
	out.WriteLn("ps_aref: {}", ap.ps_aref);

	out.WriteLn("ps:");
	DumpPSSelector(out.WithIndent(), ap.ps);

	out.WriteLn("dss:");
	DumpDepthStencilSelctor(out.WithIndent(), ap.depth);
}

static void DumpBlendMultipass(DrawConfigWriter& out, const GSHWDrawConfig::BlendMultiPass& bmp)
{
	out.WriteLn("enable: {}", bmp.enable);
	out.WriteLn("no_color1: {}", bmp.no_color1);
	out.WriteLn("blend_hw: {} ({})", GetPSBlendHWName(static_cast<HWBlendType>(bmp.blend_hw)), bmp.blend_hw);
	out.WriteLn("dither: {}", bmp.dither);

	out.WriteLn("blend:");
	DumpBlendState(out.WithIndent(), bmp.blend);
}

template<typename T, typename U = int>
static void DumpVector4(DrawConfigWriter& out, const char* name, const T& val)
{
	out.WriteLn("{}: [{}, {}, {}, {}]", name, val.x, val.y, val.z, val.w);
};

template<typename T>
static void DumpVector2(DrawConfigWriter& out, const char* name, const T& val)
{
	out.WriteLn("{}: [{}, {}]", name, val.x, val.y);
};

static void DumpPSConstantBuffer(DrawConfigWriter& out, const GSHWDrawConfig::PSConstantBuffer& cb)
{
	DumpVector4(out, "FogColor_AREF", cb.FogColor_AREF);
	DumpVector4(out, "WH", cb.WH);
	DumpVector4(out, "TA_MaxDepth_Af", cb.TA_MaxDepth_Af);
	DumpVector4(out, "FbMask", cb.FbMask);
	DumpVector4(out, "HalfTexel", cb.HalfTexel);
	DumpVector4(out, "MinMax", cb.MinMax);
	DumpVector4(out, "LODParams", cb.LODParams);
	DumpVector4(out, "STRange", cb.STRange);
	DumpVector4(out, "ChannelShuffle", cb.ChannelShuffle);
	DumpVector2(out, "ChannelShuffleOffset", cb.ChannelShuffleOffset);
	DumpVector2(out, "TCOffsetHack", cb.TCOffsetHack);
	DumpVector2(out, "STScale", cb.STScale);
	DumpVector4(out, "DitherMatrix_0", cb.DitherMatrix[0]);
	DumpVector4(out, "DitherMatrix_1", cb.DitherMatrix[1]);
	DumpVector4(out, "DitherMatrix_2", cb.DitherMatrix[2]);
	DumpVector4(out, "DitherMatrix_3", cb.DitherMatrix[3]);
	DumpVector4(out, "ScaleFactor", cb.ScaleFactor);
	out.WriteLn("LineCovScale: {}", cb.LineCovScale);
}

static void DumpVSConstantBuffer(DrawConfigWriter& out, const GSHWDrawConfig::VSConstantBuffer& cb)
{
	DumpVector2(out, "vertex_scale", cb.vertex_scale);
	DumpVector2(out, "vertex_offset", cb.vertex_offset);
	DumpVector2(out, "texture_scale", cb.texture_scale);
	DumpVector2(out, "texture_offset", cb.texture_offset);
	DumpVector2(out, "point_size", cb.point_size);
	out.WriteLn("max_depth: {}", cb.max_depth);
	out.WriteLn("line_aa1_width: {}", cb.line_aa1_width);
}

static u64 HashDrawBytes(const void* data, size_t size)
{
	if (!data || size == 0)
		return 0;
	const u8* bytes = static_cast<const u8*>(data);
	u64 hash = 14695981039346656037ull;
	for (size_t i = 0; i < size; i++)
		hash = (hash ^ bytes[i]) * 1099511628211ull;
	return hash;
}

#ifdef PCSX2_TRACE_ONLY
namespace
{
	constexpr u64 TEXTURE_TRACE_FNV_OFFSET = 14695981039346656037ull;
	constexpr u64 TEXTURE_TRACE_FNV_PRIME = 1099511628211ull;

	struct TextureTraceLevel
	{
		std::vector<u8> pixels;
		std::vector<u8> known_pixels;
		u32 width = 0;
		u32 height = 0;
		u32 bytes_per_pixel = 0;
		u32 known_count = 0;
	};

	struct TextureTraceStorage
	{
		struct RenderTargetWriter
		{
			u64 generation = 0;
			u64 draw = 0;
			GSVector4i drawarea = {};
			u8 color_mask = 0;
		};

		std::vector<TextureTraceLevel> levels;
		u32 generation = 0;
		std::vector<u8> render_target_pixels;
		u64 render_target_generation = 0;
		u64 render_target_readback_generation = std::numeric_limits<u64>::max();
		u64 render_target_writer_draw = 0;
		std::vector<RenderTargetWriter> render_target_writers;
		u32 render_target_width = 0;
		u32 render_target_height = 0;
		u32 render_target_pitch = 0;
		u32 render_target_bytes_per_pixel = 0;
	};

	std::unordered_map<const GSTexture*, TextureTraceStorage> s_texture_trace_storage;

	void TextureTraceHashByte(u64& hash, u8 value)
	{
		hash = (hash ^ value) * TEXTURE_TRACE_FNV_PRIME;
	}

	void TextureTraceHashU32(u64& hash, u32 value)
	{
		for (u32 shift = 0; shift < 32; shift += 8)
			TextureTraceHashByte(hash, static_cast<u8>(value >> shift));
	}
}

void GSRecordTextureTraceContent(const GSTexture* texture,
	const GSVector4i& rect, const void* data, u32 source_pitch,
	u32 bytes_per_pixel, u32 level)
{
	if (!texture || !data || bytes_per_pixel == 0 || level >= 32 ||
		rect.x < 0 || rect.y < 0 || rect.z <= rect.x || rect.w <= rect.y)
	{
		return;
	}

	const u32 width = std::max(1, texture->GetWidth() >> level);
	const u32 height = std::max(1, texture->GetHeight() >> level);
	if (static_cast<u32>(rect.z) > width || static_cast<u32>(rect.w) > height ||
		source_pitch < static_cast<u32>(rect.width()) * bytes_per_pixel)
	{
		return;
	}

	TextureTraceStorage& storage = s_texture_trace_storage[texture];
	if (storage.levels.size() <= level)
		storage.levels.resize(level + 1);
	TextureTraceLevel& level_storage = storage.levels[level];
	const size_t pixel_count = static_cast<size_t>(width) * height;
	const size_t byte_count = pixel_count * bytes_per_pixel;
	if (level_storage.width != width || level_storage.height != height ||
		level_storage.bytes_per_pixel != bytes_per_pixel ||
		level_storage.pixels.size() != byte_count)
	{
		level_storage = {};
		level_storage.width = width;
		level_storage.height = height;
		level_storage.bytes_per_pixel = bytes_per_pixel;
		level_storage.pixels.resize(byte_count);
		level_storage.known_pixels.resize(pixel_count);
	}

	const u8* source = static_cast<const u8*>(data);
	const u32 copy_width = static_cast<u32>(rect.width());
	const u32 copy_height = static_cast<u32>(rect.height());
	for (u32 y = 0; y < copy_height; y++)
	{
		const size_t destination_pixel =
			(static_cast<size_t>(rect.y + y) * width) + rect.x;
		std::memcpy(level_storage.pixels.data() +
				destination_pixel * bytes_per_pixel,
			source + static_cast<size_t>(y) * source_pitch,
			static_cast<size_t>(copy_width) * bytes_per_pixel);
		for (u32 x = 0; x < copy_width; x++)
		{
			u8& known = level_storage.known_pixels[destination_pixel + x];
			if (!known)
			{
				known = 1;
				level_storage.known_count++;
			}
		}
	}
	storage.generation++;
}

void GSForgetTextureTraceContent(const GSTexture* texture)
{
	s_texture_trace_storage.erase(texture);
}

GSTextureTraceSummary GSGetTextureTraceSummary(const GSTexture* texture)
{
	GSTextureTraceSummary summary;
	const auto storage_it = s_texture_trace_storage.find(texture);
	if (storage_it == s_texture_trace_storage.end())
		return summary;

	const TextureTraceStorage& storage = storage_it->second;
	summary.generation = storage.generation;
	summary.content_hash = TEXTURE_TRACE_FNV_OFFSET;
	for (u32 level = 0; level < storage.levels.size() && level < 32; level++)
	{
		const TextureTraceLevel& level_storage = storage.levels[level];
		const size_t pixel_count =
			static_cast<size_t>(level_storage.width) * level_storage.height;
		if (pixel_count == 0 || level_storage.known_count != pixel_count)
			continue;

		summary.known_levels |= 1u << level;
		TextureTraceHashU32(summary.content_hash, level);
		TextureTraceHashU32(summary.content_hash,
			static_cast<u32>(level_storage.pixels.size()));
		for (size_t i = 0; i < level_storage.pixels.size(); i++)
		{
			const u8 value = level_storage.pixels[i];
			TextureTraceHashByte(summary.content_hash, value);
			const u32 channel = i % level_storage.bytes_per_pixel;
			if (channel < 4)
				summary.channel_or |= static_cast<u32>(value) << (channel * 8);
		}
		summary.content_bytes += static_cast<u32>(level_storage.pixels.size());
	}
	if (summary.known_levels == 0)
		summary.content_hash = 0;
	return summary;
}

void GSRecordTextureTraceRenderTargetWrite(const GSTexture* texture,
	u64 writer_draw)
{
	if (!texture)
		return;
	GSRecordTextureTraceRenderTargetWrite(texture, writer_draw,
		GSVector4i(0, 0, texture->GetWidth(), texture->GetHeight()), 0xf);
}

void GSRecordTextureTraceRenderTargetWrite(const GSTexture* texture,
	u64 writer_draw, const GSVector4i& drawarea, u8 color_mask)
{
	if (!texture)
		return;
	TextureTraceStorage& storage = s_texture_trace_storage[texture];
	storage.render_target_generation++;
	storage.render_target_writer_draw = writer_draw;
	storage.render_target_writers.push_back({
		storage.render_target_generation, writer_draw, drawarea, color_mask});
}

GSTextureTraceRenderTargetSummary GSReadTextureTraceRenderTarget(
	GSTexture* texture, const GSVector4i& requested_rect)
{
	GSTextureTraceRenderTargetSummary summary;
	if (!texture || !texture->IsRenderTarget() ||
		texture->GetFormat() != GSTexture::Format::Color)
	{
		summary.status = 3;
		return summary;
	}

	TextureTraceStorage& storage = s_texture_trace_storage[texture];
	const u32 width = static_cast<u32>(texture->GetWidth());
	const u32 height = static_cast<u32>(texture->GetHeight());
	constexpr u32 bytes_per_pixel = 4;
	const u32 pitch = width * bytes_per_pixel;
	if (storage.render_target_readback_generation !=
			storage.render_target_generation ||
		storage.render_target_width != width ||
		storage.render_target_height != height)
	{
		std::unique_ptr<GSDownloadTexture> download(
			g_gs_device->CreateDownloadTexture(width, height,
				GSTexture::Format::Color));
		const GSVector4i full_rect(0, 0, width, height);
		if (!download)
		{
			summary.status = 4;
			return summary;
		}
		download->CopyFromTexture(full_rect, texture, full_rect, 0);
		download->Flush();
		if (!download->Map(full_rect))
		{
			summary.status = 4;
			return summary;
		}
		storage.render_target_pixels.resize(static_cast<size_t>(pitch) * height);
		const u8* source = download->GetMapPointer();
		const u32 source_pitch = download->GetMapPitch();
		for (u32 y = 0; y < height; y++)
		{
			std::memcpy(storage.render_target_pixels.data() +
					static_cast<size_t>(y) * pitch,
				source + static_cast<size_t>(y) * source_pitch, pitch);
		}
		download->Unmap();
		storage.render_target_readback_generation =
			storage.render_target_generation;
		storage.render_target_width = width;
		storage.render_target_height = height;
		storage.render_target_pitch = pitch;
		storage.render_target_bytes_per_pixel = bytes_per_pixel;
	}

	const GSVector4i rect = requested_rect.rintersect(
		GSVector4i(0, 0, width, height));
	if (rect.rempty())
	{
		summary.status = 3;
		return summary;
	}
	summary.content_hash = TEXTURE_TRACE_FNV_OFFSET;
	for (u32 channel = 0; channel < 4; channel++)
		summary.channel_hash[channel] = TEXTURE_TRACE_FNV_OFFSET;
	for (u32 tile = 0; tile < GSTextureTraceRenderTargetSummary::TileCount;
		tile++)
	{
		summary.tile_hash[tile] = TEXTURE_TRACE_FNV_OFFSET;
	}
	const u32 row_bytes = static_cast<u32>(rect.width()) * bytes_per_pixel;
	for (int y = rect.y; y < rect.w; y++)
	{
		const u8* row = storage.render_target_pixels.data() +
			static_cast<size_t>(y) * pitch +
			static_cast<size_t>(rect.x) * bytes_per_pixel;
		const u32 tile_y = std::min<u32>(3,
			(static_cast<u32>(y - rect.y) * 4) /
			static_cast<u32>(rect.height()));
		for (u32 x = 0; x < row_bytes; x++)
		{
			TextureTraceHashByte(summary.content_hash, row[x]);
			const u32 channel = x % bytes_per_pixel;
			TextureTraceHashByte(summary.channel_hash[channel], row[x]);
			summary.channel_or |= static_cast<u32>(row[x]) << (channel * 8);
			const u32 pixel = x / bytes_per_pixel;
			const u32 tile_x = std::min<u32>(3,
				(pixel * 4) / static_cast<u32>(rect.width()));
			const u32 tile = tile_y * 4 + tile_x;
			TextureTraceHashByte(summary.tile_hash[tile], row[x]);
			if (row[x] != 0)
				summary.nonzero_tiles |= 1u << tile;
		}
	}
	summary.generation = storage.render_target_generation;
	u64 best_channel_area[4] = {};
	bool found_writer = false;
	for (auto it = storage.render_target_writers.rbegin();
		it != storage.render_target_writers.rend(); ++it)
	{
		if (it->generation > storage.render_target_generation)
			continue;
		const GSVector4i overlap = rect.rintersect(it->drawarea);
		if (overlap.rempty())
			continue;
		const u64 area = static_cast<u64>(overlap.width()) * overlap.height();
		if (!found_writer)
		{
			summary.writer_draw = it->draw;
			found_writer = true;
		}
		for (u32 channel = 0; it->draw != 0 && channel < 4; channel++)
		{
			if ((it->color_mask & (1u << channel)) != 0 &&
				area > best_channel_area[channel])
			{
				best_channel_area[channel] = area;
				summary.channel_writer_draw[channel] = it->draw;
			}
		}
	}
	summary.content_bytes = row_bytes * rect.height();
	summary.status = 1;
	return summary;
}

void GSAppendTextureTraceRenderTargetResult(const std::string& path,
	GSTexture* texture, const GSVector4i& rect)
{
	const GSTextureTraceRenderTargetSummary summary =
		GSReadTextureTraceRenderTarget(texture, rect);
	FILE* const file = FileSystem::OpenCFile(path.c_str(), "ab");
	if (!file)
		return;
	std::fprintf(file, "rt_after_content_hash: 0x%016llx\n",
		static_cast<unsigned long long>(summary.content_hash));
	std::fprintf(file, "rt_after_generation: %llu\n",
		static_cast<unsigned long long>(summary.generation));
	for (u32 channel = 0; channel < 4; channel++)
	{
		static constexpr char names[4] = {'r', 'g', 'b', 'a'};
		std::fprintf(file, "rt_after_channel_hash_%c: 0x%016llx\n",
			names[channel],
			static_cast<unsigned long long>(summary.channel_hash[channel]));
	}
	for (u32 tile = 0; tile < GSTextureTraceRenderTargetSummary::TileCount;
		tile++)
	{
		std::fprintf(file, "rt_after_tile_hash_%02u: 0x%016llx\n", tile,
			static_cast<unsigned long long>(summary.tile_hash[tile]));
	}
	std::fprintf(file, "rt_after_content_bytes: %u\n",
		summary.content_bytes);
	std::fprintf(file, "rt_after_channel_or: 0x%08x\n", summary.channel_or);
	std::fprintf(file, "rt_after_status: %u\n", summary.status);
	std::fprintf(file, "rt_after_nonzero_tiles: 0x%04x\n",
		summary.nonzero_tiles);
	std::fclose(file);
}
#endif

static void DumpConfig(DrawConfigWriter& out, const GSHWDrawConfig& conf,
	bool ps, bool vs, bool bs, bool dss, bool ss, bool asp, bool bmp, bool cbvs, bool cbps)
{
#ifdef PCSX2_TRACE_ONLY
	out.WriteLn("source: {}", Pcsx2Trace::ResolveGsTraceSource(0xff));
#endif
	DumpTextureDescriptor(out, "rt", conf.rt);
	DumpTextureDescriptor(out, "ds", conf.ds);
	DumpTextureDescriptor(out, "tex", conf.tex);
	DumpTextureDescriptor(out, "pal", conf.pal);
	out.WriteLn("nverts: {}", conf.nverts);
	out.WriteLn("nindices: {}", conf.nindices);
	out.WriteLn("indices_per_prim: {}", conf.indices_per_prim);
	out.WriteLn("cb_vs_hash: 0x{:016x}", HashDrawBytes(&conf.cb_vs, sizeof(conf.cb_vs)));
	out.WriteLn("cb_ps_hash: 0x{:016x}", HashDrawBytes(&conf.cb_ps, sizeof(conf.cb_ps)));
	out.WriteLn("vertex_hash: 0x{:016x}",
		HashDrawBytes(conf.verts, static_cast<size_t>(conf.nverts) * sizeof(GSVertex)));
	out.WriteLn("index_hash: 0x{:016x}",
		HashDrawBytes(conf.indices, static_cast<size_t>(conf.nindices) * sizeof(u16)));
#ifdef PCSX2_TRACE_ONLY
	for (u32 i = 0; i < std::min<u32>(conf.nverts, 4); i++)
	{
		const GSVertex& vertex = conf.verts[i];
		out.WriteLn("vertex_rgba_{}: 0x{:08x}", i, vertex.RGBAQ.U32[0]);
		out.WriteLn("vertex_xy_{}: 0x{:08x}", i,
			static_cast<u32>(vertex.XYZ.X) |
			(static_cast<u32>(vertex.XYZ.Y) << 16));
		out.WriteLn("vertex_z_{}: 0x{:08x}", i, vertex.XYZ.Z);
		out.WriteLn("vertex_uv_{}: 0x{:08x}", i, vertex.UV);
	}
#endif
#ifdef PCSX2_TRACE_ONLY
	const GSTextureTraceSummary texture_summary =
		GSGetTextureTraceSummary(conf.tex);
	out.WriteLn("tex_content_hash: 0x{:016x}", texture_summary.content_hash);
	out.WriteLn("tex_content_bytes: {}", texture_summary.content_bytes);
	out.WriteLn("tex_channel_or: 0x{:08x}", texture_summary.channel_or);
	out.WriteLn("tex_content_generation: {}", texture_summary.generation);
	out.WriteLn("tex_content_levels: 0x{:08x}", texture_summary.known_levels);
	const GSTextureTraceSummary palette_summary =
		GSGetTextureTraceSummary(conf.pal);
	out.WriteLn("pal_content_hash: 0x{:016x}", palette_summary.content_hash);
	out.WriteLn("pal_content_bytes: {}", palette_summary.content_bytes);
	out.WriteLn("pal_channel_or: 0x{:08x}", palette_summary.channel_or);
	out.WriteLn("pal_content_generation: {}", palette_summary.generation);
	out.WriteLn("pal_content_levels: 0x{:08x}", palette_summary.known_levels);
	const GSTextureTraceRenderTargetSummary rt_source_summary =
		GSReadTextureTraceRenderTarget(conf.tex, conf.samplearea);
	out.WriteLn("rt_source_content_hash: 0x{:016x}",
		rt_source_summary.content_hash);
	out.WriteLn("rt_source_channel_hash_r: 0x{:016x}",
		rt_source_summary.channel_hash[0]);
	out.WriteLn("rt_source_channel_hash_g: 0x{:016x}",
		rt_source_summary.channel_hash[1]);
	out.WriteLn("rt_source_channel_hash_b: 0x{:016x}",
		rt_source_summary.channel_hash[2]);
	out.WriteLn("rt_source_channel_hash_a: 0x{:016x}",
		rt_source_summary.channel_hash[3]);
	out.WriteLn("rt_source_generation: {}", rt_source_summary.generation);
	out.WriteLn("rt_source_content_bytes: {}", rt_source_summary.content_bytes);
	out.WriteLn("rt_source_channel_or: 0x{:08x}", rt_source_summary.channel_or);
	out.WriteLn("rt_source_writer_draw: {}", rt_source_summary.writer_draw);
	out.WriteLn("rt_source_channel_writer_r: {}",
		rt_source_summary.channel_writer_draw[0]);
	out.WriteLn("rt_source_channel_writer_g: {}",
		rt_source_summary.channel_writer_draw[1]);
	out.WriteLn("rt_source_channel_writer_b: {}",
		rt_source_summary.channel_writer_draw[2]);
	out.WriteLn("rt_source_channel_writer_a: {}",
		rt_source_summary.channel_writer_draw[3]);
	out.WriteLn("rt_source_status: {}", rt_source_summary.status);
#endif
	out.WriteLn("topology: {} ({})", GetTopologyName(conf.topology), static_cast<u32>(conf.topology));
	out.WriteLn("require_one_barrier: {}", conf.require_one_barrier);
	out.WriteLn("require_full_barrier: {}", conf.require_full_barrier);
	DumpVector4(out, "drawarea", conf.drawarea);
	DumpVector4(out, "samplearea", conf.samplearea);
	out.WriteLn("tex_hazard: {}", GetTexHazardName(conf.tex_hazard));

	out.WriteLn("destination_alpha: {} ({})", GetDestinationAlphaModeName(conf.destination_alpha), static_cast<u32>(conf.destination_alpha));
	out.WriteLn("datm: {} ({})", GetSetDATMName(conf.datm), static_cast<u32>(conf.datm));
	out.WriteLn("line_expand: {}", conf.line_expand);
	out.WriteLn("colormask: {:x}", conf.colormask.wrgba);

	if (ps)
	{
		out.WriteLn("ps:");
		DumpPSSelector(out.WithIndent(), conf.ps);
	}

	if (vs)
	{
		out.WriteLn("vs:");
		DumpVSSelector(out.WithIndent(), conf.vs);
	}

	if (bs)
	{
		out.WriteLn("blend:");
		DumpBlendState(out.WithIndent(), conf.blend);
	}

	if (ss)
	{
		out.WriteLn("sampler:");
		DumpSamplerSelector(out.WithIndent(), conf.sampler);
	}

	if (dss)
	{
		out.WriteLn("depth:");
		DumpDepthStencilSelctor(out.WithIndent(), conf.depth);
	}
	
	if (asp)
	{
		out.WriteLn("alpha_second_pass:");
		DumpAlphaPass(out.WithIndent(), conf.alpha_second_pass);
	}

	if (bmp)
	{
		out.WriteLn("blend_multi_pass:");
		DumpBlendMultipass(out.WithIndent(), conf.blend_multi_pass);
	}

	if (cbvs)
	{
		out.WriteLn("cb_vs:");
		DumpVSConstantBuffer(out.WithIndent(), conf.cb_vs);
	}
	
	if (cbps)
	{
		out.WriteLn("cb_ps:");
		DumpPSConstantBuffer(out.WithIndent(), conf.cb_ps);
	}
}

void GSHWDrawConfig::DumpConfig(const std::string& path, const GSHWDrawConfig& conf,
	u32 frame,
	bool ps, bool vs, bool bs, bool dss, bool ss, bool asp, bool bmp, bool cbvs, bool cbps)
{
	if (FileSystem::ManagedCFilePtr file = FileSystem::OpenManagedCFile(path.c_str(), "w"))
	{
		DrawConfigWriter writer;
		writer.WriteLn("frame: {}", frame);
		::DumpConfig(writer, conf, ps, vs, bs, dss, ss, asp, bmp, cbvs, cbps);
		fwrite(writer.buffer.data(), 1, writer.buffer.size(), file.get());
	}
}

static constexpr u32 NUM_REMAP_INPUTS = static_cast<u32>(ShaderConvert::Count) * 4;

static constexpr ShaderConvertSelector GetRemappedShader(u32 idx)
{
	ShaderConvert convert = static_cast<ShaderConvert>(idx >> 2);
	bool depth_out = (idx >> 0) & 1;
	Filter filter = static_cast<Filter>((idx >> 1) & 1);
	return ShaderConvertSelector(convert, 0xf, depth_out, filter);
}

static constexpr bool RemapIndexIsValid(u32 idx)
{
	ShaderConvert convert = static_cast<ShaderConvert>(idx >> 2);
	bool depth_out = (idx >> 0) & 1;
	Filter filter = static_cast<Filter>((idx >> 1) & 1);
	if (HasVariableWriteMask(convert) && !depth_out && filter == Nearest)
		return false; // Handled as variable write mask
	if (depth_out && !HasFloat32Output(convert))
		return false;
	if (filter == Biln && !SupportsBilinear(convert))
		return false;
	return true;
}

static constexpr u32 CalcNumRemappedShaders()
{
	u32 num = 0;
	for (u32 i = 0; i < NUM_REMAP_INPUTS; i++)
		num += RemapIndexIsValid(i);
	return num;
}

static constexpr u32 NUM_REMAPPED_SHADERS = CalcNumRemappedShaders();
static constexpr u32 NUM_TOTAL_SHADERS = NUM_REMAPPED_SHADERS +
                                         16 * ShaderConvertSelector::NUM_VARIABLE_WRITE_MASK_SHADERS;
static_assert(NUM_REMAPPED_SHADERS <= 256); // We use u8 for the remap indices.

static constexpr std::array<u8, NUM_REMAP_INPUTS> GenRemapArray()
{
	std::array<u8, NUM_REMAP_INPUTS> out{};
	u8 out_idx = 0;
	const u8 invalid = 0xff;
	for (u32 i = 0; i < NUM_REMAP_INPUTS; i++)
		out[i] = RemapIndexIsValid(i) ? out_idx++ : invalid;
	return out;
}

static constexpr std::array<ShaderConvertSelector, NUM_TOTAL_SHADERS> GetPackedShaders()
{
	std::array<ShaderConvertSelector, NUM_TOTAL_SHADERS> out{};
	u32 append_idx = 0;
	for (u32 i = 0; i < NUM_REMAP_INPUTS; i++)
	{
		if (RemapIndexIsValid(i))
			out[append_idx++] = GetRemappedShader(i);
	}
	for (u32 i = 0; i < 16; i++)
		out[append_idx++] = ShaderConvertSelector(ShaderConvert::COPY, i);
	for (u32 i = 0; i < 16; i++)
		out[append_idx++] = ShaderConvertSelector(ShaderConvert::RTA_CORRECTION, i);
	return out;
}

constinit const u32 ShaderConvertSelector::NUM_REMAPPED_SHADERS = ::NUM_REMAPPED_SHADERS;
constinit const u32 ShaderConvertSelector::NUM_TOTAL_SHADERS = ::NUM_TOTAL_SHADERS;
constinit const std::array<u8, NUM_REMAP_INPUTS> ShaderConvertSelector::INDEX_REMAP = GenRemapArray();
static constexpr auto PACKED_SHADERS = GetPackedShaders();
const std::span<const ShaderConvertSelector> ShaderConvertSelector::SHADERS = PACKED_SHADERS;

// clang-format off

// Maps PS2 blend modes to our best approximation of them with PC hardware
const std::array<HWBlend, 3*3*3*3> GSDevice::m_blendMap =
{{
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 0000: (Cs - Cs)*As + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 0001: (Cs - Cs)*As + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 0002: (Cs - Cs)*As +  0 ==> 0
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 0010: (Cs - Cs)*Ad + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 0011: (Cs - Cs)*Ad + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 0012: (Cs - Cs)*Ad +  0 ==> 0
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 0020: (Cs - Cs)*F  + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 0021: (Cs - Cs)*F  + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 0022: (Cs - Cs)*F  +  0 ==> 0
	{ BLEND_A_MAX | BLEND_MIX2 , OP_SUBTRACT     , CONST_ONE       , SRC1_COLOR}      , // 0100: (Cs - Cd)*As + Cs ==> Cs*(As + 1) - Cd*As
	{ BLEND_MIX1               , OP_ADD          , SRC1_COLOR      , INV_SRC1_COLOR}  , // 0101: (Cs - Cd)*As + Cd ==> Cs*As + Cd*(1 - As)
	{ BLEND_MIX1               , OP_SUBTRACT     , SRC1_COLOR      , SRC1_COLOR}      , // 0102: (Cs - Cd)*As +  0 ==> Cs*As - Cd*As
	{ BLEND_A_MAX              , OP_SUBTRACT     , CONST_ONE       , DST_ALPHA}       , // 0110: (Cs - Cd)*Ad + Cs ==> Cs*(Ad + 1) - Cd*Ad
	{ 0                        , OP_ADD          , DST_ALPHA       , INV_DST_ALPHA}   , // 0111: (Cs - Cd)*Ad + Cd ==> Cs*Ad + Cd*(1 - Ad)
	{ BLEND_HW5                , OP_SUBTRACT     , DST_ALPHA       , DST_ALPHA}       , // 0112: (Cs - Cd)*Ad +  0 ==> Cs*Ad - Cd*Ad
	{ BLEND_A_MAX | BLEND_MIX2 , OP_SUBTRACT     , CONST_ONE       , CONST_COLOR}     , // 0120: (Cs - Cd)*F  + Cs ==> Cs*(F + 1) - Cd*F
	{ BLEND_MIX1               , OP_ADD          , CONST_COLOR     , INV_CONST_COLOR} , // 0121: (Cs - Cd)*F  + Cd ==> Cs*F + Cd*(1 - F)
	{ BLEND_MIX1               , OP_SUBTRACT     , CONST_COLOR     , CONST_COLOR}     , // 0122: (Cs - Cd)*F  +  0 ==> Cs*F - Cd*F
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 0200: (Cs -  0)*As + Cs ==> Cs*(As + 1)
	{ BLEND_ACCU               , OP_ADD          , SRC1_COLOR      , CONST_ONE}       , // 0201: (Cs -  0)*As + Cd ==> Cs*As + Cd
	{ BLEND_NO_REC             , OP_ADD          , SRC1_COLOR      , CONST_ZERO}      , // 0202: (Cs -  0)*As +  0 ==> Cs*As
	{ BLEND_A_MAX | BLEND_HW8  , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 0210: (Cs -  0)*Ad + Cs ==> Cs*(Ad + 1)
	{ BLEND_HW3                , OP_ADD          , DST_ALPHA       , CONST_ONE}       , // 0211: (Cs -  0)*Ad + Cd ==> Cs*Ad + Cd
	{ BLEND_HW3                , OP_ADD          , DST_ALPHA       , CONST_ZERO}      , // 0212: (Cs -  0)*Ad +  0 ==> Cs*Ad
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 0220: (Cs -  0)*F  + Cs ==> Cs*(F + 1)
	{ BLEND_ACCU               , OP_ADD          , CONST_COLOR     , CONST_ONE}       , // 0221: (Cs -  0)*F  + Cd ==> Cs*F + Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_COLOR     , CONST_ZERO}      , // 0222: (Cs -  0)*F  +  0 ==> Cs*F
	{ BLEND_MIX3               , OP_ADD          , INV_SRC1_COLOR  , SRC1_COLOR}      , // 1000: (Cd - Cs)*As + Cs ==> Cd*As + Cs*(1 - As)
	{ BLEND_A_MAX | BLEND_MIX1 , OP_REV_SUBTRACT , SRC1_COLOR      , CONST_ONE}       , // 1001: (Cd - Cs)*As + Cd ==> Cd*(As + 1) - Cs*As
	{ BLEND_MIX1               , OP_REV_SUBTRACT , SRC1_COLOR      , SRC1_COLOR}      , // 1002: (Cd - Cs)*As +  0 ==> Cd*As - Cs*As
	{ 0                        , OP_ADD          , INV_DST_ALPHA   , DST_ALPHA}       , // 1010: (Cd - Cs)*Ad + Cs ==> Cd*Ad + Cs*(1 - Ad)
	{ BLEND_A_MAX              , OP_REV_SUBTRACT , DST_ALPHA       , CONST_ONE}       , // 1011: (Cd - Cs)*Ad + Cd ==> Cd*(Ad + 1) - Cs*Ad
	{ BLEND_HW5                , OP_REV_SUBTRACT , DST_ALPHA       , DST_ALPHA}       , // 1012: (Cd - Cs)*Ad +  0 ==> Cd*Ad - Cs*Ad
	{ BLEND_MIX3               , OP_ADD          , INV_CONST_COLOR , CONST_COLOR}     , // 1020: (Cd - Cs)*F  + Cs ==> Cd*F + Cs*(1 - F)
	{ BLEND_A_MAX | BLEND_MIX1 , OP_REV_SUBTRACT , CONST_COLOR     , CONST_ONE}       , // 1021: (Cd - Cs)*F  + Cd ==> Cd*(F + 1) - Cs*F
	{ BLEND_MIX1               , OP_REV_SUBTRACT , CONST_COLOR     , CONST_COLOR}     , // 1022: (Cd - Cs)*F  +  0 ==> Cd*F - Cs*F
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 1100: (Cd - Cd)*As + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 1101: (Cd - Cd)*As + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 1102: (Cd - Cd)*As +  0 ==> 0
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 1110: (Cd - Cd)*Ad + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 1111: (Cd - Cd)*Ad + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 1112: (Cd - Cd)*Ad +  0 ==> 0
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 1120: (Cd - Cd)*F  + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 1121: (Cd - Cd)*F  + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 1122: (Cd - Cd)*F  +  0 ==> 0
	{ BLEND_HW4                , OP_ADD          , CONST_ONE       , SRC1_COLOR}      , // 1200: (Cd -  0)*As + Cs ==> Cs + Cd*As
	{ BLEND_HW1                , OP_ADD          , DST_COLOR       , SRC1_COLOR}      , // 1201: (Cd -  0)*As + Cd ==> Cd*(1 + As)
	{ BLEND_HW2                , OP_ADD          , DST_COLOR       , SRC1_COLOR}      , // 1202: (Cd -  0)*As +  0 ==> Cd*As
	{ BLEND_HW6                , OP_ADD          , CONST_ONE       , DST_ALPHA}       , // 1210: (Cd -  0)*Ad + Cs ==> Cs + Cd*Ad
	{ BLEND_HW1                , OP_ADD          , DST_COLOR       , DST_ALPHA}       , // 1211: (Cd -  0)*Ad + Cd ==> Cd*(1 + Ad)
	{ BLEND_HW5                , OP_ADD          , CONST_ZERO      , DST_ALPHA}       , // 1212: (Cd -  0)*Ad +  0 ==> Cd*Ad
	{ BLEND_HW4                , OP_ADD          , CONST_ONE       , CONST_COLOR}     , // 1220: (Cd -  0)*F  + Cs ==> Cs + Cd*F
	{ BLEND_HW1                , OP_ADD          , DST_COLOR       , CONST_COLOR}     , // 1221: (Cd -  0)*F  + Cd ==> Cd*(1 + F)
	{ BLEND_HW2                , OP_ADD          , DST_COLOR       , CONST_COLOR}     , // 1222: (Cd -  0)*F  +  0 ==> Cd*F
	{ BLEND_NO_REC             , OP_ADD          , INV_SRC1_COLOR  , CONST_ZERO}      , // 2000: (0  - Cs)*As + Cs ==> Cs*(1 - As)
	{ BLEND_ACCU               , OP_REV_SUBTRACT , SRC1_COLOR      , CONST_ONE}       , // 2001: (0  - Cs)*As + Cd ==> Cd - Cs*As
	{ BLEND_NO_REC             , OP_REV_SUBTRACT , SRC1_COLOR      , CONST_ZERO}      , // 2002: (0  - Cs)*As +  0 ==> 0 - Cs*As
	{ BLEND_HW9                , OP_ADD          , INV_DST_ALPHA   , CONST_ZERO}      , // 2010: (0  - Cs)*Ad + Cs ==> Cs*(1 - Ad)
	{ BLEND_HW3                , OP_REV_SUBTRACT , DST_ALPHA       , CONST_ONE}       , // 2011: (0  - Cs)*Ad + Cd ==> Cd - Cs*Ad
	{ 0                        , OP_REV_SUBTRACT , DST_ALPHA       , CONST_ZERO}      , // 2012: (0  - Cs)*Ad +  0 ==> 0 - Cs*Ad
	{ BLEND_NO_REC             , OP_ADD          , INV_CONST_COLOR , CONST_ZERO}      , // 2020: (0  - Cs)*F  + Cs ==> Cs*(1 - F)
	{ BLEND_ACCU               , OP_REV_SUBTRACT , CONST_COLOR     , CONST_ONE}       , // 2021: (0  - Cs)*F  + Cd ==> Cd - Cs*F
	{ BLEND_NO_REC             , OP_REV_SUBTRACT , CONST_COLOR     , CONST_ZERO}      , // 2022: (0  - Cs)*F  +  0 ==> 0 - Cs*F
	{ BLEND_HW4                , OP_SUBTRACT     , CONST_ONE       , SRC1_COLOR}      , // 2100: (0  - Cd)*As + Cs ==> Cs - Cd*As
	{ 0                        , OP_ADD          , CONST_ZERO      , INV_SRC1_COLOR}  , // 2101: (0  - Cd)*As + Cd ==> Cd*(1 - As)
	{ 0                        , OP_SUBTRACT     , CONST_ZERO      , SRC1_COLOR}      , // 2102: (0  - Cd)*As +  0 ==> 0 - Cd*As
	{ BLEND_HW6                , OP_SUBTRACT     , CONST_ONE       , DST_ALPHA}       , // 2110: (0  - Cd)*Ad + Cs ==> Cs - Cd*Ad
	{ BLEND_HW7                , OP_ADD          , CONST_ZERO      , INV_DST_ALPHA}   , // 2111: (0  - Cd)*Ad + Cd ==> Cd*(1 - Ad)
	{ 0                        , OP_SUBTRACT     , CONST_ZERO      , DST_ALPHA}       , // 2112: (0  - Cd)*Ad +  0 ==> 0 - Cd*Ad
	{ BLEND_HW4                , OP_SUBTRACT     , CONST_ONE       , CONST_COLOR}     , // 2120: (0  - Cd)*F  + Cs ==> Cs - Cd*F
	{ 0                        , OP_ADD          , CONST_ZERO      , INV_CONST_COLOR} , // 2121: (0  - Cd)*F  + Cd ==> Cd*(1 - F)
	{ 0                        , OP_SUBTRACT     , CONST_ZERO      , CONST_COLOR}     , // 2122: (0  - Cd)*F  +  0 ==> 0 - Cd*F
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 2200: (0  -  0)*As + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 2201: (0  -  0)*As + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 2202: (0  -  0)*As +  0 ==> 0
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 2210: (0  -  0)*Ad + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 2211: (0  -  0)*Ad + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 2212: (0  -  0)*Ad +  0 ==> 0
	{ BLEND_NO_REC             , OP_ADD          , CONST_ONE       , CONST_ZERO}      , // 2220: (0  -  0)*F  + Cs ==> Cs
	{ BLEND_CD                 , OP_ADD          , CONST_ZERO      , CONST_ONE}       , // 2221: (0  -  0)*F  + Cd ==> Cd
	{ BLEND_NO_REC             , OP_ADD          , CONST_ZERO      , CONST_ZERO}      , // 2222: (0  -  0)*F  +  0 ==> 0
}};
