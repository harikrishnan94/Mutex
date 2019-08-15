#include "sync_prim/mutex/FairMutex.h"
#include "testMutexUtils.h"

TEST_CASE("FairMutex Basic", "[FairMutex]") {
  MutexBasicTest<sync_prim::mutex::FairMutex>();
}
