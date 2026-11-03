import argparse
import csv
from datetime import datetime
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
from zipfile import ZipFile, ZIP_DEFLATED


RESULT_FILES = (
    "benchmark_summary.csv",
    "benchmark_raw.csv",
    "benchmark_correctness.csv",
    "benchmark_info.txt",
)
SOURCE_FILES = ("simulator.cpp", "grid_core.hpp", "sequential.cpp", "benchmark.cpp")


def info(path):
    if not path.is_file():
        raise RuntimeError(f"Missing file: {path}")
    return dict(line.split("=", 1) for line in path.read_text(encoding="utf-8").splitlines() if "=" in line)


def read_csv(path):
    with path.open(newline="", encoding="utf-8-sig") as file:
        return list(csv.DictReader(file))


def validate_results(folder, meters):
    for name in RESULT_FILES:
        if not (folder / "results" / name).is_file():
            raise RuntimeError(f"Incomplete result set in {folder}. Keep it for diagnosis; do not merge it with a new run.")
    metadata = info(folder / "results" / "benchmark_info.txt")
    if metadata.get("meters") != str(meters) or metadata.get("readings") != str(meters * 2016):
        raise RuntimeError(f"Unexpected workload in {folder}.")
    if metadata.get("version") != "stage2-v1" or metadata.get("openmp_wait_policy") != "PASSIVE":
        raise RuntimeError(f"Unexpected benchmark version or wait policy in {folder}.")
    if metadata.get("trials") != "10":
        raise RuntimeError(f"Expected ten trials per configuration in {folder}.")
    summary = read_csv(folder / "results" / "benchmark_summary.csv")
    raw = read_csv(folder / "results" / "benchmark_raw.csv")
    checks = read_csv(folder / "results" / "benchmark_correctness.csv")
    expected = {row["configuration"] for row in summary}
    if not summary or "sequential" not in expected or len(expected) != len(summary):
        raise RuntimeError(f"Missing or duplicate configurations in {folder}.")
    if len(raw) != len(summary) * 10 or {row["configuration"] for row in checks} != expected:
        raise RuntimeError(f"Result counts disagree in {folder}.")
    if any(row["status"] != "PASS" for row in checks):
        raise RuntimeError(f"Correctness did not pass in {folder}.")
    for label in expected:
        group = [row for row in raw if row["configuration"] == label]
        if len(group) != 10 or {int(row["trial"]) for row in group} != set(range(1, 11)):
            raise RuntimeError(f"Trial records are incomplete in {folder}.")
    return metadata, summary


def capture_sources(root, folder):
    target = folder / "source"
    target.mkdir(exist_ok=True)
    for name in SOURCE_FILES:
        source, destination = root / name, target / name
        if not source.is_file():
            raise RuntimeError(f"Missing project source: {source}")
        if destination.exists():
            if hashlib.sha256(source.read_bytes()).digest() != hashlib.sha256(destination.read_bytes()).digest():
                raise RuntimeError(f"Source changed since the saved experiment: {name}. Preserve runs separately instead of mixing versions.")
        else:
            shutil.copy2(source, destination)


