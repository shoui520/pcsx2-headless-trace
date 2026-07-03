// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "DEV9/DEV9.h"

#include <cstring>

dev9Struct dev9 = {};
int ThreadRun = 0;

void rx_process(NetPacket* pk)
{
}

bool rx_fifo_can_rx()
{
	return false;
}

void FLASHinit()
{
}

s32 DEV9init()
{
	std::memset(&dev9, 0, sizeof(dev9));
	return 0;
}

void DEV9close()
{
}

s32 DEV9open()
{
	return 0;
}

void DEV9shutdown()
{
}

u32 FLASHread32(u32 addr, int size)
{
	return 0;
}

void FLASHwrite32(u32 addr, u32 value, int size)
{
}

void _DEV9irq(int cause, int cycles)
{
}

int DEV9irqHandler(void)
{
	return 0;
}

void DEV9async(u32 cycles)
{
}

void DEV9runFIFO()
{
}

void DEV9writeDMA8Mem(u32* pMem, int size)
{
}

void DEV9readDMA8Mem(u32* pMem, int size)
{
	if (pMem && size > 0)
		std::memset(pMem, 0, static_cast<size_t>(size) * sizeof(u32));
}

u8 DEV9read8(u32 addr)
{
	return 0;
}

u16 DEV9read16(u32 addr)
{
	return 0;
}

u32 DEV9read32(u32 addr)
{
	return 0;
}

void DEV9write8(u32 addr, u8 value)
{
}

void DEV9write16(u32 addr, u16 value)
{
}

void DEV9write32(u32 addr, u32 value)
{
}

void DEV9CheckChanges(const Pcsx2Config& old_config)
{
}
