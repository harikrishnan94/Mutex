#include "Mutex.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <inttypes.h>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include <boost/program_options.hpp>

struct BMArgs
{
	int num_seconds;
	int num_threads;
	int crit_section_len;
	int local_section_len;
};

static BMArgs parse_args(int argc, const char *argv[]);

template <typename MutexType>
void
bench_worker(MutexType &m,
             std::atomic<bool> &quit,
             std::vector<uint64_t> &crit_section_data,
             int local_section_len,
             std::atomic<uint64_t> &result)
{
	std::vector<uint64_t> local_section_data(local_section_len);
	uint64_t num_increments = 0;

	auto do_increment = [](std::vector<uint64_t> &vec) {
		for (auto &val : vec)
		{
			val++;
		}
	};

	while (!quit)
	{
		{
			std::lock_guard<MutexType> lock{ m };

			do_increment(crit_section_data);
		}

		do_increment(local_section_data);
		num_increments++;
	}

	result += num_increments;
}

template <typename MutexType>
uint64_t
bench_mutex(BMArgs args)
{
	MutexType m;
	std::atomic<bool> quit = {};
	std::vector<uint64_t> crit_section_data(args.crit_section_len);
	std::vector<std::thread> workers;
	std::atomic<uint64_t> num_increments = 0;

	std::chrono::seconds wait_time{ args.num_seconds };

	for (int i = 0; i < args.num_threads; i++)
	{
		workers.emplace_back(bench_worker<MutexType>,
		                     std::ref(m),
		                     std::ref(quit),
		                     std::ref(crit_section_data),
		                     args.local_section_len,
		                     std::ref(num_increments));
	}

	std::this_thread::sleep_for(wait_time);
	quit.store(true);

	for (auto &worker : workers)
	{
		worker.join();
	}

	return num_increments;
}

void
do_bench(BMArgs args)
{
	uint64_t parkinglot_mutex_ops = bench_mutex<parking_lot::Mutex>(args);
	std::cout << "Parking lot mutex = " << parkinglot_mutex_ops << " ops\n";

	uint64_t pthread_mutex_ops = bench_mutex<std::mutex>(args);
	std::cout << "Pthread lot mutex = " << pthread_mutex_ops << " ops\n";
}

int
main(int argc, const char *argv[])
{
	do_bench(parse_args(argc, argv));
}

static BMArgs
parse_args(int argc, const char *argv[])
{
	using namespace boost::program_options;

	options_description desc{ "Benchmark Options" };
	desc.add_options()("help,h",
	                   "Help screen")("numthreads",
	                                  value<int>()->default_value(4)->required(),
	                                  "# Threads")("exectime",
	                                               value<int>()->default_value(1)->required(),
	                                               "Execution time of benchmark in seconds")(
	    "critsectionlen",
	    value<int>()->default_value(1)->required(),
	    "Units of work to be done INSIDE "
	    "critical section (Unit of work is incrementing a counter)")(
	    "localsectionlen",
	    value<int>()->default_value(0)->required(),
	    "Units of work to be done OUTSIDE "
	    "critical section (Unit of work is incrementing a counter)");

	variables_map vm;
	store(parse_command_line(argc, argv, desc), vm);

	if (vm.count("help"))
	{
		std::cout << desc << '\n';
		std::exit(0);
	}
	else
	{
		try
		{
			BMArgs args;

			notify(vm);

			args.num_threads       = vm["numthreads"].as<int>();
			args.num_seconds       = vm["exectime"].as<int>();
			args.crit_section_len  = vm["critsectionlen"].as<int>();
			args.local_section_len = vm["localsectionlen"].as<int>();

			return args;
		}
		catch (const std::exception &ex)
		{
			std::cerr << ex.what() << '\n';
			std::cout << desc << '\n';
			std::exit(-1);
		}
		catch (error &e)
		{
			std::cerr << e.what() << '\n';
			std::cout << desc << '\n';
			std::exit(-1);
		}
	}
}
