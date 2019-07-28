/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "ParkingLot.h"

#include <array>

namespace parking_lot::detail {
Bucket &Bucket::bucketFor(size_t key) {
  constexpr size_t kNumBuckets = 1 << 16;

  // Statically allocating this lets us use this in allocation-sensitive
  // contexts. This relies on the assumption that std::mutex won't dynamically
  // allocate memory, which we assume to be the case on Linux and iOS.
  static std::array<Bucket, kNumBuckets> gBuckets;
  return gBuckets[key % kNumBuckets];
}

std::atomic<uint64_t> idallocator{0};

} // namespace parking_lot::detail