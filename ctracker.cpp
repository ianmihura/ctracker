#include <iostream>
#include <atomic>
#include <cstdlib>
#include <mutex>

struct AllocationRecord
{
    void *ptr;
    size_t size;

    AllocationRecord *next;
};

class CTrackerMetrics
{
protected:
    CTrackerMetrics() : RecordsHead(nullptr), RecordsTail(nullptr) {}

    mutable std::mutex mutex_;

public:
    static constexpr size_t MAX_RECORDS = 10000; // TODO do we need this?
    AllocationRecord Records[MAX_RECORDS];       // TODO use a link list
    AllocationRecord *RecordsHead;
    AllocationRecord *RecordsTail;
    size_t RecordCount = 0;

    CTrackerMetrics(const CTrackerMetrics &) = delete; // not clonable
    void operator=(const CTrackerMetrics &) = delete; // not assignable

    static CTrackerMetrics *GetTracker();

    void CmallocTrack(void *ptr, size_t size)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (RecordCount < CTrackerMetrics::MAX_RECORDS)
        {
            // insertion sort by address
            uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
            size_t i = RecordCount;
            while (i > 0 && reinterpret_cast<uintptr_t>(Records[i - 1].ptr) > addr)
            {
                Records[i] = Records[i - 1];
                i--;
            }
            Records[i] = {ptr, size};
            RecordCount++;
        }
    }

    void CfreeTrack(void *ptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < RecordCount; ++i)
        {
            if (Records[i].ptr == ptr)
            {
                // found
                for (size_t j = i; j < RecordCount - 1; ++j)
                {
                    Records[j] = Records[j + 1];
                }
                RecordCount--;
                break;
            }
        }
    }

    // Total size allocated to the heap
    size_t TotalAllocated()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t active_bytes = 0;
        for (size_t i = 0; i < RecordCount; ++i)
        {
            active_bytes += Records[i].size;
        }
        return active_bytes;
    }

    // Absolute index betweenn 0-1.
    //    - Below 0.2 is generally ok
    //    - Around 0.5 is generally not ok
    //    - Above 0.8 is extremely not ok
    float FragmentationIndex()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (RecordCount < 2)
        {
            return 0.0f;
        }

        uintptr_t start = reinterpret_cast<uintptr_t>(Records[0].ptr);
        uintptr_t end = reinterpret_cast<uintptr_t>(Records[RecordCount - 1].ptr) + Records[RecordCount - 1].size;

        size_t span = end - start;
        if (span == 0)
        {
            return 0.0f;
        }

        size_t active_bytes = 0;
        for (size_t i = 0; i < RecordCount; ++i)
        {
            active_bytes += Records[i].size;
        }
        float index = 1.0f - (static_cast<float>(active_bytes) / span);
        return index;
    }

    size_t FindLargestFreeBlock()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t largest_gap = 0;

        for (size_t i = 0; i < RecordCount - 1; ++i)
        {
            uintptr_t current_end = reinterpret_cast<uintptr_t>(Records[i].ptr) + Records[i].size;
            uintptr_t next_start = reinterpret_cast<uintptr_t>(Records[i + 1].ptr);

            size_t gap = next_start - current_end;
            if (gap > largest_gap)
            {
                largest_gap = gap;
            }
        }
        return largest_gap;
    }
};

CTrackerMetrics *CTrackerMetrics::GetTracker()
{
    static CTrackerMetrics instance;
    return &instance;
}

static thread_local bool lock_tracker = false;

void *operator new(size_t size)
{
    void *ptr = std::malloc(size);

    if (!lock_tracker)
    {
        lock_tracker = true;
        printf("`new` called with size %zu -> %p\n", size, ptr);
        CTrackerMetrics::GetTracker()->CmallocTrack(ptr, size);
        lock_tracker = false;
    }

    return ptr;
}

void *operator new[](size_t size)
{
    void *ptr = std::malloc(size);

    if (!lock_tracker)
    {
        lock_tracker = true;
        printf("`new[]` called with size %zu -> %p\n", size, ptr);
        CTrackerMetrics::GetTracker()->CmallocTrack(ptr, size);
        lock_tracker = false;
    }

    return ptr;
}

void operator delete(void *ptr) noexcept
{
    if (!ptr)
    {
        return;
    }

    if (!lock_tracker)
    {
        lock_tracker = true;
        printf("`delete` called for %p\n", ptr);
        CTrackerMetrics::GetTracker()->CfreeTrack(ptr);
        lock_tracker = false;
    }

    std::free(ptr);
}

void operator delete[](void *ptr) noexcept
{
    if (!ptr)
    {
        return;
    }

    if (!lock_tracker)
    {
        lock_tracker = true;
        printf("`delete[]` called for %p\n", ptr);
        CTrackerMetrics::GetTracker()->CfreeTrack(ptr);
        lock_tracker = false;
    }

    std::free(ptr);
}

void operator delete(void *ptr, size_t size) noexcept
{
    if (!ptr)
    {
        return;
    }

    if (!lock_tracker)
    {
        lock_tracker = true;
        printf("`delete` called with size %zu for %p\n", size, ptr);
        CTrackerMetrics::GetTracker()->CfreeTrack(ptr);
        lock_tracker = false;
    }

    std::free(ptr);
}

void operator delete[](void *ptr, size_t size) noexcept
{
    if (!ptr)
    {
        return;
    }

    if (!lock_tracker)
    {
        lock_tracker = true;
        printf("`delete[]` called with size %zu for %p\n", size, ptr);
        CTrackerMetrics::GetTracker()->CfreeTrack(ptr);
        lock_tracker = false;
    }

    std::free(ptr);
}

int main()
{
    float *a = new float[1];
    float *b = new float[1];
    int *arr = new int[30];
    char *cs = new char[30];
    float *c = new float[1];
    for (int i = 0; i < 30; i++)
    {
        arr[i] = i * 2;
        cs[i] = i + 4;
        c[0] = 1;
    }
    std::printf("arr[7]: %d\n", arr[7]);
    std::printf("cs[12]: %d\n", cs[12]);
    std::printf("cs[19]: %d\n", cs[19]);
    std::printf("Fragmentation index: %f\n", CTrackerMetrics::GetTracker()->FragmentationIndex());
    float *tet = new float[30];
    float *d = new float[50];
    d[5] = 1;
    printf("d[5]: %f\n", d[5]);
    for (int i = 0; i < 30; i++)
    {
        tet[i] = static_cast<float>(i) / 3;
    }
    float *e = new float[1];
    std::printf("tet[1]: %f\n", tet[1]);
    std::printf("tet[19]: %f\n", tet[19]);
    std::printf("tet[29]: %f\n", tet[29]);

    // Use a and e to avoid unused warnings
    std::printf("a: %p, e: %p\n", (void*)a, (void*)e);

    std::printf("Fragmentation index: %f\n", CTrackerMetrics::GetTracker()->FragmentationIndex());

    delete[] cs;
    delete[] b;

    std::printf("Fragmentation index: %f\n", CTrackerMetrics::GetTracker()->FragmentationIndex());
    std::printf("Largest block: %zu bytes\n", CTrackerMetrics::GetTracker()->FindLargestFreeBlock());
    std::printf("Total alloc: %zu bytes\n", CTrackerMetrics::GetTracker()->TotalAllocated());

    // Cleanup rest
    delete[] a;
    delete[] arr;
    delete[] c;
    delete[] tet;
    delete[] d;
    delete[] e;

    return 0;
}
