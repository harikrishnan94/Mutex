#include "Mutex.h"
#include "ThreadLocal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <inttypes.h>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/program_options.hpp>

struct BMArgs
{
	int num_seconds;
	int num_threads;
	int crit_section_len;
	int local_section_len;

	bool pthread;
	bool parkinglot;
};

static void report_bench(const std::string &str, uint64_t ops);
static BMArgs parse_args(int argc, const char *argv[]);

template <typename MutexType>
void
bench_worker(MutexType &m,
             std::atomic<bool> &quit,
             std::vector<uint64_t> &crit_section_data,
             int local_section_len,
             std::atomic<uint64_t> &result)
{
	parking_lot::ThreadLocal::RegisterThread();

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
		if (crit_section_data.size())
		{
			std::lock_guard<MutexType> lock{ m };

			do_increment(crit_section_data);
		}

		do_increment(local_section_data);
		num_increments++;
	}

	result += num_increments;

	parking_lot::ThreadLocal::UnregisterThread();
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

	// Detect error (Must not happen)
	for (auto count : crit_section_data)
	{
		if (count != num_increments)
		{
			num_increments = 0;
			break;
		}
	}

	return num_increments;
}

static void
do_bench(BMArgs args)
{
	std::cout << "Benchmarking with " << args.num_threads << " threads for " << args.num_seconds
	          << " s ...\n";

	if (args.parkinglot)
	{
		uint64_t parkinglot_mutex_ops = bench_mutex<parking_lot::mutex::Mutex>(args);
		report_bench("Parking lot mutex = ", parkinglot_mutex_ops / args.num_seconds);
	}

	if (args.pthread)
	{
		uint64_t pthread_mutex_ops = bench_mutex<std::mutex>(args);
		report_bench("Pthread mutex = ", pthread_mutex_ops / args.num_seconds);
	}
}

int
main(int argc, const char *argv[])
{
	do_bench(parse_args(argc, argv));
}

static void
report_bench(const std::string &str, uint64_t ops)
{
	auto human_readable_num = [](uint64_t number) {
		int i                     = 0;
		const std::string units[] = { "", "K", "M", "G", "T", "P", "E", "Z", "Y" };
		uint64_t rem              = number;

		while (number > 1000)
		{
			rem = number % 1000;
			number /= 1000;
			i++;
		}

		return std::to_string(number) + "." + std::to_string(rem) + " " + units[i];
	};

	if (ops)
		std::cout << std::setw(25) << str << human_readable_num(ops) << "ops\n";
	else
		std::cout << std::setw(25) << str << "ERROR\n";
}

static BMArgs
parse_args(int argc, const char *argv[])
{
	using namespace boost::program_options;

	options_description desc{ "Benchmark Options" };
	desc.add_options()("help,h",
	                   "Help screen")("numthreads",
	                                  value<int>()
	                                      ->default_value(
	                                          std::max(std::thread::hardware_concurrency(), 1U))
	                                      ->required(),
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
	    "critical section (Unit of work is incrementing a counter)")(
	    "pthread",
	    value<bool>()->default_value(true)->required(),
	    "Benchmark Pthread")("parkinglot",
	                         value<bool>()->default_value(true)->required(),
	                         "Benchmark Parkinglot Mutex");

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
			args.pthread           = vm["pthread"].as<bool>();
			args.parkinglot        = vm["parkinglot"].as<bool>();

			if (!args.parkinglot && !args.pthread)
				throw std::string{
					"ERROR: Must include benchmark for one of Pthread / Parkinglot Mutex"
				};

			return args;
		}
		catch (const std::string &ex)
		{
			std::cerr << ex << '\n';
			std::cout << desc << '\n';
			std::exit(-1);
		}
		catch (error &e)
		{
			std::cerr << e.what() << '\n';
			std::cout << desc << '\n';
			std::exit(-1);
		}
		catch (const std::exception &ex)
		{
			std::cerr << ex.what() << '\n';
			std::cout << desc << '\n';
			std::exit(-1);
		}
	}
}
