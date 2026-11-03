#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace grid {
constexpr int LAGS = 12;
constexpr int DIM = LAGS + 1;
constexpr int INTERVAL = 300;
constexpr double RIDGE = 0.001;
struct Reading {
    int index;
    long long seconds;
    double voltage, current, power, energy, frequency, pf, capacity;
};
struct Meter { int id; std::vector<Reading> rows; };
struct Dataset { std::vector<Meter> meters; std::uint64_t hash = 14695981039346656037ULL; std::size_t records = 0; };

inline double number(const std::string& s) {
    std::size_t used = 0;
    double x = std::stod(s, &used);
    if (used != s.size() || !std::isfinite(x)) throw std::runtime_error("Invalid numeric field.");
    return x;
}
inline long long integer(const std::string& s) {
    std::size_t used = 0;
    long long x = std::stoll(s, &used);
    if (used != s.size()) throw std::runtime_error("Invalid integer field.");
    return x;
}
inline Dataset load(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open " + path + ". Run the simulator first from the project folder.");
    Dataset data;
    std::string line;
    if (!std::getline(input, line)) throw std::runtime_error("Dataset is empty.");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "meter_id,sample_index,elapsed_seconds,voltage_V,current_A,power_W,energy_kWh,frequency_Hz,power_factor,capacity_W")
        throw std::runtime_error("Unexpected CSV header. Use the supplied simulator schema.");
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        try {
            if (line.empty()) throw std::runtime_error("Empty data row.");
            for (unsigned char ch : line) { data.hash ^= ch; data.hash *= 1099511628211ULL; }
            data.hash ^= '\n'; data.hash *= 1099511628211ULL;
            std::vector<std::string> fields;
            std::stringstream stream(line);
            std::string part;
            while (std::getline(stream, part, ',')) fields.push_back(part);
            if (fields.size() != 10 || line.back() == ',') throw std::runtime_error("Expected ten CSV fields.");
            long long id = integer(fields[0]), idx = integer(fields[1]);
            if (id < 1 || id > 1000000 || idx < 0 || idx > 10000000) throw std::runtime_error("Meter ID or sample index out of range.");
            Reading r{static_cast<int>(idx), integer(fields[2]), number(fields[3]), number(fields[4]), number(fields[5]),
                      number(fields[6]), number(fields[7]), number(fields[8]), number(fields[9])};
            if (r.voltage <= 0 || r.current < 0 || r.power < 0 || r.energy < 0 || r.frequency <= 0 || r.pf <= 0 || r.pf > 1 || r.capacity <= 0)
                throw std::runtime_error("Nonphysical reading or capacity.");
            if (r.seconds != idx * INTERVAL) throw std::runtime_error("Expected a regular 300-second series starting at zero.");
            if (data.meters.empty() || data.meters.back().id != id) {
                if (!data.meters.empty() && id <= data.meters.back().id) throw std::runtime_error("Meter groups must be in increasing ID order.");
                data.meters.push_back({static_cast<int>(id), {}});
            }
            auto& rows = data.meters.back().rows;
            if (idx != static_cast<long long>(rows.size())) throw std::runtime_error("Missing or duplicate sample index.");
            if (!rows.empty() && (r.energy < rows.back().energy || r.capacity != rows.front().capacity))
                throw std::runtime_error("Energy must not decrease; capacity must remain constant within a meter.");
            rows.push_back(r); ++data.records;
            if (data.records > 10000000) throw std::runtime_error("Stage 1 limit is 10 million records.");
        } catch (const std::exception& e) {
            throw std::runtime_error("CSV line " + std::to_string(line_number) + ": " + e.what());
        }
    }
    if (!input.eof()) throw std::runtime_error("Dataset read failed.");
    if (data.meters.empty()) throw std::runtime_error("Dataset has no readings.");
    for (const auto& m : data.meters) if (m.rows.size() < 100) throw std::runtime_error("Each meter needs at least 100 readings.");
    return data;
}

