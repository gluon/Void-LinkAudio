// ============================================================================
// VoidLinkAudio - AudioRingBuffer (lock-free SPSC ring buffer for audio samples)
//
// Part of the VoidLinkAudio R&D project by Julien Bayle / Structure Void.
// https://julienbayle.net    https://structure-void.com
//
// Shared core: this file is compiled into every host (TouchDesigner CHOPs,
// Max externals, VCV Rack modules, VST3/AU plugins, openFrameworks addon).
//
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Julien Bayle / Structure Void
//
// VoidLinkAudio is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details — full text in LICENSE at
// the repo root, or at <https://www.gnu.org/licenses/gpl-2.0.html>.
//
// Built on top of Ableton Link Audio (GPL v2+, see ACKNOWLEDGEMENTS.md).
// ============================================================================

#pragma once

// ============================================================================
// AudioRingBuffer
//
// Single-producer / single-consumer lock-free ring buffer for interleaved
// float audio samples. Power-of-two capacity for cheap modulo via mask.
//
// Producer (Link Audio thread): writes samples received from the network
// after int16 -> float32 conversion.
// Consumer (TD cook thread): pops samples in execute() to fill the CHOP
// output channels.
//
// Header-only. No allocations after construction.
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

class AudioRingBuffer
{
public:
    // capacity will be rounded up to the next power of two so we can use
    // a bitmask instead of modulo.
    explicit AudioRingBuffer(std::size_t requestedCapacity = 16384)
    {
        std::size_t cap = 1;
        while (cap < requestedCapacity) cap <<= 1;
        mBuffer.assign(cap, 0.0f);
        mMask = cap - 1;
        mCapacity = cap;
    }

    // ---- Producer side ---------------------------------------------------

    /// Write up to `count` samples. Returns number actually written.
    /// Drops on overflow rather than blocking.
    std::size_t write(const float* src, std::size_t count) noexcept
    {
        const std::size_t w = mWriteIdx.load(std::memory_order_relaxed);
        const std::size_t r = mReadIdx.load(std::memory_order_acquire);

        const std::size_t free = mCapacity - (w - r);
        const std::size_t toWrite = (count < free) ? count : free;

        for (std::size_t i = 0; i < toWrite; ++i)
            mBuffer[(w + i) & mMask] = src[i];

        mWriteIdx.store(w + toWrite, std::memory_order_release);
        return toWrite;
    }

    // ---- Consumer side ---------------------------------------------------

    /// Read up to `count` samples into `dst`. Returns number actually read.
    std::size_t read(float* dst, std::size_t count) noexcept
    {
        const std::size_t r = mReadIdx.load(std::memory_order_relaxed);
        const std::size_t w = mWriteIdx.load(std::memory_order_acquire);

        const std::size_t avail = w - r;
        const std::size_t toRead = (count < avail) ? count : avail;

        for (std::size_t i = 0; i < toRead; ++i)
            dst[i] = mBuffer[(r + i) & mMask];

        mReadIdx.store(r + toRead, std::memory_order_release);
        return toRead;
    }

    /// Number of samples currently readable (consumer-side hint).
    std::size_t available() const noexcept
    {
        const std::size_t r = mReadIdx.load(std::memory_order_relaxed);
        const std::size_t w = mWriteIdx.load(std::memory_order_acquire);
        return w - r;
    }

    /// Number of samples that can be written right now (producer-side hint).
    std::size_t writeAvailable() const noexcept
    {
        const std::size_t w = mWriteIdx.load(std::memory_order_relaxed);
        const std::size_t r = mReadIdx.load(std::memory_order_acquire);
        return mCapacity - (w - r);
    }

    /// Reset both indices. NOT thread-safe; call only when no producer or
    /// consumer is active (e.g. between subscribe/unsubscribe).
    void reset() noexcept
    {
        mReadIdx.store(0, std::memory_order_relaxed);
        mWriteIdx.store(0, std::memory_order_relaxed);
    }

    std::size_t capacity() const noexcept { return mCapacity; }

private:
    std::vector<float>       mBuffer;
    std::size_t              mCapacity = 0;
    std::size_t              mMask     = 0;
    std::atomic<std::size_t> mWriteIdx{0};
    std::atomic<std::size_t> mReadIdx{0};
};
