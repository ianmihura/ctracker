# Project 7: Memory Allocation Tracker
Build a minimal utility to track memory allocations within a small, defined scope. Report total bytes allocated and number of allocations/deallocations.

Operator Overloading (new and delete), Custom Allocators, Global Hooks.

Override the global operator new and operator delete functions. This is a common advanced C++ technique used in low-latency systems. Inside the overloaded functions, you can safely track the size of the memory requested before calling the actual underlying system allocator. Be extremely careful not to use std::cout or other dynamic functions inside the overloaded operators to avoid re-entry/deadlocks."

### Mid-Term Scope Change
Implement a simple custom memory pool (e.g., for objects of a fixed small size)
and integrate it into a class using a custom allocator template.

Custom Allocators, Template Classes.

