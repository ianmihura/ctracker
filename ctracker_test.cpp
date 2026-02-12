#include <gtest/gtest.h>
#include "ctracker.hpp"

// #define C_TRACKER_VERBOSE 1

// The singleton tracks ALL allocations globally (including gtest internals),
// so tests measure deltas from a baseline snapshot rather than absolute values.

// Helper: snapshot the current tracker state before test-controlled allocations
struct TrackerSnapshot
{
    size_t record_count;
    size_t total_allocated;
};

static TrackerSnapshot TakeSnapshot()
{
    auto *t = CTrackerMetrics::GetTracker();
    return {t->RecordCount, t->TotalAllocated()};
}

// --- Allocation Tracking ---

TEST(CTrackerTest, TrackSingleAllocation)
{
    auto before = TakeSnapshot();

    int *p = new int[10]; // 40 bytes

    auto *t = CTrackerMetrics::GetTracker();
    EXPECT_EQ(t->RecordCount, before.record_count + 1);
    EXPECT_EQ(t->TotalAllocated(), before.total_allocated + sizeof(int) * 10);

    delete[] p;

    EXPECT_EQ(t->RecordCount, before.record_count);
    EXPECT_EQ(t->TotalAllocated(), before.total_allocated);
}

TEST(CTrackerTest, TrackMultipleAllocations)
{
    auto before = TakeSnapshot();

    float *a = new float[5];   // 20 bytes
    char *b = new char[100];   // 100 bytes
    double *c = new double[8]; // 64 bytes

    auto *t = CTrackerMetrics::GetTracker();
    size_t expected_size = sizeof(float) * 5 + sizeof(char) * 100 + sizeof(double) * 8;
    EXPECT_EQ(t->RecordCount, before.record_count + 3);
    EXPECT_EQ(t->TotalAllocated(), before.total_allocated + expected_size);

    delete[] a;
    delete[] b;
    delete[] c;

    EXPECT_EQ(t->RecordCount, before.record_count);
    EXPECT_EQ(t->TotalAllocated(), before.total_allocated);
}

// --- Free Tracking ---

TEST(CTrackerTest, FreeMiddleAllocation)
{
    auto before = TakeSnapshot();

    int *a = new int[1];
    int *b = new int[1];
    int *c = new int[1];

    auto *t = CTrackerMetrics::GetTracker();
    EXPECT_EQ(t->RecordCount, before.record_count + 3);

    // Free the middle one — record count should drop by 1
    delete[] b;
    EXPECT_EQ(t->RecordCount, before.record_count + 2);
    EXPECT_EQ(t->TotalAllocated(), before.total_allocated + sizeof(int) * 2);

    delete[] a;
    delete[] c;

    EXPECT_EQ(t->RecordCount, before.record_count);
}

TEST(CTrackerTest, FreeUnknownPointerIsNoOp)
{
    auto before = TakeSnapshot();

    // Allocate via malloc (bypasses our operator new), then try to free-track it.
    // CfreeTrack should silently do nothing.
    void *raw = std::malloc(64);
    CTrackerMetrics::GetTracker()->CfreeTrack(raw);

    EXPECT_EQ(CTrackerMetrics::GetTracker()->RecordCount, before.record_count);
    std::free(raw);
}

// --- Fragmentation ---

TEST(CTrackerTest, FragmentationIncreasesAfterFreeingMiddle)
{
    // Allocate three contiguous-ish blocks, measure fragmentation,
    // free the middle one, and verify fragmentation increases.
    int *a = new int[100];
    int *b = new int[100];
    int *c = new int[100];
    a[4] = 4;
    b[4] = 4;
    c[4] = 4;

    auto *t = CTrackerMetrics::GetTracker();
    float frag_before = t->FragmentationIndex();

    delete[] b; // Creates a gap between a and c

    float frag_after = t->FragmentationIndex();
    EXPECT_GT(frag_after, frag_before);

    delete[] a;
    delete[] c;
}

// --- Largest Free Block ---

TEST(CTrackerTest, LargestFreeBlockPositiveAfterFree)
{
    // Allocate 3 blocks; freeing the middle should produce a gap.
    // We can't predict absolute gap sizes because gtest's own allocations interleave,
    // but the largest gap should be at least as big as our created gap.
    int *a = new int[10];
    int *b = new int[200];
    int *c = new int[10];
    a[4] = 4;
    b[4] = 4;
    c[4] = 4;

    auto *t = CTrackerMetrics::GetTracker();

    delete[] b; // Creates a gap between a and c

    size_t gap = t->FindLargestFreeBlock();
    EXPECT_GE(gap, sizeof(int) * 200);

    delete[] a;
    delete[] c;
}

// --- Sorted Order ---

TEST(CTrackerTest, RecordsAreSortedByAddress)
{
    int *a = new int[10];
    int *b = new int[10];
    int *c = new int[10];

    a[4] = 4;
    b[4] = 4;
    c[4] = 4;

    auto *t = CTrackerMetrics::GetTracker();

    // Walk the linked list and verify addresses are in ascending order
    AllocationRecord *prev = nullptr;
    AllocationRecord *cur = t->RecordsHead;
    while (cur)
    {
        if (prev)
        {
            EXPECT_LT(reinterpret_cast<uintptr_t>(prev->ptr),
                      reinterpret_cast<uintptr_t>(cur->ptr));
        }
        prev = cur;
        cur = cur->next;
    }

    delete[] a;
    delete[] b;
    delete[] c;
}

// --- TotalAllocated ---

TEST(CTrackerTest, TotalAllocatedMatchesKnownSizes)
{
    auto before = TakeSnapshot();

    char *x = new char[256];
    double *y = new double[32];

    size_t expected_delta = 256 + sizeof(double) * 32;
    EXPECT_EQ(CTrackerMetrics::GetTracker()->TotalAllocated(),
              before.total_allocated + expected_delta);

    delete[] x;
    delete[] y;
}