struct Error {
    std::size_t count = 0;
    double absolute = 0, squared = 0;
    void add(double predicted, double actual) { double e = predicted - actual; ++count; absolute += std::abs(e); squared += e * e; }
    double mae() const { return count ? absolute / count : 0; }
    double rmse() const { return count ? std::sqrt(squared / count) : 0; }
    void merge(const Error& x) { count += x.count; absolute += x.absolute; squared += x.squared; }
};
struct Forecast { int index; double actual, predicted, persistence; };
struct Result {
    int id;
    std::size_t samples, train_end, validation_end;
    double mean_power = 0, peak_power = 0, energy = 0, next_forecast = 0;
    std::size_t overload = 0, voltage_alerts = 0;
    Error validation, test, persistence_validation, persistence_test;
    std::vector<Forecast> forecasts;
};
using Vector = std::array<double, DIM>;
using Matrix = std::array<Vector, DIM>;

inline Vector solve(Matrix a, Vector b) {
    for (int k = 0; k < DIM; ++k) {
        int pivot = k;
        for (int i = k + 1; i < DIM; ++i) if (std::abs(a[i][k]) > std::abs(a[pivot][k])) pivot = i;
        if (std::abs(a[pivot][k]) < 1e-12) throw std::runtime_error("Singular model matrix.");
        std::swap(a[k], a[pivot]); std::swap(b[k], b[pivot]);
        for (int i = k + 1; i < DIM; ++i) {
            double f = a[i][k] / a[k][k];
            for (int j = k; j < DIM; ++j) a[i][j] -= f * a[k][j];
            b[i] -= f * b[k];
        }
    }
    Vector coefficients{};
    for (int i = DIM - 1; i >= 0; --i) {
        double value = b[i];
        for (int j = i + 1; j < DIM; ++j) value -= a[i][j] * coefficients[j];
        coefficients[i] = value / a[i][i];
    }
    return coefficients;
}

inline Result process_meter(const Meter& meter) {
    const auto& rows = meter.rows;
    const std::size_t n = rows.size(), train_end = n * 70 / 100, validation_end = n * 85 / 100;
    Result result{};
    result.id = meter.id; result.samples = n; result.train_end = train_end; result.validation_end = validation_end;
    double mean = 0;
    for (std::size_t i = 0; i < train_end; ++i) mean += rows[i].power;
    mean /= train_end;
    double variance = 0;
    for (std::size_t i = 0; i < train_end; ++i) variance += std::pow(rows[i].power - mean, 2);
    double scale = std::sqrt(variance / train_end);
    if (scale < 1e-9) scale = 1;
    auto features = [&](std::size_t target) {
        Vector x{}; x[0] = 1;
        for (int k = 1; k <= LAGS; ++k) x[k] = (rows[target - k].power - mean) / scale;
        return x;
    };
    Matrix a{}; Vector b{};
    for (std::size_t i = LAGS; i < train_end; ++i) {
        Vector x = features(i);
        double y = (rows[i].power - mean) / scale;
        for (int j = 0; j < DIM; ++j) {
            b[j] += x[j] * y;
            for (int k = 0; k <= j; ++k) a[j][k] += x[j] * x[k];
        }
    }
    for (int j = 0; j < DIM; ++j) {
        for (int k = j + 1; k < DIM; ++k) a[j][k] = a[k][j];
        if (j > 0) a[j][j] += RIDGE;
    }
    Vector weights = solve(a, b);
    auto predict = [&](std::size_t target) {
        Vector x = features(target);
        double normalized = 0;
        for (int j = 0; j < DIM; ++j) normalized += weights[j] * x[j];
        double value = std::max(0.0, mean + scale * normalized);
        if (!std::isfinite(value)) throw std::runtime_error("Non-finite forecast.");
        return value;
    };
    result.forecasts.reserve(n - train_end);
    for (std::size_t i = train_end; i < n; ++i) {
        double predicted = predict(i), actual = rows[i].power, persistence = rows[i - 1].power;
        if (i < validation_end) { result.validation.add(predicted, actual); result.persistence_validation.add(persistence, actual); }
        else { result.test.add(predicted, actual); result.persistence_test.add(persistence, actual); }
        result.forecasts.push_back({static_cast<int>(i), actual, predicted, persistence});
    }
    for (const auto& r : rows) {
        result.mean_power += r.power;
        result.peak_power = std::max(result.peak_power, r.power);
        if (r.power > r.capacity) ++result.overload;
        if (r.voltage < 207.0 || r.voltage > 253.0) ++result.voltage_alerts;
    }
    result.mean_power /= n;
    result.energy = rows.back().energy;
    result.next_forecast = predict(n);
    return result;
}
}
