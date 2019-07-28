#include <folly/synchronization/ParkingLot.h>

#include <iostream>
#include <thread>

#include "catch.hpp"

enum ProdConsFlag { PRODUCED, CONSUMED };

template <typename Data> using ParkingLot = folly::ParkingLot<Data>;
using UnparkControl = folly::UnparkControl;

TEST_CASE("ParkingLot", "Futex - 2 Thread") {
  ParkingLot<std::nullptr_t> parkinglot;
  std::atomic<ProdConsFlag> flag = PRODUCED;

  std::atomic_bool quit = false;
  auto t = std::thread{[&flag, &quit, &parkinglot] {
    while (!quit) {
      parkinglot.park(&flag, nullptr,
                      [&quit, &flag]() { return !quit && flag == CONSUMED; },
                      []() {});

      if (!quit)
        REQUIRE(flag == PRODUCED);

      flag.store(CONSUMED);
      parkinglot.unpark(&flag, [](auto) { return UnparkControl::RemoveBreak; });
    }
  }};

  for (int i = 0; i < 100000; i++) {
    parkinglot.park(&flag, nullptr, [&flag]() { return flag == PRODUCED; },
                    []() {});

    REQUIRE(flag == CONSUMED);

    flag.store(PRODUCED);
    parkinglot.unpark(&flag, [](auto) { return UnparkControl::RemoveBreak; });
  }

  quit = true;
  parkinglot.unpark(&flag, [](auto) { return UnparkControl::RemoveBreak; });

  t.join();
}
