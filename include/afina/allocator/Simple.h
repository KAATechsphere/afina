#ifndef AFINA_ALLOCATOR_SIMPLE_H
#define AFINA_ALLOCATOR_SIMPLE_H

#include <string>
#include <cstddef>
#include <cassert>

namespace Afina {
namespace Allocator {

// Forward declaration. Do not include real class definition
// to avoid expensive macros calculations and increase compile speed
class Pointer;

/**
 * Wraps given memory area and provides defagmentation allocator interface on
 * the top of it.
 *
 * Allocator instance doesn't take ownership of wrapped memmory and do not delete it
 * on destruction. So caller must take care of resource cleaup after allocator stop
 * being needs
 */
// TODO: Implements interface to allow usage as C++ allocators
class Simple {
public:
    Simple(void *base, const size_t size):_base(base),_baseLength(size),_freeBlocks((FreeBlockHeader*)base),
    _lastBlock((FreeBlockHeader*)base),_pointerCount(0),_freePointerCount(0),_isDefragmentated(true){
        assert(size > sizeof(FreeBlockHeader));
        _freeBlocks->next=nullptr;
        _freeBlocks->size=size-sizeof(FreeBlockHeader);
        _usedMemory = 0;
        _numAllocations = 0;
    }

    /**
     * TODO: semantics
     * @param N size_t
     */
    Pointer alloc(size_t N);

    /**
     * TODO: semantics
     * @param p Pointer
     * @param N size_t
     */
    void realloc(Pointer &p, size_t N);

    /**
     * TODO: semantics
     * @param p Pointer
     */
    void free(Pointer &p);

    /**
     * TODO: semantics
     */
    void defrag();

    /**
     * TODO: semantics
     */
    std::string dump() const;

private:
    struct FreeBlockHeader
    {
        size_t size;
        FreeBlockHeader* next;
    };

    struct AllocationHeader
    {
        size_t size;
    };

    bool isLastFreeBlock(FreeBlockHeader *pointer);
    FreeBlockHeader* getLastFreeBlock();
    void updatePointers(void *start,void *end,long diff);
    void getNeighborFreeBlock(void* fb,FreeBlockHeader* &prevFB,FreeBlockHeader* &nextFB);
void getNeighborFreeBlock(void* fb,FreeBlockHeader* &prevPrevFB,FreeBlockHeader* &prevFB,FreeBlockHeader* &nextFB);
    FreeBlockHeader* allocDataInFreeBlock(FreeBlockHeader* fb,FreeBlockHeader* prevFb,size_t size);
    void *allocPointer(void* ptr);

    void *_base;
    const size_t _baseLength;

    FreeBlockHeader* _freeBlocks;
    FreeBlockHeader* _lastBlock;

    size_t _usedMemory;
    size_t _numAllocations;
    size_t _pointerCount;
    size_t _freePointerCount;

    bool _isDefragmentated;
};

} // namespace Allocator
} // namespace Afina
#endif // AFINA_ALLOCATOR_SIMPLE_H
