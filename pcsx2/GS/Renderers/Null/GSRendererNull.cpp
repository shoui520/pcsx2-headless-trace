// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GSRendererNull.h"

GSRendererNull::GSRendererNull() = default;

void GSRendererNull::VSync(u32 field, bool registers_written, bool idle_frame)
{
#ifdef PCSX2_TRACE_ONLY
	m_draw_transfers.clear();
	return;
#else
	GSRenderer::VSync(field, registers_written, idle_frame);

	m_draw_transfers.clear();
#endif
}

void GSRendererNull::Draw()
{
}

GSTexture* GSRendererNull::GetOutput(int i, float& scale, int& y_offset)
{
	return nullptr;
}
