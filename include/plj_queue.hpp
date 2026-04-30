#pragma once
#include <atomic>
#include <cstdint>

template <typename T>
class PLJQueue {
public:
    // Markers to replace the 'mark' bit used in the count struct
    static constexpr uint32_t ENQ = 0;
    static constexpr uint32_t DEQ = 1;

    struct Object; // Forward declaration

    // Represents the 'count' half of the double-size variable
    struct Count {
        uint32_t counter;
        uint32_t mark; // Used to indicate whether the object is going to be dequeued

        bool operator==(const Count& other) const {
            return counter == other.counter && mark == other.mark;
        }
    };

    // Structure Pointer
    struct Pointer {
        Object* ptr{nullptr};
        Count count{0};

        bool operator==(const Pointer& other) const {
            return ptr == other.ptr && count == other.count;
        }
    };

    // Structure Object
    struct Object {
        T* data; // Updated to hold T* to match the test suite
        std::atomic<Pointer> nextobject;
    };

private:
    // Shared Variables
    alignas(64) std::atomic<Pointer> Shared_head;
    alignas(64) std::atomic<Pointer> Shared_tail;

    // Helper method to deduce the state based on Figure 2
    int DetermineState(const Pointer& Private_head, const Pointer& Private_tail, const Pointer& Next) {
        if (Private_head.ptr != nullptr && Private_head.ptr == Private_tail.ptr) {
            if (Next.ptr == nullptr) {
                if (Next.count.mark == DEQ) return 4;
                return 3;
            }
            return 5;
        }
        if (Private_head.ptr != nullptr && Private_tail.ptr != nullptr && Private_head.ptr != Private_tail.ptr) {
            if (Next.ptr == nullptr) return 1;
            return 2;
        }
        if (Private_head.ptr == nullptr && Private_tail.ptr == nullptr) return 7;
        if (Private_head.ptr != nullptr && Private_tail.ptr == nullptr) return 6;
        if (Private_head.ptr == nullptr && Private_tail.ptr != nullptr) return 8;
        return 0; // Unknown/Transitional
    }

    // Procedure Snapshot
    void Snapshot(Pointer& Private_head, Pointer& Private_tail, Pointer& Next) {
        Pointer firsthead, firsttail;
        do {
            firsthead = Shared_head.load();
            do {
                firsttail = Shared_tail.load();
                if (firsttail.ptr != nullptr) {
                    Next = firsttail.ptr->nextobject.load();
                }
                Private_tail = Shared_tail.load();
            } while (!(Private_tail == firsttail));
            Private_head = Shared_head.load();
        } while (!(Private_head == firsthead));
    }

public:
    PLJQueue() {
        Pointer null_ptr{nullptr, {0, ENQ}};
        Shared_head.store(null_ptr);
        Shared_tail.store(null_ptr);
    }

    // Procedure Enqueue (Modified to take T* and create the Object internally)
    void enqueue(T* item) {
        // Initialize object, leave count untouched except for marking
        Object* objectptr = new Object();
        objectptr->data = item;
        objectptr->nextobject.store({nullptr, {0, ENQ}});
        
        bool success = false;

        do {
            // TODO: Need to initialize Next as it may not get filled??
            Pointer Private_head, Private_tail, Next;
            Snapshot(Private_head, Private_tail, Next);
            int State = DetermineState(Private_head, Private_tail, Next);

            switch (State) {
                case 1:
                case 3: { // Queue is in a correct state, so enqueue object
                    Pointer expected = Next;
                    Pointer new_val{objectptr, {Next.count.counter + 1, ENQ}};
                    // CSDBL to append to nextobject
                    if (Private_tail.ptr->nextobject.compare_exchange_strong(expected, new_val)) {
                        // Attempt was successful, so shift the tail to the object just added
                        // TODO: Can fallthrough
                        Pointer exp_tail = Private_tail;
                        Pointer new_tail{objectptr, {Private_tail.count.counter + 1, Private_tail.count.mark}};
                        Shared_tail.compare_exchange_strong(exp_tail, new_tail);
                        success = true;
                    }
                    break;
                }
                case 2:
                case 5: { // Finish incomplete enqueue operation
                    Pointer exp_tail = Private_tail;
                    Pointer new_tail{Next.ptr, {Private_tail.count.counter + 1, Private_tail.count.mark}};
                    Shared_tail.compare_exchange_strong(exp_tail, new_tail);
                    break;
                }
                case 4: { // Cooperate in dequeuing the object
                    // Shift the tail to NULL
                    Pointer exp_tail = Private_tail;
                    Pointer new_tail{nullptr, {Private_tail.count.counter + 1, Private_tail.count.mark}};
                    Shared_tail.compare_exchange_strong(exp_tail, new_tail);
                    
                    // Shift the head to NULL
                    Pointer exp_head = Private_head;
                    Pointer new_head{nullptr, {Private_head.count.counter + 1, Private_head.count.mark}};
                    Shared_head.compare_exchange_strong(exp_head, new_head);
                    break;
                }
                case 6: { // Complete dequeuing the object
                    Pointer exp_head = Private_head;
                    Pointer new_head{nullptr, {Private_head.count.counter + 1, Private_head.count.mark}};
                    Shared_head.compare_exchange_strong(exp_head, new_head);
                    break;
                }
                case 7: { // No elements in the queue, do an empty queue enqueue
                    Pointer exp_tail = Private_tail;
                    Pointer new_tail{objectptr, {Private_tail.count.counter + 1, Private_tail.count.mark}};
                    if (Shared_tail.compare_exchange_strong(exp_tail, new_tail)) {
                        // The object has been added, so shift the head to point to it
                        Pointer exp_head = Private_head;
                        Pointer new_head{objectptr, {Private_head.count.counter + 1, Private_head.count.mark}};
                        Shared_head.compare_exchange_strong(exp_head, new_head);
                        success = true;
                    }
                    break;
                }
                case 8: { // Finish incomplete enqueue operation
                    Pointer exp_head = Private_head;
                    Pointer new_head{Private_tail.ptr, {Private_head.count.counter + 1, Private_head.count.mark}};
                    Shared_head.compare_exchange_strong(exp_head, new_head);
                    break;
                }
            }
        } while (!success);
    }

