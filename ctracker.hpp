#ifndef C_TRACKER_HPP
#define C_TRACKER_HPP

#ifndef C_TRACKER
#define C_TRACKER 1
#endif
#ifndef C_TRACKER_VERBOSE
#define C_TRACKER_VERBOSE 0
#endif

#if C_TRACKER

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
        RecordCount++;
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
                RecordCount--;
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
        #if C_TRACKER_VERBOSE
        printf("`new` called with size %zu -> %p\n", size, ptr);
        #endif
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
        #if C_TRACKER_VERBOSE
        printf("`new[]` called with size %zu -> %p\n", size, ptr);
        #endif
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
        #if C_TRACKER_VERBOSE
        printf("`delete` called for %p\n", ptr);
        #endif
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
        #if C_TRACKER_VERBOSE
        printf("`delete[]` called for %p\n", ptr);
        #endif
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
        #if C_TRACKER_VERBOSE
        printf("`delete` called with size %zu for %p\n", size, ptr);
        #endif
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
        #if C_TRACKER_VERBOSE
        printf("`delete[]` called with size %zu for %p\n", size, ptr);
        #endif
        CTrackerMetrics::GetTracker()->CfreeTrack(ptr);
        lock_tracker = false;
    }

    std::free(ptr);
}

#endif
#endif