def execute(program, arguments, folder, environment):
    command = [str(program), *map(str, arguments)]
    print("\nRunning:", " ".join(command), flush=True)
    with (folder / "run.log").open("a", encoding="utf-8") as log:
        log.write("\n" + datetime.now().isoformat() + " " + " ".join(command) + "\n")
        log.flush()
        process = subprocess.Popen(command, cwd=folder, env=environment,
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", bufsize=1)
        try:
            for line in process.stdout:
                print(line, end="", flush=True)
                log.write(line)
            if process.wait() != 0:
                raise RuntimeError(f"Program failed. Read {folder / 'run.log'} before continuing.")
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait()


def main():
    parser = argparse.ArgumentParser(description="Preserve the 64-meter baseline and run isolated scaling experiments.")
    parser.add_argument("--meters", nargs="+", type=int, default=[256, 1024])
    args = parser.parse_args()
    if len(set(args.meters)) != len(args.meters) or any(m < 1 or m > 4096 or m == 64 for m in args.meters):
        raise RuntimeError("Choose distinct meter counts from 1..4096, excluding the saved 64-meter baseline.")
    root = Path(__file__).resolve().parent
    for name in ("simulator.exe", "benchmark.exe", *SOURCE_FILES):
        if not (root / name).is_file():
            raise RuntimeError(f"Place run_scaling.py in your existing project folder. Missing: {name}")
    if shutil.disk_usage(root).free < 1024**3:
        raise RuntimeError("Keep at least 1 GiB free before starting the default workloads.")
    baseline_info, _ = validate_results(root, 64)
    metrics = {row["metric"]: row["value"] for row in read_csv(root / "results" / "sequential_metrics.csv")}
    if metrics.get("dataset_fnv1a64_normalized_rows") != baseline_info.get("dataset_fnv1a64_normalized_rows"):
        raise RuntimeError("The root sequential and benchmark results refer to different datasets.")
    dataset_info = info(root / "data" / "dataset_info.txt")
    if (dataset_info.get("meters"), dataset_info.get("days"), dataset_info.get("seed")) != ("64", "7", "42"):
        raise RuntimeError("Expected the existing 64-meter, seven-day, seed-42 dataset in the project root.")
    experiments = root / "experiments"
    experiments.mkdir(exist_ok=True)
    baseline = experiments / "meters_0064"
    if not baseline.exists():
        baseline.mkdir()
        shutil.copytree(root / "data", baseline / "data")
        shutil.copytree(root / "results", baseline / "results")
    saved_info, _ = validate_results(baseline, 64)
    if saved_info.get("dataset_fnv1a64_normalized_rows") != baseline_info.get("dataset_fnv1a64_normalized_rows"):
        raise RuntimeError("Existing 64-meter snapshot has a different dataset. Preserve both experiments separately.")
    capture_sources(root, baseline)
    print(f"Preserved 64-meter baseline: {baseline}", flush=True)
    environment = os.environ.copy()
    environment["OMP_WAIT_POLICY"] = "PASSIVE"
    folders = [baseline]
    for meters in args.meters:
        folder = experiments / f"meters_{meters:04d}"
        folder.mkdir(exist_ok=True)
        capture_sources(root, folder)
        data_path = folder / "data" / "smart_meter_data.csv"
        if data_path.exists():
            data_meta = info(folder / "data" / "dataset_info.txt")
            if (data_meta.get("meters"), data_meta.get("days"), data_meta.get("seed")) != (str(meters), "7", "42"):
                raise RuntimeError(f"Existing dataset settings differ in {folder}.")
            print(f"Reusing existing dataset for {meters} meters.", flush=True)
        else:
            execute(root / "simulator.exe", [meters, 7, 42], folder, environment)
        present = [name for name in RESULT_FILES if (folder / "results" / name).exists()]
        if present and len(present) != len(RESULT_FILES):
            raise RuntimeError(f"Partial results in {folder}. Preserve them for diagnosis before starting another run.")
        if not present:
            execute(root / "benchmark.exe", [], folder, environment)
        meta, _ = validate_results(folder, meters)
        if meta.get("logical_processors_exposed") != baseline_info.get("logical_processors_exposed") or meta.get("compiler") != baseline_info.get("compiler"):
            raise RuntimeError(f"CPU availability or compiler differs from the baseline in {folder}.")
        print(f"Verified results for {meters} meters.", flush=True)
        folders.append(folder)
    combined = []
    for folder in folders:
        meters = int(info(folder / "results" / "benchmark_info.txt")["meters"])
        meta, summary = validate_results(folder, meters)
        for row in summary:
            combined.append({"meters": meters, "readings": meta["readings"],
                "dataset_fingerprint": meta["dataset_fnv1a64_normalized_rows"], **row})
    comparison = experiments / "scaling_summary.csv"
    with comparison.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=list(combined[0]))
        writer.writeheader()
        writer.writerows(combined)
    machine = experiments / "environment_info.txt"
    machine.write_text(f"os={platform.system()}\nos_release={platform.release()}\nos_version={platform.version()}\n"
        f"python={platform.python_version()}\nprocessor={platform.processor()}\n"
        "power_mode=record_manually\ncharger_state=record_manually\n", encoding="utf-8")
    archive = experiments / ("scaling_results_" + datetime.now().strftime("%Y%m%d_%H%M%S") + ".zip")
    with ZipFile(archive, "w", compression=ZIP_DEFLATED) as zipped:
        zipped.write(comparison, comparison.name)
        zipped.write(machine, machine.name)
        for folder in folders:
            for name in (*RESULT_FILES, "sequential_metrics.csv", "sequential_summary.csv"):
                file = folder / "results" / name
                if file.is_file(): zipped.write(file, file.relative_to(experiments))
            for file in [folder / "data" / "dataset_info.txt", folder / "run.log", *list((folder / "source").glob("*"))]:
                if file.is_file(): zipped.write(file, file.relative_to(experiments))
    print(f"\nALL REQUESTED WORKLOADS COMPLETED\nCombined summary: {comparison}\nUpload this ZIP: {archive}\n"
        "Large datasets and prediction CSVs are not included in the ZIP. Original root files are unchanged.", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
