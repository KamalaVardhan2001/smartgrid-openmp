#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include "grid_core.hpp"
#include <omp.h>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>

using Clock = std::chrono::steady_clock;
volatile double observable = 0;

double cpu_seconds() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        throw std::runtime_error("Cannot read process CPU time.");
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return (static_cast<double>(k.QuadPart) + static_cast<double>(u.QuadPart)) / 10000000.0;
#else
    auto value = std::clock();
    if (value == static_cast<std::clock_t>(-1)) throw std::runtime_error("Cannot read process CPU time.");
    return static_cast<double>(value) / CLOCKS_PER_SEC;
#endif
}

int execute(const grid::Dataset& data, std::vector<grid::Result>& output, int threads) {
    if (threads == 0) {
        for (std::size_t m = 0; m < data.meters.size(); ++m) output[m] = grid::process_meter(data.meters[m]);
        return 1;
    }
    std::exception_ptr failure;
    int actual = 0;
    long long size = static_cast<long long>(data.meters.size());
    #pragma omp parallel num_threads(threads) shared(failure, actual)
    {
        #pragma omp single
        actual = omp_get_num_threads();
        #pragma omp for schedule(static)
        for (long long m = 0; m < size; ++m) {
            try { output[m] = grid::process_meter(data.meters[m]); }
            catch (...) {
                #pragma omp critical
                { if (!failure) failure = std::current_exception(); }
            }
        }
    }
    if (failure) std::rethrow_exception(failure);
    return actual;
}

double check(const std::vector<grid::Result>& expected, const std::vector<grid::Result>& actual) {
    double max_difference = 0;
    auto compare = [&](double a, double b) {
        double difference = std::abs(a - b);
        if (!std::isfinite(a) || !std::isfinite(b) || difference > 1e-8 + 1e-10 * std::abs(a))
            throw std::runtime_error("Sequential/OpenMP numerical mismatch.");
        max_difference = std::max(max_difference, difference);
    };
    auto error = [&](const grid::Error& a, const grid::Error& b) {
        if (a.count != b.count) throw std::runtime_error("Forecast count mismatch.");
        compare(a.absolute, b.absolute); compare(a.squared, b.squared);
    };
    if (expected.size() != actual.size()) throw std::runtime_error("Meter count mismatch.");
    for (std::size_t m = 0; m < expected.size(); ++m) {
        const auto& a = expected[m]; const auto& b = actual[m];
        if (a.id != b.id || a.samples != b.samples || a.train_end != b.train_end ||
            a.validation_end != b.validation_end || a.overload != b.overload ||
            a.voltage_alerts != b.voltage_alerts || a.forecasts.size() != b.forecasts.size())
            throw std::runtime_error("Meter statistics or identity mismatch.");
        compare(a.mean_power,b.mean_power); compare(a.peak_power,b.peak_power);
        compare(a.energy,b.energy); compare(a.next_forecast,b.next_forecast);
        error(a.validation,b.validation); error(a.test,b.test);
        error(a.persistence_validation,b.persistence_validation); error(a.persistence_test,b.persistence_test);
        for (std::size_t i = 0; i < a.forecasts.size(); ++i) {
            const auto& x = a.forecasts[i]; const auto& y = b.forecasts[i];
            if (x.index != y.index) throw std::runtime_error("Forecast target mismatch.");
            compare(x.actual,y.actual); compare(x.predicted,y.predicted); compare(x.persistence,y.persistence);
        }
    }
    return max_difference;
}

struct Trial { int trial, actual_threads; double batch_wall, cpu, per_pass, throughput, cpu_percent; };
struct Config { int threads, actual = 0; double max_difference = 0; std::vector<Trial> trials; };
double quantile(std::vector<double> values, double fraction) {
    std::sort(values.begin(),values.end());
    double position = fraction * (values.size() - 1);
    std::size_t left = static_cast<std::size_t>(position);
    std::size_t right = std::min(left + 1, values.size() - 1);
    return values[left] + (values[right] - values[left]) * (position - left);
}
std::string label(int threads) { return threads == 0 ? "sequential" : "openmp_" + std::to_string(threads); }

