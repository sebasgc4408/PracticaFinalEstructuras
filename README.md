# Practice II: DialSort vs QuickSort

ST0245 - SI001 Data Structures and Algorithms  
School of Applied Sciences and Engineering, EAFIT University  
Lecturer: Alexander Narvaez Berrio  
April 2026

## Student

Delivered by student Sebastian Guerrero Cataño.

## Objective

This project implements and analyzes a C++ benchmark comparing DialSort with an
alternative sorting algorithm proposed for the practice.

The entire practical implementation is contained in:

```text
PracticaII.cpp
```

The selected alternative algorithm is QuickSort-3Way.

## Online Simulation

Interactive simulation:

https://dsortvsqsort.lovable.app

The C++ program also includes a local visualization mode:

```bash
./PracticaII --visualizar
```

## Algorithms Implemented

### DialSort-Counting

DialSort is a non-comparative sorting algorithm for bounded integer keys. In
`PracticaII.cpp`, DialSort works as follows:

1. Detect the real input range:

```cpp
mn = min(input)
mx = max(input)
U = mx - mn + 1
```

2. Build a histogram:

```cpp
H[x - mn]++
```

3. Scan the histogram from left to right and write the values back into the
array.

Complexity:

- Best case: `O(n + U)`
- Average case: `O(n + U)`
- Worst case: `O(n + U)`
- Extra memory: `O(U)`

### QuickSort-3Way

The alternative algorithm is an in-place QuickSort implementation with:

- median-of-three pivot selection
- 3-way partitioning: values smaller than, equal to, and greater than the pivot
- insertion sort for small partitions
- tail-recursion reduction

Complexity:

- Best case: `O(n log n)`
- Average case: `O(n log n)`
- Worst case: `O(n^2)`
- Extra memory: `O(log n)`

## Benchmark Design

`PracticaII.cpp` varies three experimental dimensions:

| Dimension | Values |
|---|---|
| Input size `n` | depends on selected mode |
| Universe size `U` | `256`, `1024`, `65536` |
| Distribution | uniform, skewed, sorted, reverse |

The program measures:

- best execution time
- mean execution time
- standard deviation
- throughput in million keys per second
- estimated memory usage
- correctness of the sorted output

## Build Instructions

Compile with a C++17 compiler. Use optimization flags for benchmark runs.

```bash
g++ -O3 -std=c++17 -o PracticaII PracticaII.cpp
```

On Windows PowerShell:

```powershell
g++ -O3 -std=c++17 -o PracticaII.exe PracticaII.cpp
```

If `g++` is not available on `PATH` but CLion MinGW is installed:

```powershell
$env:PATH='C:\Program Files\JetBrains\CLion 2025.3.4\bin\mingw\bin;' + $env:PATH
g++ -O3 -std=c++17 -o PracticaII.exe PracticaII.cpp
```

## Execution Instructions

### Fast Verification

```bash
./PracticaII --rapido
```

PowerShell:

```powershell
.\PracticaII.exe --rapido
```

This mode uses smaller inputs and is intended for quick testing.

### Standard Benchmark

```bash
./PracticaII
```

Uses:

- `n = 100000`, `500000`, `1000000`
- `U = 256`, `1024`, `65536`
- distributions: uniform, skewed, sorted, reverse

### Large Benchmark

```bash
./PracticaII --grande
```

Uses the mandatory practice range:

- `n = 100000`, `500000`, `1000000`, `5000000`, `10000000`
- `U = 256`, `1024`, `65536`
- distributions: uniform, skewed, sorted, reverse

### CSV Output

```bash
./PracticaII --csv
```

### Visualization

```bash
./PracticaII --visualizar
```

This shows the internal behavior of both DialSort and QuickSort-3Way.

## Verified Result

The following result was obtained by running:

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

Interpretation: in the fast verification run, DialSort outperformed QuickSort
for every tested configuration. This is expected because the tested inputs use
bounded integer keys and relatively compact universes, which match DialSort's
intended use case.

## Conclusion

DialSort is most effective when:

- keys are integers
- the key universe is bounded
- `U` is small compared with or close to `n`
- repeated values are common

QuickSort-3Way is more general and uses less auxiliary memory. It is preferable
when the data is not limited to bounded integer keys or when the universe size is
too large for a histogram to be memory-efficient.

## Reproducibility

- Source file: `PracticaII.cpp`
- Language standard: C++17
- Seed: `20260321`
- Timing: `std::chrono::high_resolution_clock`
- Warmup rounds: 3
- Measurement rounds: best of 7
- Correctness check: `std::is_sorted`
