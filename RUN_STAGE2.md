# Stage 2 OpenMP benchmark

Copy benchmark.cpp into E:\SmartGrid_OpenMP beside the existing grid_core.hpp. Do not regenerate your dataset. This stage reuses the exact Stage 1 data and meter-processing function.

In VS Code PowerShell:

```powershell
Set-Location E:\SmartGrid_OpenMP
```

```powershell
& "C:\msys64\ucrt64\bin\g++.exe" -O2 -std=c++17 -Wall -Wextra -Wpedantic -fopenmp benchmark.cpp -o benchmark.exe
```

Before launching the benchmark, set this environment variable in the same terminal:

```powershell
$env:OMP_WAIT_POLICY = "PASSIVE"
```

Then launch:

```powershell
.\benchmark.exe
```

Close CPU-heavy applications and connect laptop power. Keep the power mode consistent across later experiments. Do not change Windows CPU affinity for this first run. This benchmark may take tens of seconds or longer, depending on OpenMP overhead and available resources; it prints progress after each complete trial. It does not wait for keyboard input.

The default run tests a direct sequential loop, an OpenMP one-thread team, and requested teams of 2, 4, 8, 12 and 16 threads when supported by the logical processor count. Team size is recorded rather than assumed. Your i5 has a mixture of core types, so scaling need not be linear and the fastest configuration need not use the most threads.

Every configuration runs the same grid::process_meter function on the same data, with identical model fitting, evaluation, statistics and result structure. OpenMP partitions whole meters using schedule(static). Each worker writes to a distinct output slot, and no worker writes CSV files. Per-meter temporal order is retained.

The true sequential baseline and all OpenMP configurations are compiled in one executable with the same optimization flags. The earlier sequential.exe timing is a preliminary check; use this benchmark's matched baseline for reported speedup.

Before timing, each configuration is warmed and compared against the reference. Identity, target indices, alert counts, summary statistics, forecast arrays and error sums are checked. Numerical comparison uses abs_difference <= 1e-8 + 1e-10 * abs(reference). The final output of every timed trial is also checked outside timing. Individual repetitions within a trial are not separately compared.

Since the initial workload is short, one trial repeats the complete fit-and-evaluate workload several times. A calibration based on one sequential warm run selects a fixed number of passes for every configuration, targeting approximately 0.25 seconds of sequential work per trial, capped at 256 passes. There are no artificial sleeps or dummy calculations. These passes are repeated processing of the SAME data, not additional unique meters or independent samples. This measures warm repeated workload processing; separately measure cold-start or new-data behavior later.

For each of ten trials, configuration order is shuffled using a recorded fixed seed. Raw wall time is divided by passes to obtain seconds per workload pass. Summary values are medians with first and third quartiles calculated by linear interpolation. Speedup is median sequential time divided by median configuration time. Efficiency is speedup divided by actual team size. Throughput counts evaluated validation/test forecasts per pass, divided by compute time; compute includes fitting and statistics, so it is not pure inference throughput.

Whole-process CPU time is measured with Windows GetProcessTimes on your laptop, including worker runtime overhead. CPU percentage is normalized to the full number of logical processors exposed to OpenMP. Therefore a fully busy single thread on a 16-logical-processor machine is around 6.25 percent under ideal conditions. Windows CPU accounting can have coarser resolution than wall-clock timing. Worker spinning and oversubscription can affect this metric; a larger percentage alone is not evidence of useful speedup. PASSIVE requests that idle workers yield rather than continue spinning; setting it before process launch helps avoid lingering worker activity during a subsequent sequential trial. It is a runtime policy request, not a guarantee of a precise sleep behavior. The setting is recorded in the metadata. Non-Windows verification uses process CPU time from std::clock.

Input loading, validation, correctness comparisons and output writing are excluded from compute timing. Model fitting, forecasting, per-meter statistics, internal allocations/result replacement, OpenMP team management and a minimal result-consumption operation remain included in both modes. The two outer result containers are allocated before timing. All files are written by the coordinator after timing.

Outputs:

| File | Purpose |
| --- | --- |
| results/benchmark_summary.csv | Median runtime, quartiles, speedup, efficiency, throughput and CPU utilization |
| results/benchmark_raw.csv | All raw trial values and repetition counts |
| results/benchmark_correctness.csv | PASS status and maximum numerical difference by configuration |
| results/benchmark_info.txt | Compiler, data identity, OpenMP settings, measurement scope and limitations |

The Stage 1 result files remain unchanged. Re-running Stage 2 replaces only its four benchmark files. Close them in Excel before re-running.

Optional arguments after the default run succeeds:

```powershell
.\benchmark.exe data/smart_meter_data.csv 10 0
```

Arguments are dataset path, trial count (3 to 30) and passes per trial (0 for automatic, or 1 to 1000). Changing pass count changes repetition duration, not the unique workload size. Strong-scaling experiments require fixed meter/sample counts while varying threads. Testing more meters/days requires separate datasets; do not silently overwrite the baseline dataset.

Limitations: data are synthetic; only one hardware platform is evaluated; CSV I/O is excluded; training and inference are combined; memory footprint and hardware DRAM bandwidth are not collected at this stage. This is not proof of industrial-scale deployment or physical grid control. Do not omit slow configurations or claim linear scaling in advance.

Before final submission, retain laptop specifications, Windows version, power settings and dataset files alongside these raw results. Additional workload sizes, resource measurements and graphs are the next stage.
