# Technical Report: DialSort vs QuickSort

ST0245 - SI001 Data Structures and Algorithms  
School of Applied Sciences and Engineering, EAFIT University  
Lecturer: Alexander Narvaez Berrio  
Practice II: Experimental Analysis of Algorithms and Data Structures  
Student: Sebastian Guerrero Cataño  
April 2026

## 1. Objective

The objective of this practice is to design, implement, and experimentally
analyze a benchmark comparing DialSort with an alternative sorting algorithm.
The implementation is contained in `PracticaII.cpp` and was developed entirely
in C++.

The selected alternative is QuickSort-3Way, an in-place comparison-based
algorithm. The comparison evaluates execution time, mean time, standard
deviation, throughput, estimated memory usage, and correctness across different
input sizes, universe sizes, and distributions.

An online simulation is available at:

https://dsortvsqsort.lovable.app

## 2. Implementation Approach

### DialSort-Counting

DialSort is a non-comparative algorithm designed for bounded integer keys. The
implementation in `PracticaII.cpp` first detects the real range of the input:

```cpp
mn = min(input)
mx = max(input)
U = mx - mn + 1
```

It then allocates a histogram of size `U` and counts each key at its direct
address:

```cpp
H[x - mn]++
```

Finally, it scans the histogram from left to right and writes the values back
into the original array. This means the algorithm does not compare elements to
determine their order; the value itself determines the histogram position.

### QuickSort-3Way

The alternative solution is a QuickSort variant implemented with:

- median-of-three pivot selection
- 3-way partitioning for repeated keys
- insertion sort for small partitions
- tail-recursion reduction to limit stack usage

This makes the algorithm more robust than a naive QuickSort, especially for
sorted inputs and inputs with many repeated values.

## 3. Experimental Design

The benchmark varies three dimensions:

| Dimension | Values |
|---|---|
| Input size `n` | selected by execution mode |
| Universe size `U` | `256`, `1024`, `65536` |
| Distribution | uniform, skewed, sorted, reverse |

Execution modes in `PracticaII.cpp`:

| Mode | Command | Input sizes |
|---|---|---|
| Fast verification | `./PracticaII --rapido` | `1000`, `10000`, `100000` |
| Standard benchmark | `./PracticaII` | `100000`, `500000`, `1000000` |
| Large benchmark | `./PracticaII --grande` | `100000`, `500000`, `1000000`, `5000000`, `10000000` |

The benchmark uses a fixed seed, `20260321`, so the same configurations are
reproducible. Each algorithm is warmed up for 3 discarded runs and then measured
using the best time from 7 measured runs. Correctness is verified after every
sort using `std::is_sorted`.

## 4. Complexity Analysis

| Algorithm | Best case | Average case | Worst case | Extra memory |
|---|---:|---:|---:|---:|
| DialSort-Counting | `O(n + U)` | `O(n + U)` | `O(n + U)` | `O(U)` |
| QuickSort-3Way | `O(n log n)` | `O(n log n)` | `O(n^2)` | `O(log n)` |

DialSort is linear in the number of records plus the universe size. Its main
limitation is memory usage: if `U` is much larger than `n`, the histogram becomes
expensive.

QuickSort is more general because it works for any comparable data type and does
not require a bounded integer universe. Its average case is efficient, but it is
still comparison-based and therefore normally requires `O(n log n)` comparisons.

## 5. Performance Measurements

The following measurements were obtained with:

```powershell
.\PracticaII.exe --rapido
```

Summary:

| Metric | Value |
|---|---:|
| Configurations measured | 12 |
| DialSort faster | 12 / 12 |
| QuickSort faster | 0 / 12 |
| Effective ties | 0 / 12 |
| Average DialSort / QuickSort ratio | `0.092x` |
| Best DialSort advantage | `23.699x` throughput |
| Correctness | all checks passed |

The ratio `0.092x` means that, on average, DialSort used about 9.2% of the
QuickSort execution time in the fast verification experiment.

## 6. Comparative Analysis

DialSort performed better in every measured fast-mode configuration. This result
is consistent with the theoretical model because the tested inputs are bounded
integers and the universe sizes are compact. In these cases, the cost of
building and scanning a histogram is lower than the cost of comparison-based
partitioning.

QuickSort-3Way remains important as a practical baseline. It is more general and
requires less auxiliary memory. Its 3-way partitioning also handles repeated
values efficiently, reducing the cost of datasets with many duplicates.

The main tradeoff is therefore:

- DialSort is faster when `U` is small enough to make the histogram efficient.
- QuickSort is more flexible when keys are not bounded integers or when `U` is
  too large for memory-efficient histogram allocation.

## 7. Visualization

The project includes two visualization paths:

1. Local C++ visualization:

```bash
./PracticaII --visualizar
```

2. Online simulation:

https://dsortvsqsort.lovable.app

The visualization shows how DialSort builds and scans a histogram, and how
QuickSort-3Way selects a pivot and partitions the input into smaller, equal, and
greater regions.

## 8. Conclusions

DialSort is the better choice for the benchmark conditions used in the verified
run because the input keys are integers and the universe sizes are bounded. Its
`O(n + U)` behavior allows it to outperform QuickSort when the histogram remains
compact.

QuickSort-3Way is the better general-purpose option. It does not require
integer-only keys or a bounded universe, and its memory usage is lower for very
large key ranges.

The experiment shows that algorithm selection depends on the structure of the
data. Big O notation explains the trend, but practical factors such as cache
locality, memory allocation, branch prediction, and repeated values also affect
the measured results.

## 9. Reproducibility

Build:

```bash
g++ -O3 -std=c++17 -o PracticaII PracticaII.cpp
```

Run fast verification:

```bash
./PracticaII --rapido
```

Run the mandatory large dataset range:

```bash
./PracticaII --grande
```

Run visualization:

```bash
./PracticaII --visualizar
```

Experimental constants:

- source file: `PracticaII.cpp`
- language standard: C++17
- seed: `20260321`
- warmup rounds: 3
- measured rounds: 7
- timer: `std::chrono::high_resolution_clock`
- correctness check: `std::is_sorted`
