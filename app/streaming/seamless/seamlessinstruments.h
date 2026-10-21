/**
 * @file app/streaming/seamless/seamlessinstruments.h
 * @brief The seamless instruments' latest values, for the statistics overlay.
 * @details The log lines and the overlay read the same numbers from here, so the
 *          two cannot disagree: every setter is called by the code that logs.
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace seamless_instruments {
  inline std::atomic<int> windowCount {0};
  inline std::atomic<int> gpuPassUs {0};
  inline std::atomic<int> mainApplyUs {0};
  inline std::atomic<int> drawableW {0};
  inline std::atomic<int> drawableH {0};
  inline std::atomic<int> drawableScaleX100 {100};
}
