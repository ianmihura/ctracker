#include <iostream>
#include <memory_resource>

struct Tracker {
    int Id;
    char Buffer[16];
    Tracker(int i) : Id(i) {}
};

void Leak() {
    // Leak 1: We allocate a struct and never deallocate it.
    Tracker* Lost = new Tracker(42);

    // Leak 2: We buffer overflow: writing 1 char past the end of the array.
    for (int i = 0; i <= 16; ++i) {
        Lost->Buffer[i] = 'a'; 
    }

    std::cout << "Leaked object ID: " << Lost->Id << std::endl;

    // To avoid the first leak, you must manually destroy and deallocate
    // delete Lost;
};

class PrintingResource : public std::pmr::memory_resource {
protected:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        std::cout << "Allocating: " << bytes << " bytes\n";
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        std::cout << "Freeing: " << bytes << " bytes\n";
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

void PMRLeak() {
    PrintingResource res;
    std::pmr::polymorphic_allocator<Tracker> alloc(&res);

    // This replaces 'new Tracker(42)'
    Tracker* t = alloc.allocate(1);      // Reserves memory
    alloc.construct(t, 42);             // Calls the constructor

    std::cout << "Tracker ID: " << t->Id << "\n";

    // To avoid the leak, you must manually destroy and deallocate
    // alloc.destroy(t);
    // alloc.deallocate(t, 1);
}

int main() {
    Leak();
    PMRLeak();

    return 0;
}
