#include "../PpsCapture.h"

#include <assert.h>
#include <stdint.h>

using PpsCapture::detail::elapsedTicks;
using PpsCapture::detail::capturedTicks;

// Verifies timer extension and capture reconstruction across counter wraps and ambiguous arrivals.
int main() {
  uint32_t elapsed = 0;
  assert(elapsedTicks(400, 4, 100, 200, &elapsed) && elapsed == 100);
  // Capture/current-counter and reload races on either side of zero.
  assert(elapsedTicks(80, 4, 65530, 14, &elapsed) && elapsed == 20);
  assert(elapsedTicks(65536U * 4, 4, 1234, 1234, &elapsed) && elapsed == 65536);
  assert(elapsedTicks((65536U - 2) * 4, 4, 3, 1, &elapsed) && elapsed == 65534);
  assert(elapsedTicks((65536U + 2) * 4, 4, 3, 5, &elapsed) && elapsed == 65538);
  // Multiple coalesced reloads preserve the full elapsed interval.
  const uint32_t delayed = 1500000;
  assert(elapsedTicks(delayed * 4, 4, 65000,
                      static_cast<uint16_t>(65000 + delayed), &elapsed));
  assert(elapsed == delayed);
  // Small sampling latency around a wrap cannot select another timer epoch.
  assert(elapsedTicks(65536U * 4 - 12, 4, 1234, 1234, &elapsed));
  assert(elapsed == 65536);
  assert(elapsedTicks(65536U * 4 + 12, 4, 1234, 1234, &elapsed));
  assert(elapsed == 65536);
  // Caller subtraction also handles the 32-bit DWT counter wrapping.
  const uint32_t beforeCpu = UINT32_MAX - 100;
  const uint32_t afterCpu = beforeCpu + 400;
  assert(elapsedTicks(afterCpu - beforeCpu, 4, 42, 142, &elapsed));
  assert(elapsed == 100);
  // A stopped/reconfigured timer or impossible interval is rejected.
  assert(!elapsedTicks(20000, 4, 100, 100, &elapsed));
  assert(!elapsedTicks(0, 4, 100, 99, &elapsed));
  assert(!elapsedTicks(100, 0, 0, 1, &elapsed));
  assert(!elapsedTicks(100, 4, 0, 25, nullptr));

  // Capture just before reload, serviced just after: use the preceding epoch.
  uint32_t edge = 0;
  assert(capturedTicks(0x12000AU, 10, 65530, 100, 1, &edge));
  assert(edge == 0x11FFFAU);
  // Capture after reload uses the current epoch regardless of pending RF.
  assert(capturedTicks(0x12000AU, 10, 3, 100, 1, &edge));
  assert(edge == 0x120003U);
  // The extended 32-bit timebase can wrap at the same time as the low timer.
  assert(capturedTicks(10, 10, 65530, 100, 1, &edge));
  assert(edge == UINT32_MAX - 5);
  // A PPS just before wrap remains unambiguous when its ISR runs a few ticks
  // after the reload. A blanket service-gap>=65536 rejection would lose it.
  assert(capturedTicks(0x120018U, 24, 65530, 65560, 1, &edge));
  assert(edge == 0x11FFFAU);
  // At this exact boundary both age=10 and age=65546 fit the service window.
  assert(!capturedTicks(100, 100, 90, 65546, 1, &edge));
  assert(capturedTicks(100, 100, 90, 65545, 1, &edge) && edge == 90);
  // A capture timestamp earlier than any possible FIFO arrival is invalid.
  assert(!capturedTicks(100, 100, 90, 9, 1, &edge));
  assert(!capturedTicks(100, 100, 90, 65535, 0, &edge));
  assert(!capturedTicks(100, 100, 90, 65535, 2, &edge));
  assert(!capturedTicks(100, 100, 90, 65535, 1, nullptr));
  return 0;
}
