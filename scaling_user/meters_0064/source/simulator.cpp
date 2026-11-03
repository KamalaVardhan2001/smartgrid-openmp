#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

int integer(const char* text) {
    std::size_t used = 0;
    long long value = std::stoll(text, &used);
    if (used != std::string(text).size() || value < 0 || value > 1000000)
        throw std::runtime_error("Arguments must be nonnegative integers within the supported range.");
    return static_cast<int>(value);
}

int main(int argc, char** argv) {
    try {
        if (argc > 5) throw std::runtime_error("Usage: simulator [meters] [days] [seed] [--force]");
        int meters = argc > 1 ? integer(argv[1]) : 64;
        int days = argc > 2 ? integer(argv[2]) : 7;
        int seed = argc > 3 ? integer(argv[3]) : 42;
        bool force = argc == 5 && std::string(argv[4]) == "--force";
        if (argc == 5 && !force) throw std::runtime_error("The final option must be --force.");
        long long records = 1LL * meters * days * 288;
        if (meters < 1 || days < 1 || days > 365 || records > 10000000)
            throw std::runtime_error("Use at least one meter/day, at most 365 days and at most 10 million records.");
        std::filesystem::create_directories("data");
        const std::string path = "data/smart_meter_data.csv";
        if (std::filesystem::exists(path) && !force)
            throw std::runtime_error("Dataset already exists. Keep it for repeatable comparisons, or pass --force to replace it.");
        std::ofstream out(path);
        if (!out) throw std::runtime_error("Cannot create dataset in the current folder.");
        out << "meter_id,sample_index,elapsed_seconds,voltage_V,current_A,power_W,energy_kWh,frequency_Hz,power_factor,capacity_W\n";
        out << std::fixed << std::setprecision(8);
        std::cout << "Generating SYNTHETIC smart-meter data\nMeters: " << meters
                  << "\nSimulated days per meter: " << days
                  << "\nSampling interval: 300 seconds (5 minutes)\nSeed: " << seed
                  << "\nExpected records: " << records << "\n";
        const double pi = std::acos(-1.0);
        for (int m = 1; m <= meters; ++m) {
            std::mt19937 rng(static_cast<std::uint32_t>(seed) + static_cast<std::uint32_t>(m) * 7919U);
            std::uniform_real_distribution<double> uniform(0.0, 1.0);
            std::normal_distribution<double> normal(0.0, 1.0);
            double base = 200.0 + 600.0 * uniform(rng);
            double scale = 0.6 + 1.5 * uniform(rng);
            double capacity = 3000.0 + 500.0 * (m % 4);
            double shift = (uniform(rng) - 0.5) * 1.2;
            double energy = 0.0, noise = 0.0;
            for (int t = 0; t < days * 288; ++t) {
                double hour = (t % 288) / 12.0;
                int day = t / 288;
                double morning = std::exp(-0.5 * std::pow((hour - 7.5 - shift) / 1.4, 2));
                double evening = std::exp(-0.5 * std::pow((hour - 19.5 - shift) / 2.0, 2));
                double weekend = day % 7 >= 5 ? 1.12 : 1.0;
                noise = 0.75 * noise + 35.0 * normal(rng);
                double appliance = uniform(rng) < 0.02 ? 400.0 + 500.0 * uniform(rng) : 0.0;
                double power = std::max(25.0, weekend * (base + scale * (500.0 * morning + 900.0 * evening)) + noise + appliance);
                if (m % 5 == 0 && t % 288 >= 168 && t % 288 < 174)
                    power += capacity;
                double voltage = 230.0 + 2.0 * std::sin(2.0 * pi * hour / 24.0) + 0.8 * normal(rng);
                if (m % 7 == 0 && t % 288 >= 120 && t % 288 < 123) voltage -= 30.0;
                double pf = 0.88 + 0.11 * uniform(rng);
                double current = power / (voltage * pf);
                double frequency = 50.0 + 0.02 * normal(rng);
                energy += power * 300.0 / 3600000.0;
                out << m << ',' << t << ',' << 1LL * t * 300 << ',' << voltage << ','
                    << current << ',' << power << ',' << energy << ',' << frequency << ',' << pf << ',' << capacity << '\n';
            }
            if (m == meters || m % 16 == 0) std::cout << "Completed " << m << "/" << meters << " meters\n";
        }
        out.close();
        if (!out) throw std::runtime_error("Dataset write failed. Check disk space.");
        std::ofstream meta("data/dataset_info.txt");
        meta << "source=synthetic\nversion=stage1-v1\nmeters=" << meters << "\ndays=" << days
             << "\nseed=" << seed << "\ninterval_seconds=300\nrecords=" << records
             << "\nrow_order=meter_then_sample\nenergy_convention=constant_power_over_interval_beginning_at_sample\n";
        if (!meta) throw std::runtime_error("Cannot write dataset metadata.");
        std::cout << "Saved: " << path << "\nSaved: data/dataset_info.txt\n"
                  << "Simulation runs faster than wall-clock time; no hardware or cloud is used.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
