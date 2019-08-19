#include "sync_prim/ThreadRegistry.h"
#include "sync_prim/barrier.h"
#include "sync_prim/mutex/FairMutex.h"
#include "sync_prim/mutex/Mutex.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/moment.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/program_options.hpp>

enum MutexType { STD_MUTEX, MUTEX, FAIR_MUTEX };

struct Args {
  MutexType mtype;
  int num_threads;
  int num_seconds;
};

static std::int64_t calculate_mean(const std::vector<std::int64_t> &vals);
static std::int64_t calculate_mad(const std::vector<std::int64_t> &vals);

template <typename Mutex>
static void worker(std::int64_t &counter, sync_prim::barrier &b,
                   std::atomic<bool> &quit) {
  sync_prim::ThreadRegistry::RegisterThread();

  b.arrive_and_wait();

  std::int64_t count = 0;
  while (!quit) {
    static Mutex m;
    std::lock_guard lock{m};
    count++;
  }

  counter = count;

  sync_prim::ThreadRegistry::UnregisterThread();
}

template <typename Mutex> static void start_test(Args args) {
  std::vector<std::thread> workers;
  std::vector<std::int64_t> counters(args.num_threads);
  std::atomic<bool> quit = false;
  sync_prim::barrier b{args.num_threads + 1};

  for (int i = 0; i < args.num_threads; i++) {
    workers.emplace_back(worker<Mutex>, std::ref(counters[i]), std::ref(b),
                         std::ref(quit));
  }

  b.arrive_and_wait();
  std::this_thread::sleep_for(std::chrono::seconds{args.num_seconds});
  quit = true;

  for (auto &worker : workers) {
    worker.join();
  }

  auto mad = calculate_mad(counters);
  auto mean = calculate_mean(counters);

  std::cout << "Mean Average Deviation (Lower is better) = " << mad << " ("
            << std::fixed << std::setprecision(2) << (mad * 100.0 / mean)
            << "%)\n";
}

static void start_test(Args args) {
  switch (args.mtype) {
  case MutexType::STD_MUTEX:
    start_test<std::mutex>(args);
    break;

  case MutexType::MUTEX:
    start_test<sync_prim::mutex::DeadlockSafeMutex>(args);
    break;

  case MutexType::FAIR_MUTEX:
    start_test<sync_prim::mutex::FairDeadlockSafeMutex>(args);
    break;

  default:
    break;
  }
}

int main(int argc, char *argv[]) {
  namespace po = boost::program_options;

  po::options_description options{"Mutex Fairness Test"};

  options.add_options()("help,h", "Display this help message");

  options.add_options()("mutex,m", po::value<MutexType>()->required(),
                        "Mutex Type to test one of (std, mutex, fair)");
  options.add_options()("threads,t", po::value<int>()->required(),
                        "# thread to use");
  options.add_options()("duration,d", po::value<int>()->required(),
                        "Test duration in seconds");

  try {
    po::variables_map vm;

    po::store(po::command_line_parser(argc, argv).options(options).run(), vm);
    po::notify(vm);

    Args args;

    args.mtype = vm["mutex"].as<MutexType>();
    args.num_seconds = vm["duration"].as<int>();
    args.num_threads = vm["threads"].as<int>();

    start_test(args);
  } catch (const po::error &ex) {
    std::cerr << "ERROR: " << ex.what() << std::endl;
    std::cerr << options << std::endl;
  } catch (...) {
    std::cerr << options << std::endl;
  }
}

std::istream &operator>>(std::istream &in, MutexType &mtype) {
  std::string token;

  in >> token;

  if (token == "std")
    mtype = MutexType::STD_MUTEX;
  else if (token == "mutex")
    mtype = MutexType::MUTEX;
  else if (token == "fair")
    mtype = MutexType::FAIR_MUTEX;
  else
    in.setstate(std::ios_base::failbit);

  return in;
}

static std::int64_t calculate_mad(const std::vector<std::int64_t> &vals) {
  std::vector<std::int64_t> less_median{vals.begin(), vals.end()};

  std::sort(less_median.begin(), less_median.end());
  auto median = less_median[less_median.size() / 2];

  for (auto &val : less_median) {
    val = std::abs(val - median);
  }

  return calculate_mean(less_median);
}

static std::int64_t calculate_mean(const std::vector<std::int64_t> &vals) {
  return std::accumulate(vals.begin(), vals.end(), 0) / vals.size();
}

#if 0
static std::int64_t calculate_stddev(const std::vector<std::int64_t> &vals) {
  using namespace boost::accumulators;

  accumulator_set<std::int64_t, stats<tag::variance>> acc;
  std::for_each(vals.begin(), vals.end(), std::ref(acc));

  return std::sqrt(variance(acc));
}
#endif
