#include <omp.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

long long argument(const char* text) {
    std::size_t used = 0;
    long long x = std::stoll(text, &used);
    if (used != std::string(text).size()) throw std::runtime_error("Invalid integer argument.");
    return x;
}
int kernel(std::vector<double>& a, const std::vector<double>& b, const std::vector<double>& c, int threads) {
    if (threads == 0) {
        for (std::size_t i = 0; i < a.size(); ++i) a[i] = b[i] + 0.5 * c[i];
        return 1;
    }
    int actual = 0;
    long long n = static_cast<long long>(a.size());
    #pragma omp parallel num_threads(threads) shared(actual)
    {
        #pragma omp single
        { actual = omp_get_num_threads(); }
        #pragma omp for schedule(static)
        for (long long i = 0; i < n; ++i) a[i] = b[i] + 0.5 * c[i];
    }
    return actual;
}
double quantile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    double p = fraction * (values.size() - 1);
    std::size_t i = static_cast<std::size_t>(p), j = std::min(i + 1, values.size() - 1);
    return values[i] + (values[j] - values[i]) * (p - i);
}
struct Trial { int trial, actual; double seconds, gbps; };
struct Config { int threads; std::vector<Trial> trials; };
std::string label(int threads) { return threads == 0 ? "sequential" : "openmp_" + std::to_string(threads); }
int main(int argc, char** argv) {
    try {
        if (argc > 4) throw std::runtime_error("Usage: bandwidth [elements] [trials] [passes]");
        long long n = argc > 1 ? argument(argv[1]) : 4194304;
        long long trials = argc > 2 ? argument(argv[2]) : 7;
        long long passes = argc > 3 ? argument(argv[3]) : 10;
        if (n < 1024 || n > 67108864 || trials < 3 || trials > 30 || passes < 1 || passes > 100)
            throw std::runtime_error("Use 1024..67108864 elements, 3..30 trials and 1..100 passes.");
        omp_set_dynamic(0); omp_set_max_active_levels(1);
        std::vector<double> a(static_cast<std::size_t>(n)), b(static_cast<std::size_t>(n),2), c(static_cast<std::size_t>(n));
        std::vector<Config> configs;
        configs.push_back({0,{}});
        for (int threads : {1,2,4,8,12,16}) if (threads <= omp_get_num_procs()) configs.push_back({threads,{}});
        std::vector<std::size_t> order(configs.size()); std::iota(order.begin(),order.end(),0);
        std::mt19937 rng(20261019U);
        const double useful_bytes = static_cast<double>(n) * 3.0 * sizeof(double) * passes;
        std::cout << "Separate OpenMP array-triad benchmark\nArray footprint: " << 3.0*n*sizeof(double)/(1024*1024)
                  << " MiB\nUseful bytes per element: " << 3*sizeof(double)
                  << "\nThis is effective array bandwidth, not measured application DRAM traffic.\n";
        for (int t = 1; t <= trials; ++t) {
            std::shuffle(order.begin(),order.end(),rng);
            for (auto i : order) {
                auto& config = configs[i];
                std::fill(a.begin(),a.end(),0.0); std::fill(c.begin(),c.end(),3.0);
                int actual = kernel(a,b,c,config.threads); std::swap(a,c);
                auto start = std::chrono::steady_clock::now();
                for (int pass = 0; pass < passes; ++pass) {
                    actual = kernel(a,b,c,config.threads); std::swap(a,c);
                }
                double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                double expected = 3.0;
                for (int pass = 0; pass <= passes; ++pass) expected = 2.0 + 0.5 * expected;
                for (double value : c) if (std::abs(value-expected) > 1e-10) throw std::runtime_error("Array correctness check failed.");
                config.trials.push_back({t,actual,elapsed,useful_bytes/elapsed/1e9});
            }
            std::cout << "Completed bandwidth trial " << t << '/' << trials << std::endl;
        }
        std::filesystem::create_directories("results");
        std::ofstream raw("results/array_bandwidth_raw.csv"), summary("results/array_bandwidth_summary.csv"), info("results/array_bandwidth_info.txt");
        if (!raw || !summary || !info) throw std::runtime_error("Cannot create bandwidth output files.");
        raw << "configuration,actual_threads,trial,elements,passes,useful_bytes,batch_seconds,effective_GB_per_second\n" << std::setprecision(12);
        summary << "configuration,actual_threads,elements,array_footprint_MiB,trials,passes,median_effective_GB_per_second,q1_GB_per_second,q3_GB_per_second\n" << std::setprecision(12);
        for (const auto& config : configs) {
            std::vector<double> rates;
            for (const auto& t : config.trials) {
                raw << label(config.threads) << ',' << t.actual << ',' << t.trial << ',' << n << ',' << passes << ','
                    << useful_bytes << ',' << t.seconds << ',' << t.gbps << '\n';
                rates.push_back(t.gbps);
            }
            summary << label(config.threads) << ',' << config.trials[0].actual << ',' << n << ',' << 3.0*n*sizeof(double)/(1024*1024)
                    << ',' << trials << ',' << passes << ',' << quantile(rates,.5) << ',' << quantile(rates,.25) << ',' << quantile(rates,.75) << '\n';
            std::cout << label(config.threads) << ": " << quantile(rates,.5) << " effective GB/s\n";
        }
        const char* policy = std::getenv("OMP_WAIT_POLICY");
        info << "version=array-triad-v1\ncompiler=" << __VERSION__ << "\nopenmp_macro=" << _OPENMP
             << "\nelements=" << n << "\ntrials=" << trials << "\npasses=" << passes
             << "\nlogical_processors_exposed=" << omp_get_num_procs() << "\nwait_policy=" << (policy ? policy : "runtime_default")
             << "\nmetric=effective_array_bandwidth\nbyte_model=two_double_reads_plus_one_double_write_per_element_per_pass\n"
             << "units=decimal_GB_per_second\nalpha=0.5\narray_roles=swap_output_and_c_after_each_pass\n"
             << "scope=separate_memory_kernel_not_smart_grid_application\nactual_DRAM_traffic=not_measured\n"
             << "timing=kernel_team_management_and_vector_swap; excludes_initialization_warmup_validation_output\n"
             << "limitations=caches_write_allocation_and_runtime_overhead_are_not_separately_counted\n"
             << "not_an_official_STREAM_result=true\n";
        raw.close();summary.close();info.close();
        if (!raw || !summary || !info) throw std::runtime_error("Bandwidth output write failed.");
        std::cout << "All array checks passed. Saved three bandwidth files.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << '\n'; return 1; }
}
