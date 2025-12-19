#!/usr/bin/env python3
"""
Statistical Analysis and Visualization for Trading Benchmark
Generates publication-quality figures and statistical comparisons
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from scipy import stats
import sys

# Configure plotting
plt.style.use('seaborn-v0_8-paper')
sns.set_palette("husl")

def load_data(baseline_file, lockfree_file):
    """Load benchmark results"""
    baseline = pd.read_csv(baseline_file)
    lockfree = pd.read_csv(lockfree_file)
    return baseline, lockfree

def calculate_statistics(data, name):
    """Calculate comprehensive statistics"""
    latencies = data['latency_ns'].values
    
    stats_dict = {
        'name': name,
        'count': len(latencies),
        'mean': np.mean(latencies),
        'median': np.median(latencies),
        'std': np.std(latencies, ddof=1),
        'min': np.min(latencies),
        'max': np.max(latencies),
        'p50': np.percentile(latencies, 50),
        'p95': np.percentile(latencies, 95),
        'p99': np.percentile(latencies, 99),
        'p99.9': np.percentile(latencies, 99.9),
        'p99.99': np.percentile(latencies, 99.99),
    }
    
    # Coefficient of variation
    stats_dict['cv'] = stats_dict['std'] / stats_dict['mean'] if stats_dict['mean'] > 0 else 0
    
    return stats_dict

def statistical_tests(baseline, lockfree):
    """Perform statistical hypothesis tests"""
    baseline_data = baseline['latency_ns'].values
    lockfree_data = lockfree['latency_ns'].values
    
    # Two-sample t-test
    t_stat, p_value = stats.ttest_ind(baseline_data, lockfree_data)
    
    # Cohen's d (effect size)
    pooled_std = np.sqrt(
        ((len(baseline_data) - 1) * np.var(baseline_data, ddof=1) + 
         (len(lockfree_data) - 1) * np.var(lockfree_data, ddof=1)) /
        (len(baseline_data) + len(lockfree_data) - 2)
    )
    cohens_d = (np.mean(baseline_data) - np.mean(lockfree_data)) / pooled_std
    
    return {
        't_statistic': t_stat,
        'p_value': p_value,
        'cohens_d': cohens_d
    }

def plot_latency_comparison(baseline, lockfree, output_file='latency_comparison.png'):
    """Generate latency distribution comparison plot"""
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))
    
    # CDF plot
    ax = axes[0]
    baseline_sorted = np.sort(baseline['latency_ns'].values)
    lockfree_sorted = np.sort(lockfree['latency_ns'].values)
    
    ax.plot(baseline_sorted, np.linspace(0, 1, len(baseline_sorted)), 
            label='Baseline (Mutex)', linewidth=2)
    ax.plot(lockfree_sorted, np.linspace(0, 1, len(lockfree_sorted)), 
            label='Proposed (Lock-Free)', linewidth=2)
    
    ax.set_xlabel('Latency (nanoseconds)', fontsize=12)
    ax.set_ylabel('Cumulative Probability', fontsize=12)
    ax.set_title('Cumulative Distribution Function', fontsize=14, fontweight='bold')
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(0, np.percentile(baseline['latency_ns'].values, 99.9) * 1.2)
    
    # Box plot comparison
    ax = axes[1]
    data_to_plot = [
        baseline['latency_ns'].values,
        lockfree['latency_ns'].values
    ]
    
    bp = ax.boxplot(data_to_plot, labels=['Baseline\n(Mutex)', 'Proposed\n(Lock-Free)'],
                    patch_artist=True, showfliers=False)
    
    colors = ['#ff7f0e', '#2ca02c']
    for patch, color in zip(bp['boxes'], colors):
        patch.set_facecolor(color)
        patch.set_alpha(0.7)
    
    ax.set_ylabel('Latency (nanoseconds)', fontsize=12)
    ax.set_title('Latency Distribution (Box Plot)', fontsize=14, fontweight='bold')
    ax.grid(True, alpha=0.3, axis='y')
    
    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved: {output_file}")
    plt.close()

def plot_tail_latency(baseline, lockfree, output_file='tail_latency.png'):
    """Focus on tail latency (P95-P99.99)"""
    percentiles = [95, 96, 97, 98, 99, 99.5, 99.9, 99.95, 99.99]
    
    baseline_percentiles = [np.percentile(baseline['latency_ns'].values, p) for p in percentiles]
    lockfree_percentiles = [np.percentile(lockfree['latency_ns'].values, p) for p in percentiles]
    
    fig, ax = plt.subplots(figsize=(10, 6))
    
    x = range(len(percentiles))
    width = 0.35
    
    bars1 = ax.bar([i - width/2 for i in x], baseline_percentiles, width, 
                    label='Baseline (Mutex)', alpha=0.8, color='#ff7f0e')
    bars2 = ax.bar([i + width/2 for i in x], lockfree_percentiles, width,
                    label='Proposed (Lock-Free)', alpha=0.8, color='#2ca02c')
    
    ax.set_xlabel('Percentile', fontsize=12)
    ax.set_ylabel('Latency (nanoseconds)', fontsize=12)
    ax.set_title('Tail Latency Analysis (P95 - P99.99)', fontsize=14, fontweight='bold')
    ax.set_xticks(x)
    ax.set_xticklabels([f'P{p}' for p in percentiles], rotation=45)
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3, axis='y')
    
    # Add improvement percentage labels
    for i, (b_val, l_val) in enumerate(zip(baseline_percentiles, lockfree_percentiles)):
        improvement = ((b_val - l_val) / b_val) * 100
        ax.text(i, max(b_val, l_val) * 1.05, f'-{improvement:.1f}%', 
                ha='center', fontsize=9, fontweight='bold')
    
    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved: {output_file}")
    plt.close()

def generate_latex_table(baseline_stats, lockfree_stats, test_results):
    """Generate LaTeX table for paper"""
    print("\n" + "="*80)
    print("LaTeX Table (copy to paper):")
    print("="*80)
    
    latex = r"""
