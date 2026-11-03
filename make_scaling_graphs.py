import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT=Path('scaling_user')
OUT=Path('output/stage4/graphs');OUT.mkdir(parents=True,exist_ok=True)
rows=list(csv.DictReader((ROOT/'scaling_summary.csv').open()))
groups={m:[r for r in rows if int(r['meters'])==m] for m in [64,256,1024]}
colors={64:'#0072B2',256:'#D55E00',1024:'#009E73'}
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'axes.spines.top':False,'axes.spines.right':False,'axes.titleweight':'bold','savefig.dpi':220})
def save(fig,name):
 fig.savefig(OUT/(name+'.png'),bbox_inches='tight')
 fig.savefig(OUT/(name+'.svg'),bbox_inches='tight')
 plt.close(fig)
def series(ax,key,multiplier=1,ylabel='',ideal=None):
 for m,group in groups.items():
  omp=[r for r in group if r['configuration']!='sequential']
  ax.plot([int(r['actual_threads']) for r in omp],[float(r[key])*multiplier for r in omp],'-o',label=f'{m:,} meters',color=colors[m],markersize=4)
 if ideal=='speedup':ax.plot([1,2,4,8,12,16],[1,2,4,8,12,16],'--',color='#888888',label='Ideal linear scaling',linewidth=1)
 if ideal=='efficiency':ax.axhline(100,ls='--',color='#888888',linewidth=1)
 ax.set_xticks([1,2,4,8,12,16]);ax.set_xlabel('OpenMP threads');ax.set_ylabel(ylabel);ax.grid(axis='y',alpha=.2);ax.legend(frameon=False)

fig,axes=plt.subplots(1,3,figsize=(13.5,4))
for ax,(m,group) in zip(axes,groups.items()):
 omp=group[1:];x=[int(r['actual_threads']) for r in omp]
 med=[float(r['median_seconds_per_pass'])*1000 for r in omp]
 low=[a-float(r['q1_seconds'])*1000 for a,r in zip(med,omp)]
 high=[float(r['q3_seconds'])*1000-a for a,r in zip(med,omp)]
 ax.errorbar(x,med,yerr=[low,high],fmt='-o',color=colors[m],capsize=3,label='OpenMP median and Q1–Q3')
 b=group[0];ax.axhline(float(b['median_seconds_per_pass'])*1000,color='#555555',ls='--',label='Sequential median')
 ax.axhspan(float(b['q1_seconds'])*1000,float(b['q3_seconds'])*1000,color='#777777',alpha=.12,label='Sequential Q1–Q3')
 ax.set_title(f'{m:,} meters');ax.set_xlabel('OpenMP threads');ax.set_xticks([1,2,4,8,12,16]);ax.set_ylabel('Compute time per pass (ms)');ax.grid(axis='y',alpha=.2)
axes[0].legend(frameon=False,fontsize=8)
fig.suptitle('Compute time on the Windows laptop',y=1.04)
fig.text(.5,-.04,'10 trials per configuration; fit + evaluation + statistics. CSV I/O excluded. Shading/error bars show quartiles, not confidence intervals.',ha='center',fontsize=9)
fig.tight_layout();save(fig,'01_runtime_with_quartiles')
for name,key,mult,label,title,ideal in [
 ('02_speedup','speedup_vs_sequential',1,'Speedup relative to direct sequential','Measured speedup','speedup'),
 ('03_efficiency','efficiency_fraction',100,'Parallel efficiency (%)','Efficiency versus thread count','efficiency'),
 ('04_throughput','median_forecasts_per_second',1e-6,'Evaluated forecasts / second (millions)','Combined fit-and-evaluate throughput',None),
 ('05_cpu_utilization','median_cpu_percent_of_exposed_logical_capacity',1,'Process CPU / 16 logical processors (%)','Median normalized CPU utilization',None)]:
 fig,ax=plt.subplots(figsize=(8,4.7));series(ax,key,mult,label,ideal);ax.set_title(title)
 fig.text(.5,-.02,'Synthetic meter streams; warm repeated workloads; medians of 10 trials. CPU accounting includes OpenMP runtime activity.',ha='center',fontsize=8)
 fig.tight_layout();save(fig,name)
fig,axes=plt.subplots(1,3,figsize=(14,4))
for ax,m in zip(axes,groups):
 raw=list(csv.DictReader((ROOT/f'meters_{m:04d}/results/benchmark_raw.csv').open()))
 configs=groups[m];data=[[float(r['seconds_per_pass'])*1000 for r in raw if r['configuration']==c['configuration']] for c in configs]
 labels=['Seq','1','2','4','8','12','16']
 ax.boxplot(data,tick_labels=labels,showfliers=False,widths=.5)
 for i,values in enumerate(data,1):ax.scatter([i+(j-4.5)*.025 for j in range(len(values))],values,s=14,color=colors[m],alpha=.7,zorder=3)
 ax.set_title(f'{m:,} meters');ax.set_ylabel('Compute time per pass (ms)');ax.set_xlabel('Sequential / OpenMP threads');ax.grid(axis='y',alpha=.2)
fig.suptitle('All measured trials retained',y=1.03)
fig.text(.5,-.04,'Each dot is one measured trial; boxes show the middle 50%. No slow trial was removed.',ha='center',fontsize=9)
fig.tight_layout();save(fig,'06_raw_trial_distributions')
fig,axes=plt.subplots(2,2,figsize=(12,8))
series(axes[0,0],'speedup_vs_sequential',1,'Speedup');axes[0,0].set_title('Speedup grows with workload')
series(axes[0,1],'efficiency_fraction',100,'Efficiency (%)');axes[0,1].set_title('Efficiency depends on thread count')
series(axes[1,0],'median_forecasts_per_second',1e-6,'Evaluated forecasts/s (millions)');axes[1,0].set_title('Fit-and-evaluate throughput')
series(axes[1,1],'median_cpu_percent_of_exposed_logical_capacity',1,'CPU / 16 logical processors (%)');axes[1,1].set_title('Normalized process CPU utilization')
fig.suptitle('Smart grid OpenMP results on your laptop',fontsize=16)
fig.text(.5,.005,'64, 256 and 1,024 synthetic meters; 10 trials per configuration. Compute only. Memory bandwidth and physical grid control are not measured here.',ha='center',fontsize=9)
fig.tight_layout(rect=[0,.03,1,.96]);save(fig,'scaling_overview')
print('Created seven PNG and SVG figures from uploaded measurements.')
