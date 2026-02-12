# CTracker: A Heap Allocation and Fragmentation Monitor

`CTracker` is a lightweight memory tracking tool for C++ that monitors heap allocations and calculates memory fragmentation in real-time using a linked-list based registry.

## Features

* **Zero-Heap Administrative Nodes**: The tracker manages its own metadata using `malloc`/`free` to bypass the `new`/`delete` hooks, preventing infinite recursion.
* **Fragmentation Analysis**: Calculates a fragmentation index based on the "span" of current allocations versus active bytes.
* **Thread-Safe**: Uses mutex locks and `thread_local` guards to safely handle multi-threaded allocations.
* **Automatic Overloads**: Hooks into `operator new` and `operator delete` (including array versions) for transparent tracking.

## How it Works

The library overrides global allocation operators. When `new` is called, it adds a node to a global CTrackerMetrics (singleton). The nodes themselves are allocated via `std::malloc` so the tracker's own memory doesn't trigger another `new` call.

## Usage

Integrate the tracker into your build. It automatically intercepts all standard `new` and `delete` calls.

```cpp
#include <ctracker.hpp>

#define C_TRACKER 1 // Enable the tracker: overrides `new` and `delete`

// Access/Initialize the singleton tracker
auto tracker = CTrackerMetrics::GetTracker();

// Normal allocations are automatically tracked
float *data = new float[50];

// Query metrics at any time
std::printf("Fragmentation Index: %f\n", tracker->FragmentationIndex());
std::printf("Total Active Bytes: %zu\n", tracker->TotalAllocated());
std::printf("Largest Free Block: %zu bytes\n", tracker->FindLargestFreeBlock());

delete[] data;
```

## Metrics Interpretation

* **Fragmentation Index**:
    * **< 0.2**: Ideal. Memory is tightly packed.
    * **~ 0.5**: Sub-optimal. Significant gaps are appearing between allocations.
    * **> 0.8**: Critical. High memory fragmentation, which may lead to allocation failures even if total free memory is sufficient.

## Architecture

* **Dynamic Record Registry**: Uses a linked list to store allocation records.
* **Address-Sorted Order**: Maintains records in a sorted list by memory address to efficiently identify gaps and fragmentation.
* **Singleton**
* **Thread-safe Mutex**

## Requirements

* **OS**: Linux (recommended for best compatibility with `thread_local`)
* **Language**: C++11 or higher (C++20 recommended)
* **Compiler**: GCC or Clang with `-pthread` support.