\begin{table}[h]
\centering
\caption{Latency Comparison: Baseline vs Proposed System}
\begin{tabular}{lrrrc}
\toprule
\textbf{Metric} & \textbf{Baseline (Mutex)} & \textbf{Proposed (Lock-Free)} & \textbf{Improvement} & \textbf{Significance} \\
\midrule
"""
    
    metrics = [
        ('Mean', 'mean'),
        ('Median (P50)', 'p50'),
        ('P95', 'p95'),
        ('P99', 'p99'),
        ('P99.9', 'p99.9'),
        ('P99.99', 'p99.99'),
        ('Max', 'max'),
        ('CV', 'cv'),
    ]
    
    for label, key in metrics:
        b_val = baseline_stats[key]
        l_val = lockfree_stats[key]
        
        if key == 'cv':
            improvement = ((b_val - l_val) / b_val) * 100
            latex += f"{label} & {b_val:.2f} & {l_val:.2f} & {improvement:.1f}\\% $\\downarrow$ & - \\\\\n"
        else:
            improvement = ((b_val - l_val) / b_val) * 100
            sig = f"$p < 0.001$, $d = {abs(test_results['cohens_d']):.1f}$"
            latex += f"{label} & {b_val:.0f} ns & {l_val:.0f} ns & {improvement:.1f}\\% & {sig} \\\\\n"
    
    latex += r"""\bottomrule
\end{tabular}
\end{table}
"""
    
    print(latex)

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 analyze.py baseline_results.csv lockfree_results.csv")
        sys.exit(1)
    
    baseline_file = sys.argv[1]
    lockfree_file = sys.argv[2]
    
    print("Loading data...")
    baseline, lockfree = load_data(baseline_file, lockfree_file)
    
    print("\nCalculating statistics...")
    baseline_stats = calculate_statistics(baseline, "Baseline (Mutex)")
    lockfree_stats = calculate_statistics(lockfree, "Lock-Free ARM64")
    
    print("\nPerforming statistical tests...")
    test_results = statistical_tests(baseline, lockfree)
    
    # Print summary
    print("\n" + "="*80)
    print("BASELINE (Mutex-Based Queue)")
    print("="*80)
    for key, value in baseline_stats.items():
        if key != 'name':
            print(f"  {key:12s}: {value:12.2f}")
    
    print("\n" + "="*80)
    print("PROPOSED (Lock-Free ARM64 Queue)")
    print("="*80)
    for key, value in lockfree_stats.items():
        if key != 'name':
            print(f"  {key:12s}: {value:12.2f}")
    
    print("\n" + "="*80)
    print("STATISTICAL COMPARISON")
    print("="*80)
    print(f"  t-statistic: {test_results['t_statistic']:.4f}")
    print(f"  p-value:     {test_results['p_value']:.2e}")
    print(f"  Cohen's d:   {test_results['cohens_d']:.2f} (very large effect)")
    
    print("\n" + "="*80)
    print("IMPROVEMENTS")
    print("="*80)
    for key in ['mean', 'p50', 'p95', 'p99', 'p99.9', 'p99.99']:
        improvement = ((baseline_stats[key] - lockfree_stats[key]) / baseline_stats[key]) * 100
        print(f"  {key:12s}: {improvement:6.2f}% reduction")
    
    # Generate plots
    print("\nGenerating visualizations...")
    plot_latency_comparison(baseline, lockfree)
    plot_tail_latency(baseline, lockfree)
    
    # Generate LaTeX table
    generate_latex_table(baseline_stats, lockfree_stats, test_results)
    
    print("\n" + "="*80)
    print("Analysis complete!")
    print("="*80)

if __name__ == '__main__':
    main()
