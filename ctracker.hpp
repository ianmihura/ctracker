#ifndef C_TRACKER_HPP
#define C_TRACKER_HPP

#ifndef C_TRACKER
#define C_TRACKER 0
#endif

#if C_TRACKER

#include <iostream>
#include <atomic>
#include <cstdlib>
#include <mutex>

// TODO tests

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
    AllocationRecord *RecordsHead;
    AllocationRecord *RecordsTail;
    size_t RecordCount = 0;

    ~CTrackerMetrics()
    {
        AllocationRecord *current = RecordsHead;
        while (current)
        {
            AllocationRecord *next = current->next;
            std::free(current);
            current = next;
        }
    }

    CTrackerMetrics(const CTrackerMetrics &) = delete; // not clonable
    void operator=(const CTrackerMetrics &) = delete;  // not assignable

    static CTrackerMetrics *GetTracker();

    void CmallocTrack(void *ptr, size_t size)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // We use malloc here to avoid calling our own `operator new`
        AllocationRecord *newRecord = static_cast<AllocationRecord *>(std::malloc(sizeof(AllocationRecord)));
        if (!newRecord)
        {
            return;
        }

        newRecord->ptr = ptr;
        newRecord->size = size;
        newRecord->next = nullptr;

        // Maintain sorted order by address for easier fragmentation analysis
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);

        if (!RecordsHead || reinterpret_cast<uintptr_t>(RecordsHead->ptr) > addr)
        {
            newRecord->next = RecordsHead;
            RecordsHead = newRecord;
            if (!RecordsTail)
            {
                RecordsTail = newRecord;
            }
        }
        else
        {
            AllocationRecord *current = RecordsHead;
            while (current->next && reinterpret_cast<uintptr_t>(current->next->ptr) < addr)
            {
                current = current->next;
            }
            newRecord->next = current->next;
            current->next = newRecord;
            if (!newRecord->next)
            {
                RecordsTail = newRecord;
            }
        }
    }

    void CfreeTrack(void *ptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        AllocationRecord *current = RecordsHead;
        AllocationRecord *prev = nullptr;

        while (current)
        {
            if (current->ptr == ptr)
            {
                if (prev)
                {
                    prev->next = current->next;
                }
                else
                {
                    RecordsHead = current->next;
                }

                if (current == RecordsTail)
                {
                    RecordsTail = prev;
                }

                std::free(current); // Free the record node
                return;
            }
            prev = current;
            current = current->next;
        }
    }

    // Total size allocated to the heap
    size_t TotalAllocated()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t active_bytes = 0;
        AllocationRecord *current = RecordsHead;
        while (current)
        {
            active_bytes += current->size;
            current = current->next;
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
        if (!RecordsHead || (RecordsHead == RecordsTail))
        { // record count < 2
            return 0.0f;
        }

        uintptr_t start = reinterpret_cast<uintptr_t>(RecordsHead->ptr);
        uintptr_t end = reinterpret_cast<uintptr_t>(RecordsTail->ptr) + RecordsTail->size;

        size_t span = end - start;
        if (span == 0)
        {
            return 0.0f;
        }

        size_t active_bytes = 0;
        AllocationRecord *current = RecordsHead;
        while (current)
        {
            active_bytes += current->size;
            current = current->next;
        }
        float index = 1.0f - (static_cast<float>(active_bytes) / span);
        return index;
    }

    size_t FindLargestFreeBlock()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t largest_gap = 0;

        if (!RecordsHead)
        {
            return 0;
        }

        AllocationRecord *current = RecordsHead;
        while (current && current->next)
        {
            uintptr_t current_end = reinterpret_cast<uintptr_t>(current->ptr) + current->size;
            uintptr_t next_start = reinterpret_cast<uintptr_t>(current->next->ptr);

            if (next_start > current_end)
            {
                size_t gap = next_start - current_end;
                if (gap > largest_gap)
                {
                    largest_gap = gap;
                }
            }
            current = current->next;
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
    std::printf("a: %p, e: %p\n", (void *)a, (void *)e);

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

#endif
#endif
