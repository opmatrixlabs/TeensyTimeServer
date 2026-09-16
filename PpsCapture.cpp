#include "PpsCapture.h"

namespace PpsCapture {
namespace detail {

// Reconstructs elapsed timer ticks across counter wraps using the CPU counter as a reference.
bool elapsedTicks(uint32_t elapsedCpuCycles,
                  uint32_t cpuCyclesPerTick,
                  uint16_t previousCounter,
                  uint16_t currentCounter,
                  uint32_t* elapsed) {
  if (elapsed == nullptr || cpuCyclesPerTick == 0)
    return false;

  const uint32_t expected = elapsedCpuCycles / cpuCyclesPerTick;
  const uint32_t low = static_cast<uint16_t>(currentCounter - previousCounter);
  uint32_t candidate = (expected & 0xFFFF0000UL) | low;
  // Select the nearest congruent timer interval, including either side of a
  // 16-bit wrap. Sampling CNT and DWT is not simultaneous; a small discrepancy
  // is expected, but cannot change the fine timing taken from CNT/CVAL0.
  if (candidate < expected && expected - candidate > 32768U) {
    if (candidate > UINT32_MAX - 65536U)
      return false;
    candidate += 65536U;
  } else if (candidate > expected && candidate - expected > 32768U) {
    if (candidate < 65536U)
      return false;
    candidate -= 65536U;
  }
  const uint32_t discrepancy = candidate > expected
                                   ? candidate - expected
                                   : expected - candidate;
  // Far larger than normal adjacent register-read latency, but much smaller
  // than the half-period needed to choose the wrong timer epoch.
  if (discrepancy > 1024U)
    return false;
  *elapsed = candidate;
  return true;
}

// Resolves a captured pulse's full timestamp only when its timer epoch is unambiguous.
bool capturedTicks(uint32_t currentTicks,
                   uint16_t currentCounter,
                   uint16_t capturedCounter,
                   uint32_t ticksSinceService,
                   uint8_t queuedCaptures,
                   uint32_t* edge) {
  if (edge == nullptr || queuedCaptures != 1)
    return false;
  const uint32_t age = static_cast<uint16_t>(currentCounter - capturedCounter);
  // The entry arrived after the previous FIFO observation. Exactly one
  // congruent capture epoch must fit that interval: age, not age+65536.
  if (age > ticksSinceService || ticksSinceService - age >= 65536U)
    return false;
  *edge = currentTicks - age;
  return true;
}

}  // namespace detail
}  // namespace PpsCapture

#if !defined(PPS_CAPTURE_MATH_TEST)

#include <Arduino.h>

#if !defined(__IMXRT1062__) || !defined(ARDUINO_TEENSY_MICROMOD)
#error PpsCapture requires the Teensy MicroMod target
#endif

