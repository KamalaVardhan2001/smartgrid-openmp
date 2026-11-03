#include "grid_core.hpp"
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>

using Clock = std::chrono::steady_clock;
double seconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double>(end - start).count();
}

int main(int argc, char** argv) {
    try {
        if (argc > 2) throw std::runtime_error("Usage: sequential [dataset.csv]");
        std::string path = argc == 2 ? argv[1] : "data/smart_meter_data.csv";
        std::cout << "Sequential smart-grid analysis\nDataset: " << path << "\nLoading and validating data...\n";
        auto total_start = Clock::now();
        auto data = grid::load(path);
        auto loaded = Clock::now();
        std::cout << "Meters: " << data.meters.size() << "\nReadings: " << data.records
                  << "\nModel: ridge autoregression, 12 lags\nSplit: chronological 70/15/15\n"
                  << "Forecast: next 5-minute sample\nProcessing meters sequentially...\n";
        std::vector<grid::Result> results(data.meters.size());
        auto compute_start = Clock::now();
        for (std::size_t m = 0; m < data.meters.size(); ++m) results[m] = grid::process_meter(data.meters[m]);
        auto computed = Clock::now();
        grid::Error test, persistence, validation;
        std::size_t overloads = 0, voltage_alerts = 0, evaluated = 0;
        std::filesystem::create_directories("results");
        std::ofstream summary("results/sequential_summary.csv");
        std::ofstream predictions("results/sequential_predictions.csv");
        std::ofstream metrics("results/sequential_metrics.csv");
        if (!summary || !predictions || !metrics) throw std::runtime_error("Cannot create output files; close them in Excel if they are locked.");
        summary << "meter_id,samples,train_samples,validation_targets,test_targets,mean_power_W,peak_power_W,energy_kWh,overload_readings,voltage_alert_readings,next_forecast_W,validation_mae_W,test_mae_W,test_rmse_W,persistence_test_mae_W,persistence_test_rmse_W\n";
        predictions << "meter_id,sample_index,target_elapsed_seconds,split,actual_W,predicted_W,persistence_W\n";
        summary << std::setprecision(12); predictions << std::setprecision(12);
        for (const auto& r : results) {
            test.merge(r.test); persistence.merge(r.persistence_test); validation.merge(r.validation);
            overloads += r.overload; voltage_alerts += r.voltage_alerts; evaluated += r.forecasts.size();
            summary << r.id << ',' << r.samples << ',' << r.train_end << ',' << r.validation.count << ',' << r.test.count << ','
                    << r.mean_power << ',' << r.peak_power << ',' << r.energy << ',' << r.overload << ',' << r.voltage_alerts << ','
                    << r.next_forecast << ',' << r.validation.mae() << ',' << r.test.mae() << ',' << r.test.rmse() << ',' << r.persistence_test.mae() << ',' << r.persistence_test.rmse() << '\n';
            for (const auto& f : r.forecasts)
                predictions << r.id << ',' << f.index << ',' << 1LL * f.index * grid::INTERVAL << ','
                            << (static_cast<std::size_t>(f.index) < r.validation_end ? "validation" : "test") << ','
                            << f.actual << ',' << f.predicted << ',' << f.persistence << '\n';
        }
        summary.close(); predictions.close();
        if (!summary || !predictions) throw std::runtime_error("Failed while exporting output files.");
        auto exported = Clock::now();
        double compute = seconds(compute_start, computed);
        metrics << "metric,value,unit\n" << std::setprecision(12)
                << "meters," << data.meters.size() << ",count\nreadings," << data.records << ",count\n"
                << "evaluated_forecasts," << evaluated << ",count\ntest_forecasts," << test.count << ",count\n"
                << "dataset_load_seconds," << seconds(total_start, loaded) << ",s\n"
                << "compute_seconds," << compute << ",s\n"
                << "post_compute_export_seconds," << seconds(computed, exported) << ",s\n"
                << "evaluated_forecasts_per_compute_second," << evaluated / compute << ",forecasts/s\n"
                << "validation_mae," << validation.mae() << ",W\ntest_mae," << test.mae() << ",W\n"
                << "test_rmse," << test.rmse() << ",W\npersistence_test_mae," << persistence.mae() << ",W\n"
                << "persistence_test_rmse," << persistence.rmse() << ",W\n"
                << "overload_readings," << overloads << ",count\nvoltage_alert_readings," << voltage_alerts << ",count\n"
                << "dataset_fnv1a64_normalized_rows," << std::hex << data.hash << std::dec << ",hex\n";
        metrics.close();
        if (!metrics) throw std::runtime_error("Metrics export failed.");
        std::cout << std::fixed << std::setprecision(6)
                  << "\nCOMPLETED\nData load time: " << seconds(total_start, loaded)
                  << " s\nCompute time (fit + evaluation + statistics): " << compute
                  << " s\nPost-compute export time: " << seconds(computed, exported)
                  << " s\nEvaluated forecasts: " << evaluated
                  << "\nEvaluated forecasts per compute second: " << evaluated / compute
                  << "\nAR test MAE: " << test.mae() << " W\nAR test RMSE: " << test.rmse()
                  << " W\nPersistence test MAE: " << persistence.mae() << " W\nPersistence test RMSE: " << persistence.rmse()
                  << " W\nOverload readings: " << overloads << "\nVoltage alert readings: " << voltage_alerts
                  << "\nSaved three CSV files in results/.\n"
                  << "This is one sequential run, not a final parallel benchmark.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
