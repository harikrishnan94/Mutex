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
#include <ostream>
#include <string>
#include <thread>
#include <vector>

#include <boost/program_options.hpp>

struct BMArgs
{
	int num_seconds;
	int num_threads;
	uint64_t crit_section_duration;
	uint64_t local_section_duration;

	bool pthread;
	bool parkinglot;

	friend std::ostream &
	operator<<(std::ostream &out, BMArgs args)
	{
		out << "Threads = " << args.num_threads
		    << ", Critical Section Duration = " << args.crit_section_duration << " ns"
		    << ", Local Section Duration = " << args.local_section_duration << " ns"
		    << ", Benchmark Duration = " << args.num_seconds << " s";

		return out;
	}
};

static void report_bench(const std::string &str, uint64_t ops);
static BMArgs parse_args(int argc, const char *argv[]);

template <typename MutexType>
void
bench_worker(MutexType &m,
             std::atomic<bool> &quit,
             volatile uint64_t &shared_data,
             uint64_t critical_section_duration,
             uint64_t local_section_duration,
             std::atomic<uint64_t> &total_operations)
{
	parking_lot::ThreadLocal::RegisterThread();

	volatile uint64_t local_section_data = 0;
	uint64_t num_operations              = 0;

	auto delay_ns = [](uint64_t ns, volatile uint64_t &data) {
		if (ns)
		{
			auto end = std::chrono::high_resolution_clock::now() + std::chrono::nanoseconds{ ns };

			while (std::chrono::high_resolution_clock::now() < end)
			{
				++data;
			}
		}
	};

	while (!quit)
	{
		// Here we model both local work outside of the critical section as well as
		// some work inside of the critical section. The idea is to capture some
		// more or less realisitic contention levels.
		// If contention is too low, the benchmark won't measure anything useful.
		// If contention is unrealistically high, the benchmark will favor
		// bad mutex implementations that block and otherwise distract threads
		// from the mutex and shared state for as much as possible.
		// To achieve this, amount of local work is multiplied by number of threads
		// to keep ratio between local work and critical section approximately
		// equal regardless of number of threads.

		{
			std::lock_guard<MutexType> lock{ m };

			delay_ns(critical_section_duration, shared_data);
		}

		delay_ns(local_section_duration, local_section_data);
		num_operations++;
	}

	total_operations += num_operations;

	parking_lot::ThreadLocal::UnregisterThread();
}

template <typename MutexType>
uint64_t
bench_mutex(BMArgs args)
{
	MutexType m;
	std::atomic<bool> quit        = {};
	volatile uint64_t shared_data = 0;
	std::vector<std::thread> workers;
	std::atomic<uint64_t> total_operations = 0;

	std::chrono::seconds bench_time{ args.num_seconds };

	for (int i = 0; i < args.num_threads; i++)
	{
		workers.emplace_back(bench_worker<MutexType>,
		                     std::ref(m),
		                     std::ref(quit),
		                     std::ref(shared_data),
		                     args.crit_section_duration,
		                     args.local_section_duration * args.num_threads,
		                     std::ref(total_operations));
	}

	std::this_thread::sleep_for(bench_time);
	quit.store(true);

	for (auto &worker : workers)
	{
		worker.join();
	}

	return total_operations;
}

static void
do_bench(BMArgs args)
{
	std::cout << "Benchmark -> " << args << std::endl;

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
	    "critsection",
	    value<uint64_t>()->default_value(1)->required(),
	    "Amount of time spent INSIDE "
	    "critical section (ns)")("localsection",
	                             value<uint64_t>()->default_value(0)->required(),
	                             "Amount of time spent OUTSIDE "
	                             "critical section (ns)")(
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

			args.num_threads            = vm["numthreads"].as<int>();
			args.num_seconds            = vm["exectime"].as<int>();
			args.crit_section_duration  = vm["critsection"].as<uint64_t>();
			args.local_section_duration = vm["localsection"].as<uint64_t>();
			args.pthread                = vm["pthread"].as<bool>();
			args.parkinglot             = vm["parkinglot"].as<bool>();

			if (!args.parkinglot && !args.pthread)
			{
				throw std::string{
					"ERROR: Must include benchmark for one of Pthread / Parkinglot Mutex"
				};
			}

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
