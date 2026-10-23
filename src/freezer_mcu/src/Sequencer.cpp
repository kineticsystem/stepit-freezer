// Copyright 2026 Giovanni Remigi
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the Giovanni Remigi nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "Sequencer.h"

// Arduino Nano R4, RA4M1 at 48 MHz. GPT1, a 32-bit timer, counts PCLKD, 48
// MHz, from 0 to 0xFFFFFFFF: one tick is 1/48 µs, and the counter wraps after
// about 89 s, which unsigned arithmetic absorbs. Its compare match A marks the
// end of each step. The core reserves GPT1 for the PWM of D3 and D11: the
// board drives D3 as a plain pin and leaves D11 free.
//
// The shift registers are written through the port's set and reset registers,
// not digitalWrite: the interrupt must shift 16 bits between two steps as short
// as MIN_HOLD_US. The pins are those of main.cpp.

#include <FspTimer.h>

namespace sequencer
{
namespace
{
constexpr uint32_t TICKS_PER_US = 48;

// The shift registers are on port 1: D3 is P104, D4 is P103, D6 is P106.
constexpr uint16_t CLOCK_BIT = 1U << 4;  // D3, SH_CP of the 74HC595.
constexpr uint16_t DATA_BIT = 1U << 3;   // D4, DS of the 74HC595.
constexpr uint16_t LATCH_BIT = 1U << 6;  // D6, ST_CP of the 74HC595.

// The compare match outranks USB (12) and millis() (14): a host talking to
// the controller must not delay a boundary.
constexpr uint8_t MATCH_PRIORITY = 2;
constexpr uint8_t TIMER_CHANNEL = 1;

FspTimer timer;
IRQn_Type matchIrq = FSP_INVALID_VECTOR;

/** Disable interrupts for its scope, and restore them as they were. */
class InterruptLock
{
public:
  InterruptLock() : m_primask{ __get_PRIMASK() }
  {
    __disable_irq();
  }
  ~InterruptLock()
  {
    __set_PRIMASK(m_primask);
  }

private:
  uint32_t m_primask;
};

inline void setBits(uint16_t bits)
{
  R_PORT1->POSR = bits;
}

inline void clearBits(uint16_t bits)
{
  R_PORT1->PORR = bits;
}

/** The time in ticks. */
inline uint32_t ticksNow()
{
  return R_GPT1->GTCNT;
}

/** Arm the compare match for the given time. */
inline void armMatch(uint32_t ticks)
{
  R_GPT1->GTCCR[0] = ticks;  // GTCCRA.
}

/** Drop a compare match that happened and has not been handled. */
inline void clearMatch()
{
  R_GPT1->GTST_b.TCFA = 0;
  R_BSP_IrqStatusClear(matchIrq);
  NVIC_ClearPendingIRQ(matchIrq);
}

inline void enableMatch()
{
  NVIC_EnableIRQ(matchIrq);
}

inline void disableMatch()
{
  NVIC_DisableIRQ(matchIrq);
}

/** The compare-match interrupt. */
void onMatch()
{
  clearMatch();
  runDueBoundaries();
}

void initTimer()
{
  FspTimer::force_use_of_pwm_reserved_timer();
  timer.begin(TIMER_MODE_PERIODIC, GPT_TIMER, TIMER_CHANNEL, 0xFFFFFFFF, 0, TIMER_SOURCE_DIV_1);
  timer.setup_capture_a_irq(MATCH_PRIORITY, onMatch);
  timer.open();
  matchIrq = static_cast<const gpt_extended_cfg_t*>(timer.get_cfg()->p_extend)->capture_a_irq;
  disableMatch();
  // The full 32 bits, which FSP's period, one count short, does not give;
  // and no buffer, which would copy GTCCRC over the target at each wrap.
  R_GPT1->GTBER = 0;
  R_GPT1->GTPR = 0xFFFFFFFF;
  R_GPT1->GTPBR = 0xFFFFFFFF;
  R_GPT1->GTCNT = 0;
  clearMatch();
  timer.start();
}

// The loaded table. Written only while not running, read by the interrupt.
uint16_t outputs[MAX_STEPS];
uint32_t holdTicks[MAX_STEPS];
byte count = 0;
uint32_t durationUs = 0;

// The state of the shot, shared with the interrupt.
volatile bool running = false;
volatile byte step = 0;
volatile uint32_t startTicks = 0;
volatile uint32_t targetTicks = 0;  // The end of the current step.
volatile uint32_t worstLatenessTicks = 0;
uint16_t lastShotId = 0;

/**
 * Shift a pattern into the 74HC595, least significant bit first, without
 * latching it: the outputs keep the previous pattern. Bit 0 ends in the last
 * stage of the chain, which drives the shutter of OUT1.
 */
inline void shiftPattern(uint16_t value)
{
  for (byte i = 0; i < 16; i++)
  {
    if (value & 1)
    {
      setBits(DATA_BIT);
    }
    else
    {
      clearBits(DATA_BIT);
    }
    setBits(CLOCK_BIT);
    clearBits(CLOCK_BIT);
    value >>= 1;
  }
  clearBits(DATA_BIT);
}

/** Copy the shifted pattern to the 16 outputs, all at once. */
inline void latchPattern()
{
  setBits(LATCH_BIT);
  clearBits(LATCH_BIT);
}

/** Arm the compare match for the end of the current step. */
inline void schedule()
{
  armMatch(targetTicks);
}
}  // namespace

/**
 * Handle every boundary that is due: latch the next pattern, or end the shot
 * after the last step. Called by the compare-match interrupt, and by start()
 * after arming it. Call with interrupts disabled.
 *
 * It loops, and checks the time again after arming the next match, because a
 * boundary may be due before the match is armed: a step held for less than
 * the time it takes to shift the next pattern in would otherwise wait for the
 * counter to wrap, 32.8 ms later.
 */
void runDueBoundaries()
{
  while (running)
  {
    const uint32_t now = ticksNow();
    const int32_t lateness = static_cast<int32_t>(now - targetTicks);
    if (lateness < 0)
    {
      return;  // Not due yet, e.g. a match of an earlier wrap of the counter.
    }
    if (static_cast<uint32_t>(lateness) > worstLatenessTicks)
    {
      worstLatenessTicks = lateness;
    }
    if (step + 1 >= count)
    {
      // The end of the last step. Its pattern switched every output off.
      running = false;
      disableMatch();
      return;
    }
    latchPattern();
    step = step + 1;
    targetTicks = targetTicks + holdTicks[step];
    if (step + 1 < count)
    {
      shiftPattern(outputs[step + 1]);
    }
    schedule();
  }
}

void init()
{
  initTimer();
}

void load(const Step* steps, byte stepCount, uint32_t tableDurationUs)
{
  for (byte i = 0; i < stepCount; i++)
  {
    outputs[i] = steps[i].outputs;
    holdTicks[i] = steps[i].holdUs * TICKS_PER_US;
  }
  count = stepCount;
  durationUs = tableDurationUs;
}

bool setOutputs(uint16_t pattern)
{
  InterruptLock lock;
  if (running)
  {
    return false;
  }
  shiftPattern(pattern);
  latchPattern();
  return true;
}

void stop()
{
  InterruptLock lock;
  // The shift stage may hold the next pattern of the shot: zeros replace it.
  running = false;
  disableMatch();
  clearMatch();
  shiftPattern(0);
  latchPattern();
}

void reset()
{
  stop();
  InterruptLock lock;
  count = 0;
  durationUs = 0;
  lastShotId = 0;
  worstLatenessTicks = 0;
}

uint32_t duration()
{
  return durationUs;
}

uint16_t start()
{
  lastShotId = lastShotId == 0xFFFF ? 1 : lastShotId + 1;
  InterruptLock lock;
  shiftPattern(outputs[0]);
  const uint32_t now = ticksNow();
  latchPattern();
  startTicks = now;
  step = 0;
  targetTicks = now + holdTicks[0];
  worstLatenessTicks = 0;
  running = true;
  if (count > 1)
  {
    shiftPattern(outputs[1]);
  }
  schedule();
  clearMatch();  // Drop a match that happened before this shot.
  enableMatch();
  runDueBoundaries();  // The end of step 0 may already be due.
  return lastShotId;
}

Status status()
{
  Status result;
  {
    InterruptLock lock;
    result.running = running;
    result.step = running ? step : 0;
    result.elapsedUs = running ? (ticksNow() - startTicks) / TICKS_PER_US : 0;
    result.worstLatenessUs = (worstLatenessTicks + TICKS_PER_US - 1) / TICKS_PER_US;
  }
  result.loaded = count > 0;
  result.lastShotId = lastShotId;
  return result;
}
}  // namespace sequencer