namespace PpsCapture {
namespace {

constexpr uint8_t PIN = 0;
constexpr uint8_t SUBMODULE = 1;
constexpr uint16_t SUBMODULE_BIT = 1U << SUBMODULE;
constexpr uint16_t OUTPUT_MASK = FLEXPWM_OUTEN_PWMA_EN(SUBMODULE_BIT) |
                                 FLEXPWM_OUTEN_PWMB_EN(SUBMODULE_BIT) |
                                 FLEXPWM_OUTEN_PWMX_EN(SUBMODULE_BIT);
constexpr uint16_t CAPTURE_MODE = FLEXPWM_SMCAPTCTRLX_EDGX0(2) |
                                  FLEXPWM_SMCAPTCTRLX_ARMX;
constexpr uint32_t MAX_SERVICE_GAP_MILLIS = 500;

volatile bool running = false;
volatile bool healthy = false;
volatile uint32_t counterTicks = 0;
volatile uint16_t counterLow = 0;
volatile uint32_t counterCpu = 0;
volatile uint32_t counterMillis = 0;
volatile uint32_t captureWindowStartTicks = 0;
volatile uint32_t pulseCount = 0;
volatile uint32_t edgeTicks = 0;
volatile uint32_t intervalTicks = 0;
volatile uint32_t edgeMicros = 0;
volatile uint32_t intervalMicros = 0;
volatile uint32_t invalidPulseCount = 0;
volatile bool hasPulse = false;
uint32_t timerHz = 0;
uint32_t cpuHz = 0;
uint32_t cpuCyclesPerTick = 0;
uint32_t savedPinMux = 0;
uint32_t savedPinPad = 0;

// All accesses to shared timer state use the caller's original PRIMASK.
class InterruptLock {
 public:
  // Saves the current interrupt mask and disables interrupts while shared state is accessed.
  InterruptLock() {
    asm volatile("mrs %0, primask" : "=r"(primask_) :: "memory");
    __disable_irq();
  }
  // Restores interrupt delivery only if interrupts were enabled before the lock was acquired.
  ~InterruptLock() {
    if ((primask_ & 1U) == 0)
      __enable_irq();
  }
 private:
  uint32_t primask_;
};

// Marks the timer unusable and disables its interrupts after a continuity failure.
void invalidateTimer() {
  healthy = false;
  hasPulse = false;
  ++invalidPulseCount;
  IMXRT_FLEXPWM1.SM[SUBMODULE].INTEN = 0;
}

// Samples the timer and CPU counters together to extend and validate the running timestamp.
// Call only with interrupts masked. The 500 ms bound keeps subtraction well
// inside the DWT wrap at the supported clock rates. Reload IRQs normally run
// every 65536 timer ticks. Debug halts and multi-second global IRQ blackouts
// are unsupported; stop/restart this driver around such operations.
bool sampleCounter(uint32_t* ticks, uint16_t* low, uint32_t* cpu,
                   uint32_t* nowMillis, uint32_t* elapsed) {
  if (!running || !healthy || F_CPU_ACTUAL != cpuHz || F_BUS_ACTUAL != timerHz)
    return false;
  *nowMillis = millis();
  *cpu = ARM_DWT_CYCCNT;
  *low = IMXRT_FLEXPWM1.SM[SUBMODULE].CNT;
  const uint32_t cpuElapsed = *cpu - counterCpu;
  if (*nowMillis - counterMillis > MAX_SERVICE_GAP_MILLIS ||
      cpuElapsed > cpuHz / 2U ||
      !detail::elapsedTicks(cpuElapsed, cpuCyclesPerTick, counterLow, *low, elapsed))
    return false;
  *ticks = counterTicks + *elapsed;
  return true;
}

// Converts timer ticks to the nearest whole microsecond for coarse pulse-age tracking.
uint32_t ticksToMicros(uint32_t ticks) {
  return static_cast<uint32_t>((static_cast<uint64_t>(ticks) * 1000000ULL +
                                timerHz / 2U) / timerHz);
}

// Services timer reloads and captured PPS edges while rejecting ambiguous or invalid pulse timestamps.
void captureIsr() {
  InterruptLock lock;
  if (!running || !healthy)
    return;

  auto& timer = IMXRT_FLEXPWM1.SM[SUBMODULE];
  uint32_t nowTicks, nowCpu, nowMillis, elapsed;
  uint16_t nowLow;
  if (!sampleCounter(&nowTicks, &nowLow, &nowCpu, &nowMillis, &elapsed)) {
    invalidateTimer();
    asm volatile("dsb" ::: "memory");
    return;
  }
  // This sample precedes the FIFO observation. An edge that arrives between
  // that observation and the final CNT sample is therefore still inside the
  // next ISR's conservative capture window.
  const uint32_t fifoObservationTicks = nowTicks;
  const uint16_t flags = timer.STS;
  const uint8_t queued = (timer.CAPTCTRLX >> 10) & 7U;

  if (queued != 0 || (flags & FLEXPWM_SMSTS_CFX0) != 0) {
    const uint16_t captured = timer.CVAL0;
    timer.STS = FLEXPWM_SMSTS_CFX0;
    // Read CNT after popping CVAL0, so the captured edge cannot be newer than
    // the counter used to determine its age. FIFO count is also checked on
    // each reload, even if a new edge races with clearing the capture flag.
    if (!sampleCounter(&nowTicks, &nowLow, &nowCpu, &nowMillis, &elapsed)) {
      invalidateTimer();
      asm volatile("dsb" ::: "memory");
      return;
    }
    uint32_t capturedTicks;
    if (!detail::capturedTicks(nowTicks, nowLow, captured,
                              nowTicks - captureWindowStartTicks, queued,
                              &capturedTicks)) {
      // Discard an entry if more than one timer epoch could fit its arrival
      // window. A FIFO with an unexpected count is also unsafe.
      for (uint8_t i = 1; i < queued && i < 4; ++i)
        (void)timer.CVAL0;
      timer.CAPTCTRLX = 0;
      timer.CAPTCTRLX = CAPTURE_MODE;
      ++invalidPulseCount;
      pulseCount += queued == 0 ? 1U : queued;
      hasPulse = false;
      intervalTicks = 0;
      intervalMicros = 0;
    } else {
      const uint32_t captureAge = static_cast<uint16_t>(nowLow - captured);
      const uint32_t capturedMicros = micros() -
          ticksToMicros(static_cast<uint32_t>(ARM_DWT_CYCCNT - nowCpu) /
                            cpuCyclesPerTick + captureAge);
      const uint32_t interval = hasPulse ? capturedTicks - edgeTicks : 0;
      if (hasPulse && (interval < timerHz - timerHz / 1000U ||
                       interval > timerHz + timerHz / 1000U))
        ++invalidPulseCount;
      ++pulseCount;
      intervalTicks = interval;
      intervalMicros = ticksToMicros(interval);
      edgeTicks = capturedTicks;
      edgeMicros = capturedMicros;
      hasPulse = true;
    }
  }

  // A reload that occurs after the flags snapshot remains pending and invokes
  // the ISR again. No epoch decision depends on this flag or its clear order.
  if (flags & FLEXPWM_SMSTS_RF)
    timer.STS = FLEXPWM_SMSTS_RF;
  counterTicks = nowTicks;
  counterLow = nowLow;
  counterCpu = nowCpu;
  counterMillis = nowMillis;
  captureWindowStartTicks = fifoObservationTicks;
  asm volatile("dsb" ::: "memory");
}

}  // namespace

// Configures pin 0 and its FlexPWM timer for PPS capture after checking clock compatibility and timer ownership.
bool begin() {
  InterruptLock lock;
  if (running)
    return healthy;
  timerHz = F_BUS_ACTUAL;
  cpuHz = F_CPU_ACTUAL;
  if (timerHz == 0 || cpuHz < timerHz || cpuHz % timerHz != 0)
    return false;
  auto& timer = IMXRT_FLEXPWM1.SM[SUBMODULE];
  CCM_CCGR4 |= CCM_CCGR4_PWM1(CCM_CCGR_ON);
  if ((IMXRT_FLEXPWM1.OUTEN & OUTPUT_MASK) != 0 ||
      timer.INTEN != 0 || timer.DMAEN != 0)
    return false;

  cpuCyclesPerTick = cpuHz / timerHz;
  savedPinMux = CORE_PIN0_CONFIG;
  savedPinPad = CORE_PIN0_PADCONFIG;
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM1_1);
  IMXRT_FLEXPWM1.MCTRL &= ~FLEXPWM_MCTRL_RUN(SUBMODULE_BIT);
  IMXRT_FLEXPWM1.MCTRL |= FLEXPWM_MCTRL_CLDOK(SUBMODULE_BIT);
  timer.CTRL2 = FLEXPWM_SMCTRL2_INDEP;
  timer.CTRL = FLEXPWM_SMCTRL_HALF;
  timer.INIT = 0;
  timer.VAL0 = 0;
  timer.VAL1 = 65535;
  timer.VAL2 = timer.VAL3 = timer.VAL4 = timer.VAL5 = 0;
  timer.CAPTCTRLA = timer.CAPTCTRLB = timer.CAPTCTRLX = 0;
  timer.CAPTCOMPX = 0;
  timer.STS = 0xFFFF;
  // Pin mux + software-input-on matches PJRC's FreqMeasureMulti pin-0 route:
  // https://github.com/PaulStoffregen/FreqMeasureMulti/blob/master/FreqMeasureMultiIMXRT.cpp
  pinMode(PIN, INPUT);
  CORE_PIN0_CONFIG = 4 | 0x10;
  ARM_DEMCR |= ARM_DEMCR_TRCENA;
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
  pulseCount = edgeTicks = intervalTicks = edgeMicros = intervalMicros = 0;
  invalidPulseCount = 0;
  hasPulse = false;
  IMXRT_FLEXPWM1.MCTRL |= FLEXPWM_MCTRL_LDOK(SUBMODULE_BIT) |
                           FLEXPWM_MCTRL_RUN(SUBMODULE_BIT);
  counterMillis = millis();
  counterCpu = ARM_DWT_CYCCNT;
  counterLow = timer.CNT;
  counterTicks = counterLow;
  captureWindowStartTicks = counterTicks;
  running = healthy = true;
  timer.CAPTCTRLX = CAPTURE_MODE;
  timer.STS = 0xFFFF;
  attachInterruptVector(IRQ_FLEXPWM1_1, captureIsr);
  NVIC_SET_PRIORITY(IRQ_FLEXPWM1_1, 32);
  NVIC_CLEAR_PENDING(IRQ_FLEXPWM1_1);
  timer.INTEN = FLEXPWM_SMINTEN_CX0IE | FLEXPWM_SMINTEN_RIE;
  NVIC_ENABLE_IRQ(IRQ_FLEXPWM1_1);
  return true;
}

