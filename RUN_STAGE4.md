# Stage 4 Graphs and resource measurements

The graphs/ folder contains seven figures in PNG and SVG formats generated from your uploaded laptop results. The scaling_user/ folder preserves those measurement files and source snapshots. SCALING_FINDINGS.md describes verified findings and limitations. No container timing values are presented as your laptop measurements.

## Next Windows steps

Copy bandwidth.cpp and run_resources.py directly into E:\SmartGrid_OpenMP. You may copy this entire package there as well. No existing source files have the same names. Keep your existing data, experiment folders, executables and source files.

In VS Code PowerShell:

```powershell
Set-Location E:\SmartGrid_OpenMP
```

Compile the separate array-bandwidth benchmark:

```powershell
& "C:\msys64\ucrt64\bin\g++.exe" -O2 -std=c++17 -Wall -Wextra -Wpedantic -fopenmp bandwidth.cpp -o bandwidth.exe
```

Run the resource collector:

```powershell
py -3.14 run_resources.py
```

No Python packages or administrator driver installation is needed. Keep the charger connected, use the same Windows power mode, and close CPU-heavy programs. Allow a few minutes. Detailed child output is written to logs; the terminal prints which workload is being processed.

The collector locates the preserved 64-, 256- and 1024-meter datasets under experiments/meters_NNNN/data. It creates a NEW timestamped experiments/resources_... folder. Original timing results and datasets are not modified.

## What is measured

### Process memory footprint

For each workload a separate benchmark.exe process runs while Python queries its memory statistics every 50 ms. On Windows, GetProcessMemoryInfo reports working set, peak working set and private commit. Working set is resident process memory; private commit is committed private memory and is not synonymous with resident RAM.

The reported scope is the ENTIRE benchmark process: input loading, all tested thread configurations, correctness checks and export. It is not memory for an individual thread configuration or only the numeric kernel. Parent Python memory is excluded. The maximum sampled working set and maximum reported lifetime peak are recorded separately. Very brief events after the last successful sample may be missed. Resource runs use three trials and one workload pass because they measure footprint, not final execution speed. Their timing CSVs must not replace the ten-trial timing study.

### Forecast accuracy on every workload

After memory sampling ends, sequential.exe exports forecast accuracy for that workload in the new resource folder. accuracy_summary.csv combines held-out autoregression and persistence MAE/RMSE for 64, 256 and 1024 meters. This also checks accuracy on the larger synthetic sets. These remain simulated-data results, not external field validation. The sequential export times are not mixed into the parallel timing study.

### Effective array bandwidth

A separate OpenMP C++ benchmark runs an array triad, output = b + 0.5 x c, then swaps output and c so successive passes depend on previous values. The default three arrays contain 4194304 doubles each, a total of 96 MiB. Each trial performs ten timed passes after a warm-up, with seven trials per configuration. Every output element is checked after timing.

Effective bandwidth = elements x passes x 24 useful bytes / elapsed seconds. The 24-byte model counts two double reads and one double write. GB/s uses decimal units. Initialization, warm-up, correctness checks and output are outside timing; team-management overhead and array-role swaps remain inside timing.

This is an EFFECTIVE ARRAY BANDWIDTH measurement. Actual DRAM traffic can differ because of caching, write allocation and runtime behavior. It is not the smart-grid application's DRAM bandwidth, is not a hardware-counter result, and is not an official STREAM result. In your report it belongs in a separately labeled platform memory-kernel experiment. Application DRAM bandwidth remains unmeasured unless supported hardware counters are collected later.

## Results to upload

At the end, the terminal prints RESOURCE COLLECTION COMPLETED and a timestamped ZIP path in experiments/. Attach that ZIP. It includes memory samples/summary, accuracy summary, small per-workload result files, array-bandwidth raw/summary data, metadata, logs and collector source. Large datasets and per-target prediction CSVs are excluded from the ZIP.

If an error occurs, retain its message and the relevant log. Do not use partial results. Each launch creates a new folder, so previous resource attempts remain available.

## Verification and remaining limits

bandwidth.cpp compiled without warnings in a Linux GCC/OpenMP environment. The default 96-MiB array experiment passed full-array correctness checks and its exported useful-byte rates/medians matched independent calculations. The complete resource runner was tested with real child processes and a Linux /proc memory backend, including accuracy export and compact archive creation. The Windows API branch follows Microsoft documentation but needs verification on your laptop. No Windows memory values or bandwidth values have been invented for this package.

Windows API references:

- https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo
- https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex

## Recreating the existing graphs

This is optional. make_scaling_graphs.py reads scaling_user/scaling_summary.csv and raw measurement files. It requires matplotlib and writes to output/stage4/graphs relative to its working directory. The supplied PNG/SVG files are already ready to use, so installing plotting packages is unnecessary for the next resource step.
