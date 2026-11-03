# Smart Grid Project Stage 1

Performance Analysis of Parallel Processing for Smart Electricity Grid Management

This starter implements a synthetic smart-meter simulator and a true sequential C++ processing baseline. It needs no sensor, Firebase, Python package or GPU. The next implementation stage will add OpenMP around the same meter-processing function. This archive does not yet contain the parallel implementation or a final performance study.

## 1 Files

Keep these three source files together in E:\SmartGrid_OpenMP:

| File | Purpose |
| --- | --- |
| simulator.cpp | Generates repeatable synthetic readings and dataset metadata |
| grid_core.hpp | Defines data structures, validates CSV, fits and evaluates each meter model |
| sequential.cpp | Processes meters one at a time, records timings and exports results |

Keep your existing omp_check.cpp as well. It is a separate setup test and does not need replacement.

## 2 Windows instructions

Extract this ZIP. Copy its files directly into E:\SmartGrid_OpenMP. Confirm that simulator.cpp, sequential.cpp and grid_core.hpp appear together at that level, rather than in an extra nested folder.

Open E:\SmartGrid_OpenMP in VS Code. Select Terminal > New Terminal. Use PowerShell and run each command individually.

```powershell
Set-Location E:\SmartGrid_OpenMP
```

Compile the simulator:

```powershell
& "C:\msys64\ucrt64\bin\g++.exe" -O2 -std=c++17 -Wall -Wextra -Wpedantic simulator.cpp -o simulator.exe
```

Compile the sequential program:

```powershell
& "C:\msys64\ucrt64\bin\g++.exe" -O2 -std=c++17 -Wall -Wextra -Wpedantic sequential.cpp -o sequential.exe
```

Successful compilation normally returns to the prompt without printing a message. These commands use the same UCRT64 compiler you have already verified. There is no OpenMP directive in this sequential stage, so -fopenmp is not needed here.

Generate the initial workload:

```powershell
.\simulator.exe 64 7 42
```

Arguments mean 64 meters, seven simulated days per meter and random seed 42. You do not need to type additional input or wait seven real days. The data are generated as quickly as the computer can write them.

Run analysis:

```powershell
.\sequential.exe
```

The first program creates data/ automatically. The second creates results/ automatically. Relative paths are resolved from the terminal's current folder. All generated files therefore remain on E: when run as shown.

The source archive is small. The default generated data and CSV results together are roughly tens of megabytes, not gigabytes. Start with the default size; do not increase it before confirming a successful run.

## 3 Expected output and files

The default run produces exactly 129024 readings: 64 x 7 x 288 samples. There are 2016 samples per meter at a five-minute interval.

Dataset files:

- data/smart_meter_data.csv
- data/dataset_info.txt

Analysis files:

- results/sequential_summary.csv: one row per meter, historical statistics, alert counts, next-step forecast and accuracy.
- results/sequential_predictions.csv: validation and test targets with actual load, autoregression forecast and persistence forecast.
- results/sequential_metrics.csv: pooled error metrics, workload counts, timing scopes, throughput and dataset fingerprint.

The default program evaluates 38720 historical forecasts: 64 x (302 validation + 303 test). Its test subset contains 19392 targets. It also calculates one future forecast per meter, which has no observed future target and is excluded from reported accuracy and evaluated-forecast count.

Actual times and accuracy values must come from your own run. Do not copy the author's container timings into your laptop results.

You can open CSV results with Excel, but close them before running analysis again because Windows may lock open files. VS Code can also display them as text. There is no interactive dashboard yet.

## 4 Simulation assumptions

Every meter contains an individually scaled daily consumption pattern, morning and evening peaks, correlated random variation, occasional appliance activity and a weekly factor. Selected meters have deliberately injected overload episodes and voltage sags to demonstrate threshold monitoring.

| Column | Meaning |
| --- | --- |
| meter_id | Positive meter identifier |
| sample_index | Zero-based sample number within that meter |
| elapsed_seconds | Simulated time from zero in 300-second increments |
| voltage_V | RMS voltage in volts |
| current_A | Current in amperes |
| power_W | Active power in watts |
| energy_kWh | Cumulative simulated energy through the represented interval |
| frequency_Hz | Frequency in hertz |
| power_factor | Dimensionless power factor |
| capacity_W | Configured demonstration threshold for that meter |

Active power and current satisfy P = V x I x power_factor, apart from CSV rounding. The energy model treats each power reading as constant over its five-minute interval, adding P x 300 / 3600000 kWh. Forecast inputs use power only. Voltage alerts use 207-253 V as a configurable demonstration range; these are not protection-system settings or a claim of standards compliance.