    // Procedure Dequeue (Modified to take T*& and return a bool for emptiness)
    bool dequeue(T*& item) {
        bool success = false;
        Object* dequeued_obj = nullptr;

        do {
            Pointer Private_head, Private_tail, Next;
            Snapshot(Private_head, Private_tail, Next);
            int State = DetermineState(Private_head, Private_tail, Next);

            switch (State) {
                case 1:
                case 2: { // Do a single step dequeue i.e shift head to the next object
                    Pointer exp_head = Private_head;
                    Pointer new_head{Private_head.ptr->nextobject.load().ptr, {Private_head.count.counter + 1, Private_head.count.mark}};
                    if (Shared_head.compare_exchange_strong(exp_head, new_head)) {
                        dequeued_obj = Private_head.ptr;
                        success = true;
                    }
                    break;
                }
                case 3: { // Single object in queue, so do a three step dequeue
                    // Mark the object to be dequeued
                    Pointer exp_next = Next;
                    Pointer new_next{Next.ptr, {Next.count.counter + 1, DEQ}};
                    if (Private_tail.ptr->nextobject.compare_exchange_strong(exp_next, new_next)) {
                        success = true;
                        dequeued_obj = Private_head.ptr;

                        // TODO: can fallthrough to next case
                        // Attempt was successful, so make the tail NULL
                        Pointer exp_tail = Private_tail;
                        Pointer new_tail{nullptr, {Private_tail.count.counter + 1, Private_tail.count.mark}};
                        Shared_tail.compare_exchange_strong(exp_tail, new_tail);

                        // Now shift the head to NULL
                        Pointer exp_head = Private_head;
                        Pointer new_head{nullptr, {Private_head.count.counter + 1, Private_head.count.mark}};
                        Shared_head.compare_exchange_strong(exp_head, new_head);
                    }
                    break;
                }
                case 4: { // Cooperate in dequeuing the object
                    Pointer exp_tail = Private_tail;
                    Pointer new_tail{nullptr, {Private_tail.count.counter + 1, Private_tail.count.mark}};
                    Shared_tail.compare_exchange_strong(exp_tail, new_tail);

                    Pointer exp_head = Private_head;
                    Pointer new_head{nullptr, {Private_head.count.counter + 1, Private_head.count.mark}};
                    Shared_head.compare_exchange_strong(exp_head, new_head);
                    break;
                }
                case 5: { // Complete unfinished enqueue operation
                    Pointer exp_tail = Private_tail;
                    Pointer new_tail{Private_tail.ptr->nextobject.load().ptr, {Private_tail.count.counter + 1, Private_tail.count.mark}};
                    Shared_tail.compare_exchange_strong(exp_tail, new_tail);
                    break;
                }
                case 6: { // Complete dequeuing the object
                    Pointer exp_head = Private_head;
                    Pointer new_head{nullptr, {Private_head.count.counter + 1, Private_head.count.mark}};
                    Shared_head.compare_exchange_strong(exp_head, new_head);
                    break;
                }
                case 7: { // Report empty queue
                    return false; // Queue is empty, return false immediately
                }
                case 8: { // Finish incomplete enqueue operation
                    Pointer exp_head = Private_head;
                    Pointer new_head{Private_tail.ptr, {Private_head.count.counter + 1, Private_head.count.mark}};
                    Shared_head.compare_exchange_strong(exp_head, new_head);
                    break;
                }
            }
        } while (!success);

        // Delink dequeued object from list and extract data
        if (dequeued_obj != nullptr) {
            // dequeued_obj->nextobject.store({nullptr, {0, ENQ}});
            Pointer old_next = dequeued_obj->nextobject.load();
            dequeued_obj->nextobject.store({nullptr, {old_next.count.counter + 1, ENQ}});

            item = dequeued_obj->data;
            
            // TODO: Memory leak as of now
            // Note: In strict lock-free structures without memory reclamation strategies 
            // (like Hazard Pointers or Epoch-based reclamation), `delete dequeued_obj;` 
            // is omitted here to prevent use-after-free segfaults if other lagging threads 
            // are currently examining it in their Snapshot.
            return true;
        }

        return false;
    }
};