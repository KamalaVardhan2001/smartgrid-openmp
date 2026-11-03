# Smart Grid OpenMP Scaling Findings

These findings use scaling_results_20261019_214302.zip uploaded by the user. Source files are preserved under scaling_user/. They describe the user's Windows 11 laptop, i5-1240P and 16 GB RAM as reported in the conversation, with GCC 16.1.0 and 16 logical processors exposed to OpenMP. Windows power mode and charger state were not captured automatically and must be recorded by the user.

## Verification

All 210 raw trial records were checked against the three seven-configuration summaries. Each configuration has ten trials. Forecast counts, reported median runtimes and speedups reproduce the raw records within export precision. All 21 correctness rows report PASS with zero maximum numerical difference. The C++ source snapshots for simulator.cpp, grid_core.hpp, sequential.cpp and benchmark.cpp match across all three experiment folders.

The initial sequential and 64-meter benchmark dataset fingerprints agree. Each larger workload has its own fingerprint, as expected for different meter counts. The compact uploaded archive does not include the full generated datasets, so this review checks the recorded fingerprints rather than rehashing the actual CSV bytes.

## Measured results

The fastest measured median uses 16 threads for all three workloads.

| Meters | Readings | Evaluated forecasts per pass | Sequential median ms | 16-thread median ms | Speedup | Compute-time reduction | 16-thread efficiency |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 129024 | 38720 | 5.7247 | 1.5265 | 3.7501 | 73.33% | 23.44% |
| 256 | 516096 | 154880 | 25.5905 | 4.8933 | 5.2297 | 80.88% | 32.69% |
| 1024 | 2064384 | 619520 | 102.7941 | 17.9356 | 5.7313 | 82.55% | 35.82% |

The timed workload fits a separate ridge autoregression model for each meter, evaluates validation/test targets, calculates historical statistics and constructs/replaces result arrays. It excludes CSV loading, validation/fingerprinting, output writes and correctness comparisons. Throughput counts evaluated validation/test forecasts and includes model-fitting costs in the denominator; it is not pure pretrained-model inference throughput or a physical meter-ingestion rate.

At 1024 meters the median normalized process CPU utilization is 6.33% for the direct sequential loop and 89.29% for 16 threads, normalized to the capacity of all 16 exposed logical processors. The corresponding reported median throughput is approximately 34.54 million evaluated forecasts/s for the combined fit/evaluate workload. Runtime activity, thread management and Windows CPU-time granularity affect interpretation.

## Scaling interpretation

Larger workloads show stronger measured speedup at the highest thread counts in this experiment. Parallel efficiency remains below ideal linear scaling at larger teams. The laptop has heterogeneous core types and hardware multithreading, so equal thread counts do not imply equal physical-core capacity. These files do not isolate the causes of scaling limits.

Four threads are faster than eight or twelve for the 64-meter workload. At 256 and 1024 meters, the median continues to fall as thread count increases through 16. This supports reporting workload-dependent scaling rather than recommending a universal thread count.

## Timing variation

For 64 meters, the 16-thread Q1-Q3 runtime is approximately 1.359-2.000 ms. Its IQR is about 42% of its median, while the four-thread IQR is about 4.7% of its median.

For 256 meters, seven sequential trials are approximately 22.6-26.3 ms and three are approximately 65.9-66.3 ms. All are retained. The sequential Q1-Q3 interval is approximately 24.14-55.95 ms, so a single median conceals appreciable variability. Background activity, thermal changes or other effects are possible, but the supplied data do not establish a cause. A confirmation session is useful before treating the exact 5.23-fold value as stable.

For 1024 meters, the sequential Q1-Q3 interval is approximately 99.10-104.66 ms, and the 16-thread interval is approximately 16.74-18.47 ms. Quartiles are descriptive spreads, not confidence intervals. The graphs include raw points and quartile ranges without deleting slow trials.

## Limits and pending measurements

Data are synthetic and come from one laptop. There is no physical smart-meter deployment, electrical switching, general fault classifier or evidence of real-world forecasting generalization. Threshold alerts are explicitly labeled as such. Accuracy for the original 64-meter dataset favors autoregression on RMSE and persistence on MAE; accuracy summaries for larger datasets are pending the next collector run.

Memory footprint and smart-grid application DRAM bandwidth were not measured in these timing experiments. The next collector measures process footprint separately. Its effective array-triad bandwidth is a supplementary memory-kernel metric and must not be relabeled as application DRAM bandwidth.

## Figures

1. 01_runtime_with_quartiles: per-workload runtime curves, sequential medians and quartile ranges.
2. 02_speedup: measured speedup against thread count with an ideal reference line.
3. 03_efficiency: speedup divided by actual thread count.
4. 04_throughput: reported median evaluated-forecast throughput.
5. 05_cpu_utilization: normalized whole-process CPU utilization.
6. 06_raw_trial_distributions: every raw trial point, including slow observations.
7. scaling_overview: a four-panel presentation overview.

PNG files are convenient for slides; SVG files preserve vector quality. All plots derive from the uploaded measurement tables, with no illustrative performance values.
