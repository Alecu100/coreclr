// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#ifndef _ALLOC_H_
#define _ALLOC_H_

#if !defined(_HOST_H_)
#include "host.h"
#endif // defined(_HOST_H_)

class ArenaAllocator
{
private:
    ArenaAllocator(const ArenaAllocator& other) = delete;
    ArenaAllocator& operator=(const ArenaAllocator& other) = delete;

protected:
    struct PageDescriptor
    {
        PageDescriptor* m_next;
        PageDescriptor* m_previous;

        size_t m_pageBytes; // # of bytes allocated
        size_t m_usedBytes; // # of bytes actually used. (This is only valid when we've allocated a new page.)
                            // See ArenaAllocator::allocateNewPage.

        BYTE m_contents[];
    };

    // Anything less than 64K leaves VM holes since the OS allocates address space in this size.
    // Thus if we want to make this smaller, we need to do a reserve / commit scheme
    enum
    {
        DEFAULT_PAGE_SIZE = 16 * OS_page_size,
        MIN_PAGE_SIZE     = sizeof(PageDescriptor)
    };

    static size_t s_defaultPageSize;

    IEEMemoryManager* m_memoryManager;

    PageDescriptor* m_firstPage;
    PageDescriptor* m_lastPage;

    // These two pointers (when non-null) will always point into 'm_lastPage'.
    BYTE* m_nextFreeByte;
    BYTE* m_lastFreeByte;

    bool isInitialized();

    void* allocateNewPage(size_t size, bool canThrow);

    void* allocateHostMemory(size_t size);
    void freeHostMemory(void* block);

public:
    ArenaAllocator();
    ArenaAllocator(IEEMemoryManager* memoryManager);
    ArenaAllocator& operator=(ArenaAllocator&& other);

    // NOTE: it would be nice to have a destructor on this type to ensure that any value that
    //       goes out of scope is either uninitialized or has been torn down via a call to
    //       destroy(), but this interacts badly in methods that use SEH. #3058 tracks
    //       revisiting EH in the JIT; such a destructor could be added if SEH is removed
    //       as part of that work.

    virtual void destroy();

    inline void* allocateMemory(size_t sz);

    size_t getTotalBytesAllocated();
    size_t getTotalBytesUsed();

    static bool   bypassHostAllocator();
    static size_t getDefaultPageSize();

    static void startup();
    static void shutdown();

    static ArenaAllocator* getPooledAllocator(IEEMemoryManager* memoryManager);
};

//------------------------------------------------------------------------
// ArenaAllocator::allocateMemory:
//    Allocates memory using an `ArenaAllocator`.
//
// Arguments:
//    size - The number of bytes to allocate.
//
// Return Value:
//    A pointer to the allocated memory.
//
// Note:
//    The DEBUG version of the method has some abilities that the release
//    version does not: it may inject faults into the allocator and
//    seeds all allocations with a specified pattern to help catch
//    use-before-init problems.
//
inline void* ArenaAllocator::allocateMemory(size_t size)
{
    assert(isInitialized());
    assert(size != 0);

    // Ensure that we always allocate in pointer sized increments.
    size = (size_t)roundUp(size, sizeof(size_t));

#if defined(DEBUG)
    if (JitConfig.ShouldInjectFault() != 0)
    {
        // Force the underlying memory allocator (either the OS or the CLR hoster)
        // to allocate the memory. Any fault injection will kick in.
        void* p = ClrAllocInProcessHeap(0, S_SIZE_T(1));
        if (p != nullptr)
        {
            ClrFreeInProcessHeap(0, p);
        }
        else
        {
            NOMEM(); // Throw!
        }
    }
#endif

    void* block = m_nextFreeByte;
    m_nextFreeByte += size;

    if (m_nextFreeByte > m_lastFreeByte)
    {
        block = allocateNewPage(size, true);
    }

#if defined(DEBUG)
    memset(block, UninitializedWord<char>(nullptr), size);
#endif

    return block;
}

#endif // _ALLOC_H_
