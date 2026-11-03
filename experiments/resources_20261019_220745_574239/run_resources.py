import argparse
import csv
import ctypes
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys
import time
from zipfile import ZipFile, ZIP_DEFLATED


class MemoryReader:
    def __init__(self, pid):
        self.pid = pid
        self.handle = None
        if os.name == "nt":
            from ctypes import wintypes
            class Counters(ctypes.Structure):
                _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
                    (name, ctypes.c_size_t) for name in (
                        "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage",
                        "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]
            self.Counters = Counters
            self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            self.psapi = ctypes.WinDLL("psapi", use_last_error=True)
            self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            self.kernel.OpenProcess.restype = wintypes.HANDLE
            self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            self.kernel.CloseHandle.restype = wintypes.BOOL
            self.psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
            self.psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
            self.handle = self.kernel.OpenProcess(0x1000, False, pid)
            if not self.handle: raise OSError(ctypes.get_last_error(), "Cannot query child-process memory")
            self.backend = "Windows_GetProcessMemoryInfo"
        elif Path(f"/proc/{pid}/status").exists():
            self.backend = "Linux_proc_status_verification"
        else:
            raise RuntimeError("Memory sampling is implemented for Windows, with a Linux test fallback.")

    def sample(self):
        if self.handle:
            counters = self.Counters()
            counters.cb = ctypes.sizeof(counters)
            if not self.psapi.GetProcessMemoryInfo(self.handle, ctypes.byref(counters), counters.cb):
                return None
            return counters.WorkingSetSize/2**20, counters.PeakWorkingSetSize/2**20, counters.PrivateUsage/2**20
        try:
            values = {}
            for line in Path(f"/proc/{self.pid}/status").read_text().splitlines():
                if line.startswith(("VmRSS:", "VmHWM:")):
                    name, value, _ = line.split()
                    values[name.rstrip(":")] = float(value)/1024
            if "VmRSS" not in values: return None
            return values["VmRSS"], values.get("VmHWM",values["VmRSS"]), None
        except FileNotFoundError:
            return None

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def monitor(command, directory, environment, meters):
    samples = []
    started = time.perf_counter()
    with (directory/"memory_run.log").open("w",encoding="utf-8") as log:
        process = subprocess.Popen(command,cwd=directory,env=environment,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
        reader = None
        try:
            reader = MemoryReader(process.pid)
            with (directory/"memory_samples.csv").open("w",newline="",encoding="utf-8") as file:
                writer=csv.writer(file)
                writer.writerow(["meters","elapsed_seconds","working_set_MiB","reported_peak_working_set_MiB","private_commit_MiB","backend"])
                while True:
                    sample = reader.sample()
                    if sample is not None:
                        samples.append(sample)
                        writer.writerow([meters,time.perf_counter()-started,sample[0],sample[1],sample[2],reader.backend])
                    if process.poll() is not None: break
                    time.sleep(.05)
            if process.returncode != 0: raise RuntimeError(f"Memory probe failed. Read {directory/'memory_run.log'}")
            if not samples: raise RuntimeError("No valid memory samples were captured.")
            with (directory/"results/benchmark_correctness.csv").open(newline="",encoding="utf-8") as file:
                checks=list(csv.DictReader(file))
            if not checks or any(row["status"]!="PASS" for row in checks):raise RuntimeError("Instrumented-run correctness failed.")
            private=[x[2] for x in samples if x[2] is not None]
            return {"meters":meters,"samples":len(samples),"sample_interval_seconds":.05,
                "max_sampled_working_set_MiB":max(x[0] for x in samples),
                "max_reported_peak_working_set_MiB":max(x[1] for x in samples),
                "max_sampled_private_commit_MiB":max(private) if private else "",
                "backend":reader.backend,"scope":"entire_benchmark_process_all_configurations_load_compute_export"}
        finally:
            if process.poll() is None:
                process.terminate();process.wait()
            if reader:reader.close()


def main():
    parser=argparse.ArgumentParser(description="Collect separate process-memory and array-bandwidth measurements.")
    parser.add_argument("--meters",nargs="+",type=int,default=[64,256,1024])
    parser.add_argument("--bandwidth-elements",type=int,default=4194304)
    parser.add_argument("--bandwidth-trials",type=int,default=7)
    parser.add_argument("--bandwidth-passes",type=int,default=10)
    args=parser.parse_args()
    if not args.meters or len(set(args.meters))!=len(args.meters) or any(x<1 for x in args.meters):raise RuntimeError("Choose distinct positive meter counts.")
    root=Path(__file__).resolve().parent
    for name in ("benchmark.exe","bandwidth.exe","sequential.exe"):
        if not (root/name).is_file():raise RuntimeError(f"Missing {name}. Compile it before starting this step.")
    datasets=[]
    for meters in args.meters:
        dataset=root/"experiments"/f"meters_{meters:04d}"/"data/smart_meter_data.csv"
        meta=dataset.parent/"dataset_info.txt"
        if not dataset.is_file() or not meta.is_file():raise RuntimeError(f"Missing completed dataset for {meters} meters.")
        values=dict(line.split("=",1) for line in meta.read_text().splitlines() if "=" in line)
        if values.get("meters")!=str(meters):raise RuntimeError("Dataset metadata does not match folder.")
        datasets.append((meters,dataset))
    output=root/"experiments"/("resources_"+datetime.now().strftime("%Y%m%d_%H%M%S_%f"))
    output.mkdir(parents=True)
    environment=os.environ.copy();environment["OMP_WAIT_POLICY"]="PASSIVE"
    summaries=[]
    accuracy=[]
    for meters,dataset in datasets:
        directory=output/f"meters_{meters:04d}";directory.mkdir()
        print(f"Sampling memory for {meters} meters. The child log is saved in {directory}.",flush=True)
        row=monitor([str(root/"benchmark.exe"),str(dataset),"3","1"],directory,environment,meters)
        summaries.append(row)
        print(f"Completed: maximum reported process working-set peak {row['max_reported_peak_working_set_MiB']:.2f} MiB",flush=True)
        print(f"Exporting forecast accuracy for {meters} meters...",flush=True)
        with (directory/"sequential_run.log").open("w",encoding="utf-8") as log:
            evaluated=subprocess.run([str(root/"sequential.exe"),str(dataset)],cwd=directory,env=environment,
                stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
        if evaluated.returncode!=0:raise RuntimeError(f"Accuracy export failed. Read {directory/'sequential_run.log'}")
        with (directory/"results/sequential_metrics.csv").open(newline="",encoding="utf-8") as file:
            values={row["metric"]:row["value"] for row in csv.DictReader(file)}
        accuracy.append({"meters":meters,**{key:values[key] for key in (
            "test_forecasts","test_mae","test_rmse","persistence_test_mae","persistence_test_rmse","dataset_fnv1a64_normalized_rows")}})
    with (output/"memory_summary.csv").open("w",newline="",encoding="utf-8") as file:
        writer=csv.DictWriter(file,fieldnames=list(summaries[0]));writer.writeheader();writer.writerows(summaries)
    with (output/"accuracy_summary.csv").open("w",newline="",encoding="utf-8") as file:
        writer=csv.DictWriter(file,fieldnames=list(accuracy[0]));writer.writeheader();writer.writerows(accuracy)
    bandwidth=output/"array_bandwidth";bandwidth.mkdir()
    print("Running separate array-bandwidth kernel...",flush=True)
    with (bandwidth/"run.log").open("w",encoding="utf-8") as log:
        run=subprocess.run([str(root/"bandwidth.exe"),str(args.bandwidth_elements),str(args.bandwidth_trials),str(args.bandwidth_passes)],
            cwd=bandwidth,env=environment,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
    if run.returncode!=0:raise RuntimeError(f"Bandwidth run failed. Read {bandwidth/'run.log'}")
    (output/"RESOURCE_SCOPE.txt").write_text(
        "Memory is sampled every 50 ms from a separate benchmark process for each workload.\n"
        "Scope includes dataset load, all thread configurations, correctness and output.\n"
        "The Windows API supplies current/peak working set and private commit; private commit is not resident RAM.\n"
        "Sampled peaks may miss very brief events; reported lifetime peaks apply through the sampled time.\n"
        "These instrumented runs use 3 trials and 1 pass and are for memory only, not replacements for the original timing study.\n"
        "Separate sequential runs export forecasting accuracy after memory sampling ends; their timings are not combined with the original study.\n"
        "Bandwidth is a separate array-triad effective useful-byte rate, not application DRAM traffic.\n"
        "Original datasets, timing results and source files were not modified.\n",encoding="utf-8")
    for name in ("run_resources.py","bandwidth.cpp"):
        if (root/name).is_file():(output/name).write_bytes((root/name).read_bytes())
    archive=output.with_suffix(".zip")
    with ZipFile(archive,"w",ZIP_DEFLATED) as zipped:
        for file in output.rglob("*"):
            if file.is_file() and file.name!="sequential_predictions.csv":zipped.write(file,file.relative_to(output))
    print(f"\nRESOURCE COLLECTION COMPLETED\nUpload this ZIP: {archive}",flush=True)


if __name__=="__main__":
    try:main()
    except (OSError,RuntimeError,ValueError,KeyError) as error:
        print(f"ERROR: {error}",file=sys.stderr);sys.exit(1)
