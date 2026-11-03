# Stage 3 Workload scaling

Copy run_scaling.py into E:\SmartGrid_OpenMP beside your existing simulator.exe, benchmark.exe and C++ source files. No recompilation or Python package installation is needed.

Connect the charger and keep the Windows power mode unchanged. Close CPU-heavy programs. In VS Code PowerShell run:

```powershell
Set-Location E:\SmartGrid_OpenMP
```

```powershell
py -3.14 run_scaling.py
```

Your screenshot established that Python 3.14 is installed. The script also works with Python 3.15; it uses only Python's standard library. OpenMP work is still performed by C++, not Python.

The script preserves the existing 64-meter dataset/results in `experiments/meters_0064`, or validates an existing snapshot. It does not change the original root data/ or results/ folders. It then generates separate seven-day, five-minute, seed-42 workloads for 256 and 1024 meters, running the existing ten-trial OpenMP benchmark in each workload folder. OMP_WAIT_POLICY=PASSIVE is set for the child processes automatically.

Expected workload sizes:

| Meters | Readings | Evaluated forecasts per pass |
| --- | ---: | ---: |
| 64 | 129024 | 38720 |
| 256 | 516096 | 154880 |
| 1024 | 2064384 | 619520 |

Allow the processes to finish. Generation progress appears every 16 meters, then correctness and trial progress appear. Larger workloads can take several minutes on a laptop. No interactive input is required. There are no artificial sleeps or calculations added for timing.

For each workload, benchmark.csv outputs are saved under experiments/meters_NNNN/results/. Source copies and a run.log are retained beside them. Source changes are rejected when attempting to reuse an experiment folder, so results from different source versions are not silently mixed. Completed experiment folders are reused on later launches; partial or mismatched results stop the script for diagnosis instead of being overwritten.

If interrupted, retain the terminal error and run.log. Relaunching can reuse a fully generated dataset, but incomplete result sets are intentionally not overwritten automatically. A partially written dataset requires diagnosis before reuse.

At completion, experiments/scaling_summary.csv combines the workload summaries. A timestamped scaling_results_YYYYMMDD_HHMMSS.zip contains the small result tables, metadata, source copies and logs. Large meter datasets and per-target prediction CSVs are omitted from the ZIP. Attach the final ZIP to the conversation.

The default generated datasets occupy roughly a few hundred megabytes in total. The initial backup also copies the existing dataset/results. The exact sizes depend on CSV number lengths; your E: drive capacity is sufficient for these defaults. The runner requires at least 1 GiB free as an initial check, not a guarantee for arbitrary larger workloads.

These runs measure strong scaling within each fixed meter workload, and workload growth across 64/256/1024 meters. They are not a weak-scaling experiment, which would require workload per worker to remain constant. More meters are independently generated synthetic streams, not real physical meters.

CPU utilization remains the existing benchmark's process-wide estimate. The current automatic repetition calibration targets sequential batch duration, so faster configurations can have shorter batches and coarser Windows CPU accounting. Timing quartiles and raw values must be retained. DRAM bandwidth and memory footprint are still not measured; this runner does not add resource counters.

Record charger state and Windows power mode manually for the final report. The automatic environment_info.txt intentionally leaves these fields marked for manual recording. Do not claim results for workloads that have not completed on your laptop.