int main(int argc, char** argv) {
    try {
        if (argc > 4) throw std::runtime_error("Usage: benchmark [dataset.csv] [trials 3..30] [passes 0..1000; 0=auto]");
        std::string path = argc > 1 ? argv[1] : "data/smart_meter_data.csv";
        long long requested_trials = argc > 2 ? grid::integer(argv[2]) : 10;
        long long requested_passes = argc > 3 ? grid::integer(argv[3]) : 0;
        if (requested_trials < 3 || requested_trials > 30 || requested_passes < 0 || requested_passes > 1000)
            throw std::runtime_error("Use 3..30 trials and 0..1000 passes (0 selects automatically).");
        int trials = static_cast<int>(requested_trials);
        omp_set_dynamic(0); omp_set_max_active_levels(1);
        int processors = omp_get_num_procs();
        const char* wait_policy = std::getenv("OMP_WAIT_POLICY");
        if (!wait_policy || std::string(wait_policy) != "PASSIVE")
            std::cout << "NOTE: Set OMP_WAIT_POLICY=PASSIVE before launch to limit idle worker spinning between configurations.\n";
        std::cout << "Smart Grid Sequential vs OpenMP Benchmark\nLoading data once...\n";
        auto data = grid::load(path);
        std::cout << "Meters: " << data.meters.size() << " | Readings: " << data.records
                  << " | Logical processors exposed: " << processors << '\n';
        std::vector<grid::Result> reference(data.meters.size()), output(data.meters.size());
        auto warm_start = Clock::now();
        execute(data,reference,0);
        double warm_time = std::chrono::duration<double>(Clock::now() - warm_start).count();
        int passes = requested_passes ? static_cast<int>(requested_passes) :
            static_cast<int>(std::clamp(std::ceil(0.25 / std::max(warm_time,1e-6)),1.0,256.0));
        std::size_t forecasts = 0;
        for (const auto& r : reference) forecasts += r.forecasts.size();
        std::vector<Config> configs;
        Config sequential{}; sequential.threads = 0; configs.push_back(sequential);
        for (int threads : {1,2,4,8,12,16}) if (threads <= processors) {
            Config config{}; config.threads = threads; configs.push_back(config);
        }
        std::cout << "Trials per configuration: " << trials << " | Workload passes per trial: " << passes
                  << "\nA pass refits and evaluates all meters on the same fixed dataset.\n"
                  << "Checking outputs and warming every configuration...\n";
        for (auto& config : configs) {
            config.actual = execute(data,output,config.threads);
            config.max_difference = check(reference,output);
            execute(data,output,config.threads);
            std::cout << label(config.threads) << ": PASS (actual threads " << config.actual << ")\n";
        }
        std::vector<std::size_t> order(configs.size());
        std::iota(order.begin(),order.end(),0);
        std::mt19937 order_rng(20261019U);
        for (int t = 1; t <= trials; ++t) {
            std::shuffle(order.begin(),order.end(),order_rng);
            for (std::size_t selected : order) {
                auto& config = configs[selected];
                double cpu_start = cpu_seconds();
                auto start = Clock::now();
                int actual_threads = 1;
                for (int pass = 0; pass < passes; ++pass) {
                    actual_threads = execute(data,output,config.threads);
                    observable = output.back().next_forecast;
                }
                double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
                double cpu = cpu_seconds() - cpu_start;
                if (actual_threads != config.actual) throw std::runtime_error("OpenMP team size changed between runs.");
                config.trials.push_back({t,actual_threads,elapsed,cpu,elapsed/passes,
                    static_cast<double>(forecasts)*passes/elapsed,100.0*cpu/(elapsed*processors)});
                config.max_difference = std::max(config.max_difference,check(reference,output));
            }
            std::cout << "Completed trial " << t << '/' << trials << std::endl;
        }
        std::filesystem::create_directories("results");
        std::ofstream raw("results/benchmark_raw.csv"), summary("results/benchmark_summary.csv"),
            correctness("results/benchmark_correctness.csv"), info("results/benchmark_info.txt");
        if (!raw || !summary || !correctness || !info) throw std::runtime_error("Cannot create benchmark files. Close open result files in Excel.");
        raw << "configuration,requested_threads,actual_threads,trial,passes,forecasts_per_pass,batch_wall_seconds,process_cpu_seconds,seconds_per_pass,forecasts_per_second,cpu_percent_of_exposed_logical_capacity\n" << std::setprecision(12);
        summary << "configuration,requested_threads,actual_threads,trials,passes_per_trial,median_seconds_per_pass,q1_seconds,q3_seconds,speedup_vs_sequential,efficiency_fraction,median_forecasts_per_second,median_cpu_percent_of_exposed_logical_capacity\n" << std::setprecision(12);
        correctness << "configuration,status,max_absolute_numeric_difference,absolute_tolerance,relative_tolerance\n" << std::setprecision(12);
        std::vector<double> baseline_times;
        for (const auto& t : configs[0].trials) baseline_times.push_back(t.per_pass);
        double baseline = quantile(baseline_times,0.5);
        std::cout << "\nRESULTS (median compute time per complete workload pass)\n"
                  << std::left << std::setw(14) << "Configuration" << std::right << std::setw(13) << "Seconds"
                  << std::setw(12) << "Speedup" << std::setw(14) << "Efficiency" << '\n';
        for (const auto& config : configs) {
            std::vector<double> times,throughputs,cpu;
            for (const auto& t : config.trials) {
                times.push_back(t.per_pass); throughputs.push_back(t.throughput); cpu.push_back(t.cpu_percent);
                raw << label(config.threads) << ',' << (config.threads ? config.threads : 1) << ',' << t.actual_threads << ','
                    << t.trial << ',' << passes << ',' << forecasts << ',' << t.batch_wall << ',' << t.cpu << ','
                    << t.per_pass << ',' << t.throughput << ',' << t.cpu_percent << '\n';
            }
            double median = quantile(times,0.5), speedup = baseline/median, efficiency = speedup/config.actual;
            summary << label(config.threads) << ',' << (config.threads ? config.threads : 1) << ',' << config.actual << ','
                    << trials << ',' << passes << ',' << median << ',' << quantile(times,0.25) << ',' << quantile(times,0.75) << ','
                    << speedup << ',' << efficiency << ',' << quantile(throughputs,0.5) << ',' << quantile(cpu,0.5) << '\n';
            correctness << label(config.threads) << ",PASS," << config.max_difference << ",1e-8,1e-10\n";
            std::cout << std::left << std::setw(14) << label(config.threads) << std::right << std::fixed << std::setprecision(6)
                      << std::setw(13) << median << std::setprecision(3) << std::setw(12) << speedup
                      << std::setw(13) << efficiency*100 << "%\n";
        }
        info << "version=stage2-v1\ncompiler=" << __VERSION__ << "\nopenmp_macro=" << _OPENMP
             << "\ndataset_path=" << path << "\ndataset_fnv1a64_normalized_rows=" << std::hex << data.hash << std::dec
             << "\nmeters=" << data.meters.size() << "\nreadings=" << data.records << "\nforecasts_per_pass=" << forecasts
             << "\ntrials=" << trials << "\npasses_per_trial=" << passes
             << "\nlogical_processors_exposed=" << processors
             << "\nconfiguration_order_seed=20261019\nopenmp_schedule=static\nopenmp_dynamic=disabled\n"
             << "openmp_wait_policy=" << (wait_policy ? wait_policy : "runtime_default") << '\n'
             << "timing=warm_fit_evaluate_statistics_and_result_assignment; excludes_load_output_correctness\n"
             << "cpu_normalization=process_cpu_seconds / (batch_wall_seconds * exposed_logical_processors) * 100\n"
             << "cpu_scope=whole_process_all_threads_including_OpenMP_worker_runtime\n"
             << "memory_bandwidth=not_measured\nmemory_footprint=not_measured\n"
             << "passes=identical_fit_evaluate_workload_replays_for_stable_timing_not_new_meter_data\n"
             << "warning=single_machine_synthetic_workload_not_field_validation\n";
        raw.close();summary.close();correctness.close();info.close();
        if (!raw || !summary || !correctness || !info) throw std::runtime_error("Failed while writing benchmark results.");
        std::cout << "\nAll correctness checks passed. Saved four benchmark files in results/.\n"
                  << "Speedup below 1 means slower than sequential; retain every measured configuration.\n"
                  << "CPU percent is normalized to all exposed logical processors, not only the requested threads.\n"
                  << "CSV load/export are excluded. Memory footprint and DRAM bandwidth are not measured yet.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
