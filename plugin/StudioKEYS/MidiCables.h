/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Four USB-MIDI cables on the SMK-37 / StudioKEYS session (VST_IMPLEMENTATION_PLAN.md).
// Indices match the USB-MIDI cable field (0..3). Product copy numbers ports 1..4.
namespace skmidi {

enum class Cable : uint8_t
{
  Performance = 0, // cable 0 / port 1 — notes the DAW records; the synth
  Live = 1,        // cable 1 / port 2 — Ableton Live remote script
  Hui = 2,         // cable 2 / port 3 — Mackie HUI
  Link = 3,        // cable 3 / port 4 — plugin↔keyboard sync/lock; never notes
  Count = 4
};

inline constexpr const char* CableName(Cable c)
{
  switch (c)
  {
    case Cable::Performance: return "Performance";
    case Cable::Live: return "Live";
    case Cable::Hui: return "HUI";
    case Cable::Link: return "Link";
    default: return "?";
  }
}

inline constexpr bool AcceptsNotes(Cable c)
{
  return c != Cable::Link;
}

// Lock-free-ish ring stubs. Real host queues land in a later phase.
template <size_t Capacity = 256>
class MidiCableQueue
{
public:
  bool Push(uint8_t status, uint8_t data1, uint8_t data2, uint8_t cable)
  {
    const size_t w = mWrite.load(std::memory_order_relaxed);
    const size_t next = (w + 1) % Capacity;
    if (next == mRead.load(std::memory_order_acquire))
      return false;

    mBuf[w] = {status, data1, data2, cable};
    mWrite.store(next, std::memory_order_release);
    return true;
  }

  bool Pop(uint8_t& status, uint8_t& data1, uint8_t& data2, uint8_t& cable)
  {
    const size_t r = mRead.load(std::memory_order_relaxed);
    if (r == mWrite.load(std::memory_order_acquire))
      return false;

    const Entry e = mBuf[r];
    status = e.status;
    data1 = e.data1;
    data2 = e.data2;
    cable = e.cable;
    mRead.store((r + 1) % Capacity, std::memory_order_release);
    return true;
  }

  void Clear()
  {
    mRead.store(0, std::memory_order_relaxed);
    mWrite.store(0, std::memory_order_relaxed);
  }

private:
  struct Entry
  {
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    uint8_t cable = 0;
  };

  std::array<Entry, Capacity> mBuf {};
  std::atomic<size_t> mRead {0};
  std::atomic<size_t> mWrite {0};
};

class MidiRouter
{
public:
  bool Enqueue(Cable cable, uint8_t status, uint8_t data1, uint8_t data2)
  {
    const uint8_t type = static_cast<uint8_t>(status & 0xF0);
    const bool isNote = (type == 0x90 || type == 0x80);
    if (isNote && !AcceptsNotes(cable))
      return false;

    return mCables[static_cast<size_t>(cable)].Push(status, data1, data2, static_cast<uint8_t>(cable));
  }

  MidiCableQueue<>& Queue(Cable cable) { return mCables[static_cast<size_t>(cable)]; }

  void ClearAll()
  {
    for (auto& q : mCables)
      q.Clear();
  }

private:
  std::array<MidiCableQueue<>, static_cast<size_t>(Cable::Count)> mCables {};
};

} // namespace skmidi