// Stops PPS capture and its timer interrupts, then restores the pin's previous configuration.
void end() {
  InterruptLock lock;
  if (!running)
    return;
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM1_1);
  auto& timer = IMXRT_FLEXPWM1.SM[SUBMODULE];
  timer.INTEN = 0;
  timer.CAPTCTRLX = 0;
  IMXRT_FLEXPWM1.MCTRL &= ~FLEXPWM_MCTRL_RUN(SUBMODULE_BIT);
  timer.STS = 0xFFFF;
  NVIC_CLEAR_PENDING(IRQ_FLEXPWM1_1);
  CORE_PIN0_CONFIG = savedPinMux;
  CORE_PIN0_PADCONFIG = savedPinPad;
  running = healthy = hasPulse = false;
}

// Returns the configured capture timer frequency in ticks per second.
uint32_t ticksPerSecond() { return timerHz; }

// Reads the extended timer timestamp and rejects it if the timer is stopped or continuity is uncertain.
bool readTicks(uint32_t* ticks) {
  if (ticks == nullptr)
    return false;
  *ticks = 0;
  InterruptLock lock;
  uint32_t nowCpu, nowMillis, elapsed;
  uint16_t low;
  if (!sampleCounter(ticks, &low, &nowCpu, &nowMillis, &elapsed)) {
    if (running && healthy)
      invalidateTimer();
    return false;
  }
  return true;
}

// Copies a coherent pulse-capture snapshot and reports whether it contains a valid edge from a healthy timer.
bool snapshot(Snapshot* result) {
  if (result == nullptr)
    return false;
  InterruptLock lock;
  result->pulseCount = pulseCount;
  result->edgeTicks = edgeTicks;
  result->intervalTicks = intervalTicks;
  result->edgeMicros = edgeMicros;
  result->intervalMicros = intervalMicros;
  result->invalidPulseCount = invalidPulseCount;
  result->hasPulse = running && hasPulse;
  result->timerHealthy = running && healthy &&
                         F_CPU_ACTUAL == cpuHz && F_BUS_ACTUAL == timerHz;
  return result->hasPulse && result->timerHealthy;
}

}  // namespace PpsCapture

#endif  // !PPS_CAPTURE_MATH_TEST
