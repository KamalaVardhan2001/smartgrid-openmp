# Review of the uploaded 64 meter experiment

Inputs: benchmark_summary.csv, benchmark_raw.csv, benchmark_correctness.csv, benchmark_info.txt and sequential_metrics.csv supplied by the user on October 19, 2026.

Verified 70 raw trial records across seven configurations, with ten trials each and 42 repeated workload passes per trial. Each pass evaluates 38720 validation/test forecasts for 64 meters and 129024 readings. Medians, quartiles, speedups and raw throughput calculations reproduce the reported summary within exported floating-point precision. The sequential and benchmark metadata share the dataset fingerprint 893e05134cae26ee. All seven correctness records report PASS and zero maximum numerical difference.

The direct sequential median is 5.72467261905 ms. The fastest observed median is 1.52652142857 ms at 16 threads, a 3.75014232483-fold speedup and a 73.3343453826 percent compute-time reduction. This applies to this fixed synthetic workload and timing scope, not the full CSV pipeline or field deployment.

The 16-thread timing interquartile range spans 1.35882619048 to 2.00026547619 ms, with IQR approximately 42.0 percent of its median. The four-thread IQR is approximately 4.7 percent of its median. Thus the fastest measured configuration also shows appreciable variability; retain raw times and quartiles in later figures. These quartiles are not confidence intervals, and no cause of variability is established by these files.

Median CPU usage is 6.2665 percent for sequential and 61.5603 percent for 16 threads under normalization to 16 exposed logical processors. These values are process CPU-time estimates rather than a direct measure of useful computation. Windows accounting granularity and runtime overhead affect short batches. Memory footprint and DRAM bandwidth remain unmeasured.

The separate initial sequential run has a compute time of 9.9389 ms. Its cold/initial allocation and execution conditions differ from the warmed repeated benchmark; do not combine that number with the OpenMP benchmark to calculate speedup. Use the matched direct sequential baseline from benchmark_summary.csv.

Initial held-out autoregression MAE/RMSE are 73.4220931171 W and 169.747982886 W; persistence MAE/RMSE are 63.1598745901 W and 194.042644627 W. Autoregression improves RMSE but worsens MAE on this simulated dataset. These accuracy results are separate from parallel speedup and should both be reported.

Runner verification: run_scaling.py was executed with real compiled simulator/benchmark subprocesses on small two- and four-meter test workloads, preserving an existing 64-meter snapshot. Combined summary and ZIP contents, restart/reuse behavior, source-change rejection and unchanged original data/results were checked. The 256/1024-meter experiments still require execution on the user's laptop; no results for those workloads are supplied here.
