#include "sync_prim/mutex/FairMutex.h"
#include "testMutexUtils.h"

TEST_SUITE_BEGIN("FairMutex");

TEST_CASE("FairMutex Basic") { MutexBasicTest<sync_prim::mutex::FairMutex>(); }

TEST_SUITE_END();