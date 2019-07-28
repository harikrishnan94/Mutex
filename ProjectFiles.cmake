# Copyright (c) <year> <author> (<email>) Distributed under the MIT License. See accompanying file
# LICENSE.md or copy at http://opensource.org/licenses/MIT

# Set project source files.
set(SRC "${SRC_PATH}/ThreadRegistry.cpp" "${SRC_PATH}/TraceLog.cpp" "${SRC_PATH}/Mutex.cpp")

set(BENCH_SRC_PATH "${SRC_PATH}/benchmark")
set(BENCH_SRC "${BENCH_SRC_PATH}/benchMutex.cpp")
set(BENCH2_SRC "${BENCH_SRC_PATH}/benchMutex2.cpp")

# Set project benchmark files. set(BENCHMARK_SRC "${SRC_PATH}/benchmark.cpp")

# Set project test source files.
set(TEST_SRC
    "${TEST_SRC_PATH}/testBase.cpp"
    "${TEST_SRC_PATH}/testParkingLot.cpp"
    "${TEST_SRC_PATH}/testMutex.cpp")