Data are stored in meter order, then time order. The parser rejects unexpected columns, malformed/non-finite numbers, negative power/current/energy, invalid power factor, missing/duplicate indices, irregular intervals, decreasing energy and inconsistent capacity within a meter. These checks do not constitute complete validation of an arbitrary external electricity dataset. Real datasets require a separate adapter.

Seeded output is repeatable for the same compiled program/runtime and arguments. C++ standard-library random distributions may differ across toolchains; preserve the actual generated CSV and use its fingerprint for later comparisons. The metrics fingerprint is FNV-1a over normalized data rows, including normalized newlines, excluding the header. It is an identity aid, not a security checksum.

## 5 Forecasting method

This implementation uses ridge-regularized autoregression with 12 lagged power inputs plus an intercept. It is a fitted linear forecasting model, not the guide's LSTM. The original base implementation remains a credited reference for the monitoring workflow; no claim is made that its neural network has been converted to C++.

For each meter, use the first floor(0.70 N) readings for training, the following readings up to floor(0.85 N) for validation, and the remainder for testing. Fit mean and standard deviation using training power only. Each feature vector contains the preceding 12 observed values. Training targets are strictly before the training cutoff. Ridge strength is 0.001; the intercept is not penalized. The supplied implementation solves the small regularized normal equations with partial pivoting and clips negative forecasts to zero.

At sample t:

predicted_power(t) = training_mean + training_std x [intercept + sum(weight[k] x standardized_power(t-k), k=1..12)]

Twelve five-minute observations represent one hour of historical input. The target is one sample ahead, corresponding to five minutes. This differs from the original base code's five-second acquisition cadence and incorrect horizon labeling.

Validation and testing use chronological rolling one-step evaluation: each prediction can use earlier observations that have already arrived, including earlier test observations. It never uses the target value or later observations as inputs. The model and training scaler remain fixed throughout validation and test evaluation. This is not a recursively generated multi-step forecast.

Persistence predicts the most recently observed power. Both models are evaluated on exactly the same targets. Report both MAE and RMSE in watts. The autoregression may outperform persistence on one measure and underperform on another; preserve these results without cherry-picking.

Historical peak load, mean load, energy and threshold counts describe the whole observed dataset. They are not forecast inputs. Overload/voltage counts are numbers of flagged readings, not numbers of independent fault episodes. No fault classifier or physical load-balancing controller is implemented.

## 6 Timing interpretation

- Dataset load time includes CSV reading, validation, normalized-row fingerprinting and data allocation.
- Compute time includes model fitting, validation/test forecasting, historical per-meter statistics and construction of result arrays. It excludes console progress, dataset loading and output-file writing. The outer result container is allocated before timing; internal model/result allocations remain timed.
- Evaluated forecasts per compute second equals validation-plus-test forecast count divided by that compute time. This is a combined fit-and-evaluate workload measure, not isolated pretrained-model inference throughput. One extra future forecast per meter and historical statistics are included in the timed work but excluded from the evaluated-target count.
- Post-compute export time includes pooled result aggregation and writing summary/prediction CSVs; it excludes writing the metrics CSV itself. It is not a full end-to-end timing.

This starter reports a single sequential run. Short compute times are normal for a small, efficient C++ workload. Final experiments must use sufficiently large workloads, repeated trials, matching compiler optimization, identical dataset fingerprints and equal numerical work in sequential/OpenMP versions. Never add sleeps or meaningless calculations to manufacture a speedup.

CPU utilization, memory footprint and hardware memory-bandwidth measurements are not collected at this stage. Their absence must be preserved in any interim report. OpenMP speedup cannot be reported until the parallel implementation and correctness checks exist.

## 7 Re-running and troubleshooting

The simulator refuses to replace an existing dataset by default. This protects later sequential/parallel comparisons. To deliberately regenerate it:

```powershell
.\simulator.exe 64 7 42 --force
```

This replaces dataset files. Re-run analysis afterward; the analysis replaces its three result files.

To analyze another file with exactly this schema and ordering:

```powershell
.\sequential.exe "E:\path\to\dataset.csv"
```

If compilation says grid_core.hpp is missing, place it beside sequential.cpp and check the current directory. If an executable cannot find a runtime DLL, verify that C:\msys64\ucrt64\bin is on PATH, as it was during your successful OpenMP setup check. If the dataset cannot be found, run the simulator first from E:\SmartGrid_OpenMP. If a program reports ERROR, retain the message and do not use partial outputs as results.

## 8 Review explanation

"Our software simulates multiple electricity meters with timestamped readings. The sequential baseline processes one meter at a time, fits a forecasting model from earlier readings, evaluates future targets, and identifies threshold violations. We will parallelize the same meter-processing function with OpenMP and compare correctness, execution time, throughput and resource use on the same dataset."

The paper and final documentation must describe the implemented C++/OpenMP and autoregression design rather than retain claims about Python multiprocessing or LSTM that do not match this code.
