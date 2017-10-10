#include <afina/allocator/Simple.h>
//#include <Simple.h>

#include <afina/allocator/Pointer.h>
//#include <Pointer.h>

#include <afina/allocator/Error.h>
//#include <Error.h>

#include <algorithm>
#include <cstring>

typedef uint8_t u8;
typedef intptr_t uptr;

template<class T>
void* ptr_add(T* ptr,long add){
    return reinterpret_cast<void*>(reinterpret_cast<u8*>(ptr)+add);
}

template<class T>
void* ptr_subtract(T* ptr,long subtract){
    return reinterpret_cast<void*>(reinterpret_cast<u8*>(ptr)-subtract);
}

template <class T,class U>
bool isContigiousBlocks(T* fBlock,U* sBlock){
    if(fBlock==nullptr || sBlock==nullptr) return false;
    const u8* fBlockLeft=reinterpret_cast<const u8*>(fBlock);
    const u8* fBlockRight=fBlockLeft+sizeof(T)+fBlock->size;
    const u8* sBlockLeft=reinterpret_cast<const u8*>(sBlock);
    const u8* sBlockRight=sBlockLeft+sizeof(U)+sBlock->size;
    return fBlockRight==sBlockLeft || sBlockRight==fBlockLeft;
}

namespace Afina {
namespace Allocator {

/**
 * TODO: semantics
 * @param N size_t
 */

Pointer Simple::alloc(size_t N) {
    assert(N!=0);

    size_t total_size = N+sizeof(AllocationHeader);
    size_t pointerMemory=sizeof(void*)*(_freePointerCount>0 ? _pointerCount : _pointerCount+1);

    //if unused memory is not enough return null pointer
    if(_baseLength-_usedMemory<total_size+pointerMemory){
        throw AllocError(AllocErrorType::NoMemory,"");
    }

    //if there is not enough memory to save pointer in last free block make defragmentation
    if(!_isDefragmentated && _freePointerCount==0 && (_lastBlock==nullptr || (_lastBlock!=nullptr && _lastBlock->size<=sizeof(void*))))
        defrag();

    FreeBlockHeader* prev_free_block = nullptr;
    FreeBlockHeader* free_block = _freeBlocks;

    while(free_block != nullptr)
    {
        //If allocation doesn't fit in this FreeBlock, try the next one
        if(free_block->size+sizeof(FreeBlockHeader) < total_size)
        {
            prev_free_block = free_block;
            free_block = free_block->next;
            continue;
        }

        AllocationHeader* ah=reinterpret_cast<AllocationHeader*>(free_block);
        void* ptrToPtr;//Pointer to a pointer

        if(free_block!=_lastBlock){
            ptrToPtr=allocPointer(ptr_add(ah,sizeof(AllocationHeader)));
            allocDataInFreeBlock(free_block,prev_free_block,N);
        }else{
            if(_isDefragmentated || _freePointerCount){
                ptrToPtr=allocPointer(ptr_add(ah,sizeof(AllocationHeader)));
                _lastBlock=allocDataInFreeBlock(free_block,prev_free_block,N);
            }else{
                size_t tempS1=sizeof(FreeBlockHeader)+_lastBlock->size;
                size_t tempS2=sizeof(AllocationHeader)+N+sizeof(void*);
                if(tempS1<tempS2){
			break;
                }else if(tempS1<tempS2+sizeof(FreeBlockHeader)+1){
                    ah->size=sizeof(FreeBlockHeader)+_lastBlock->size-sizeof(void*)-sizeof(AllocationHeader);
                    ++_usedMemory;
                    ++_pointerCount;
                    *reinterpret_cast<void**>(ptr_add(_base,_baseLength-sizeof(void*)*_pointerCount))=ptr_add(ah,sizeof(AllocationHeader));
                    ptrToPtr=ptr_add(_base,_baseLength-sizeof(void*)*_pointerCount);
                    _lastBlock=nullptr;
                }else{
                    ptrToPtr=allocPointer(ptr_add(ah,sizeof(AllocationHeader)));
                    _lastBlock=allocDataInFreeBlock(free_block,prev_free_block,N);
                }
            }
        }
        Pointer p;
        p.set(ptrToPtr);
        return p;
    }

    defrag();
    return alloc(N);
}


/**
 * TODO: semantics
 * @param p Pointer
 * @param N size_t
 */
void Simple::realloc(Pointer &p, size_t N) {
    void* rawPointer=p.getRaw();
    if(rawPointer==nullptr){
        p=alloc(N);
        return;
    }
    if(rawPointer>ptr_add(_base,_baseLength) || rawPointer<ptr_add(_base,_baseLength-sizeof(void*)*_pointerCount))
        return;

    AllocationHeader *src=(AllocationHeader*)ptr_subtract(*reinterpret_cast<void**>(p.getRaw()),sizeof(AllocationHeader));
    long diff=N-src->size;
    if(long(_baseLength-_usedMemory-sizeof(void*)*_pointerCount)<diff){
        throw AllocError(AllocErrorType::NoMemory,"");
    }
    size_t tempSize=src->size;
    if(diff>0){
        if(!_isDefragmentated){
            FreeBlockHeader*prevPrevFb,*prevFb,*nextFb;
            getNeighborFreeBlock(rawPointer,prevPrevFb,prevFb,nextFb);
            FreeBlockHeader *cprevFb=isContigiousBlocks(prevFb,src)?prevFb:nullptr;
            FreeBlockHeader *cnextFb=isContigiousBlocks(nextFb,src)?nextFb:nullptr;
            size_t cprevFbSize=cprevFb==nullptr?0:cprevFb->size;
            size_t cnextFbSize=cnextFb==nullptr?0:cnextFb->size;
            if(cprevFbSize+cnextFbSize+2*sizeof(FreeBlockHeader)>=diff){
                if(cnextFbSize+sizeof(FreeBlockHeader)>=diff){
                    FreeBlockHeader* newFb=nullptr;
                    if(cnextFbSize>diff){
                        src->size+=diff;
                        _usedMemory+=diff;
                        newFb=reinterpret_cast<FreeBlockHeader*>(ptr_add(nextFb,diff));
                        newFb->next=nextFb->next;
                        newFb->size=nextFb->size-diff;
                    }else{
                        src->size+=sizeof(FreeBlockHeader)+cnextFbSize;
                        _usedMemory+=sizeof(FreeBlockHeader)+cnextFbSize;
                    }
                    if(prevFb!=nullptr)prevFb->next=newFb;
                    else _freeBlocks=newFb;
                    if(nextFb==_lastBlock)_lastBlock=newFb;
                }else if(cprevFbSize+sizeof(FreeBlockHeader)>=diff){
                    if(cprevFbSize>diff){
                        std::memmove((void*)ptr_subtract(src,diff),(void*)src,sizeof(AllocationHeader)+src->size);
                        src=(AllocationHeader*)ptr_subtract(src,diff);
                        src->size+=diff;
                        prevFb->size-=diff;
                        _usedMemory+=diff;
                    }else{
                        std::memmove((void*)prevFb,(void*)src,sizeof(AllocationHeader)+src->size);
                        src=(AllocationHeader*)prevFb;
                        src->size+=sizeof(FreeBlockHeader)+cprevFbSize;
                        if(prevPrevFb!=nullptr)prevPrevFb->next=nextFb;
                        else _freeBlocks=nextFb;
                        _usedMemory+=sizeof(FreeBlockHeader)+cprevFbSize;
                    }
                    *reinterpret_cast<void**>(rawPointer)=(void*)src;
                }else{
                    std::memmove((void*)prevFb,(void*)src,sizeof(AllocationHeader)+src->size);
                    src=(AllocationHeader*)prevFb;
                    FreeBlockHeader *newFb=nullptr;
                    if(cnextFbSize+cprevFbSize+sizeof(FreeBlockHeader)>diff){
                        src->size+=diff;
                        _usedMemory+=diff;
                        newFb=(FreeBlockHeader*)ptr_add(src,sizeof(AllocationHeader)+src->size);
                        newFb->next=nextFb->next;
                        newFb->size=cnextFbSize+sizeof(FreeBlockHeader)+cprevFbSize-diff;
                        if(prevPrevFb!=nullptr)prevPrevFb->next=newFb;
                        else _freeBlocks=newFb;
                    }else{
                        src->size+=2*sizeof(FreeBlockHeader)+cprevFbSize+cnextFbSize;
                        _usedMemory+=2*sizeof(FreeBlockHeader)+cprevFbSize+cnextFbSize;
                        if(prevPrevFb!=nullptr)prevPrevFb->next=nextFb->next;
                        else _freeBlocks=nextFb->next;
                    }
                    if(_lastBlock==nextFb)_lastBlock=newFb;
                    *reinterpret_cast<void**>(rawPointer)=(void*)src;
                }
            }else{
                defrag();
                realloc(p,N);
                return;
            }
        }else{
            std::rotate((u8*)src,(u8*)ptr_add(src,sizeof(AllocationHeader)+src->size),(u8*)_lastBlock);
            updatePointers(ptr_add(src,sizeof(AllocationHeader)+tempSize),(void*)_lastBlock,-long(src->size+sizeof(AllocationHeader)));
            AllocationHeader *nah=(AllocationHeader*)ptr_subtract(_lastBlock,sizeof(AllocationHeader)+tempSize);
            *reinterpret_cast<void**>(rawPointer)=(void*)ptr_add(nah,sizeof(AllocationHeader));
            if(_lastBlock->size>diff){
                nah->size+=diff;
                _usedMemory+=diff;
                _lastBlock=(FreeBlockHeader*)ptr_add(_lastBlock,diff);
                _lastBlock->size=_freeBlocks->size-diff;
                _lastBlock->next=nullptr;
                _freeBlocks=_lastBlock;
            }else{
                nah->size+=sizeof(FreeBlockHeader)+_lastBlock->size;
                _usedMemory+=sizeof(FreeBlockHeader)+_lastBlock->size;
                _lastBlock=_freeBlocks=nullptr;
            }
        }
    }else if(diff<0){
        if(-diff>sizeof(FreeBlockHeader)){
            FreeBlockHeader *prevFB,*nextFB;
            getNeighborFreeBlock((void*)src,prevFB,nextFB);
            FreeBlockHeader *nfbh=(FreeBlockHeader*)ptr_add(src,sizeof(AllocationHeader)+N);
            nfbh->size=src->size-N-sizeof(FreeBlockHeader);
            if(nextFB!=nullptr && isContigiousBlocks(nfbh,nextFB)){
                nfbh->size+=sizeof(FreeBlockHeader)+nextFB->size;
                nfbh->next=nextFB->next;
                if(_lastBlock==nextFB)_lastBlock=nextFB;
            }else{
                nfbh->next=nextFB;
                if(ptr_add(nfbh,sizeof(void*)+nfbh->size)==ptr_add(_base,_baseLength-sizeof(void*)*_pointerCount))
                    _lastBlock=nfbh;
                _isDefragmentated=false;
            }
            if(prevFB!=nullptr) prevFB->next=nfbh;
            else _freeBlocks=nfbh;

            _usedMemory+=diff;
        }
    }
}


/**
 * TODO: semantics
 * @param p Pointer
 */
void Simple::free(Pointer &p) {
    if(p.getRaw()<ptr_add(_base,_baseLength-sizeof(void*)*_pointerCount) || p.getRaw()>ptr_add(_base,_baseLength)){
        throw AllocError(AllocErrorType::InvalidFree,"");
    }
    void *delPointer=ptr_subtract(*reinterpret_cast<void**>(p.getRaw()),sizeof(AllocationHeader));

    size_t tempSize=reinterpret_cast<AllocationHeader*>(delPointer)->size;

    FreeBlockHeader* prev_free_block = nullptr;
    FreeBlockHeader* free_block = _freeBlocks;

    while(!(prev_free_block==nullptr || prev_free_block<delPointer) || !(free_block==nullptr || free_block>delPointer))
    {
        prev_free_block = free_block;
        free_block = free_block->next;
    }

    void *leftBorder=prev_free_block!=nullptr ? ptr_add(prev_free_block,sizeof(FreeBlockHeader)+prev_free_block->size) : nullptr;
    void *rightBorder=ptr_add(delPointer,sizeof(AllocationHeader)+reinterpret_cast<AllocationHeader*>(delPointer)->size);

    FreeBlockHeader *lfb=nullptr;
    if(leftBorder==delPointer && rightBorder==(void*)free_block){
        prev_free_block->next=free_block->next;
        prev_free_block->size=((char*)free_block-(char*)prev_free_block)+free_block->size;
        lfb=prev_free_block;
    }
    if(leftBorder==delPointer && rightBorder!=(void*)free_block){
        prev_free_block->size=prev_free_block->size+sizeof(AllocationHeader)+((AllocationHeader*)delPointer)->size;
        lfb=free_block;
    }
    if(leftBorder!=delPointer && rightBorder==(void*)free_block){
        FreeBlockHeader *fb=(FreeBlockHeader*)delPointer;
        fb->size=sizeof(AllocationHeader)+((AllocationHeader*)delPointer)->size+free_block->size;
        fb->next=free_block->next;
        if(prev_free_block!=nullptr) prev_free_block->next=fb;
        else _freeBlocks=fb;
        lfb=fb;
    }
    if(leftBorder!=delPointer && rightBorder!=(void*)free_block){
        FreeBlockHeader *fb=(FreeBlockHeader*)delPointer;
        fb->size=sizeof(AllocationHeader)+((AllocationHeader*)delPointer)->size-sizeof(FreeBlockHeader);
        fb->next=free_block;
        if(prev_free_block!=nullptr) prev_free_block->next=fb;
        else _freeBlocks=fb;
        lfb=fb;
    }
    if(isLastFreeBlock(lfb))_lastBlock=lfb;
    _usedMemory=_usedMemory-sizeof(AllocationHeader)-tempSize;

    *reinterpret_cast<void**>(p.getRaw())=nullptr;
    ++_freePointerCount;

    size_t firstNullPtrCount=0;
    void* value=nullptr;
    for(size_t i=0;i<_pointerCount && value==nullptr;++i){
        value=*reinterpret_cast<void**>(ptr_add(_base,_baseLength-sizeof(void*)*(_pointerCount-i)));
        firstNullPtrCount+=(value==nullptr?1:0);
    }
    if(firstNullPtrCount>0){
        if(_lastBlock!=nullptr){
            _pointerCount-=firstNullPtrCount;
            _freePointerCount-=firstNullPtrCount;
            _lastBlock->size+=sizeof(void*)*firstNullPtrCount;
            _usedMemory=_usedMemory-sizeof(void*)*firstNullPtrCount;
        }else if(sizeof(void*)*firstNullPtrCount>sizeof(FreeBlockHeader)){
            FreeBlockHeader* fb=(FreeBlockHeader*)ptr_add(_base,_baseLength-sizeof(void*)*(_pointerCount));
            fb->size=sizeof(void*)*firstNullPtrCount-sizeof(FreeBlockHeader);
            _pointerCount-=firstNullPtrCount;
            _freePointerCount-=firstNullPtrCount;
            _lastBlock=fb;
            getLastFreeBlock()->next=fb;
            _usedMemory=_usedMemory-sizeof(void*)*firstNullPtrCount;
        }
    }

    _isDefragmentated=false;
}

/**
 * TODO: semantics
 */
void Simple::defrag() {
    if(_isDefragmentated)return;

    FreeBlockHeader* prev_free_block = _freeBlocks;
    FreeBlockHeader* free_block = _freeBlocks==nullptr ? nullptr : _freeBlocks->next;

    while(prev_free_block != nullptr){
        if(free_block!=nullptr){
            updatePointers(prev_free_block,free_block,-sizeof(FreeBlockHeader)-prev_free_block->size);
            void *src=ptr_add(prev_free_block,sizeof(FreeBlockHeader)+prev_free_block->size);
            std::memmove((void*)prev_free_block,src,(uptr)free_block-(uptr)src);
            prev_free_block=reinterpret_cast<FreeBlockHeader*>(ptr_add(prev_free_block,(uptr)free_block-(uptr)src));
            prev_free_block->next=free_block->next;
            prev_free_block->size=free_block->size+(((uptr)free_block)-((uptr)prev_free_block));
            free_block=free_block->next;
        }else{
            void *pointerStart=ptr_add(_base,_baseLength-sizeof(void*)*_pointerCount);
            void *dataStart=ptr_add(prev_free_block,sizeof(FreeBlockHeader)+prev_free_block->size);
            if((char*)pointerStart-(char*)dataStart>0){
                updatePointers(prev_free_block,pointerStart,-sizeof(FreeBlockHeader)-prev_free_block->size);
                size_t freeBlockSize=prev_free_block->size;
                std::memmove((void*)prev_free_block,dataStart,(u8*)pointerStart-(u8*)dataStart);
                prev_free_block=(FreeBlockHeader*)(ptr_add(prev_free_block,(u8*)pointerStart-(u8*)dataStart));
                prev_free_block->size=freeBlockSize;
                prev_free_block->next=nullptr;
            }
            _freeBlocks=prev_free_block;
            break;
        }
    }
    _lastBlock=_freeBlocks;
    _isDefragmentated=true;
}

/**
 * TODO: semantics
 */
std::string Simple::dump() const { return ""; }

bool Simple::isLastFreeBlock(FreeBlockHeader *pointer){
    if((u8*)pointer<(u8*)_base || (u8*)pointer>(u8*)_base+_baseLength) return false;
    intptr_t blockEndPtr=reinterpret_cast<intptr_t>(pointer)+sizeof(FreeBlockHeader)+pointer->size;
    intptr_t pointStartPtr=reinterpret_cast<intptr_t>(_base)+_baseLength-sizeof(void*)*_pointerCount;
    return blockEndPtr==pointStartPtr;
}

void Simple::getNeighborFreeBlock(void* fb,FreeBlockHeader* &prevFB,FreeBlockHeader* &nextFB){
    prevFB = nullptr;
    nextFB = _freeBlocks;

    while(!(prevFB==nullptr || prevFB<fb) || !(nextFB==nullptr || nextFB>fb))
    {
        prevFB = nextFB;
        nextFB = nextFB->next;
    }
}

void Simple::getNeighborFreeBlock(void *fb, Simple::FreeBlockHeader *&prevPrevFB, Simple::FreeBlockHeader *&prevFB, Simple::FreeBlockHeader *&nextFB)
{
    prevPrevFB=nullptr;
    prevFB = nullptr;
    nextFB = _freeBlocks;

    while(!(prevFB==nullptr || prevFB<fb) || !(nextFB==nullptr || nextFB>fb))
    {
        prevPrevFB=prevFB;
        prevFB = nextFB;
        nextFB = nextFB->next;
    }
}

Simple::FreeBlockHeader* Simple::allocDataInFreeBlock(FreeBlockHeader* fb,FreeBlockHeader* prevFb,size_t size){
    if(fb->size+sizeof(FreeBlockHeader)<sizeof(AllocationHeader)+size) return nullptr;

    AllocationHeader* ah=(AllocationHeader*)fb;
    if(fb->size+sizeof(FreeBlockHeader)<sizeof(AllocationHeader)+size+sizeof(FreeBlockHeader)+1){
        if(prevFb!=nullptr)prevFb->next=fb->next;
        else _freeBlocks=fb->next;
        ah->size=sizeof(FreeBlockHeader)+fb->size-sizeof(AllocationHeader);
        _usedMemory+=sizeof(AllocationHeader)+ ah->size;
        return nullptr;
    }else{
        FreeBlockHeader* nfb=(FreeBlockHeader*)ptr_add(fb,sizeof(AllocationHeader)+size);
        nfb->next=fb->next;
        nfb->size=fb->size-sizeof(AllocationHeader)-size;
        ah->size=size;
        _usedMemory+=sizeof(AllocationHeader)+ ah->size;
        if(prevFb!=nullptr)prevFb->next=nfb;
        else _freeBlocks=nfb;
        return nfb;
    }
}

void *Simple::allocPointer(void *ptr)
{
    if(_freePointerCount>0){
        for(size_t i=0;i<_pointerCount;++i){
            void* &point=*reinterpret_cast<void**>(ptr_add(_base,_baseLength-sizeof(void*)*(i+1)));
            if(point==nullptr){
                point=ptr;
                --_freePointerCount;
                return ptr_add(_base,_baseLength-sizeof(void*)*(i+1));
            }
        }
    }else{
        if(_lastBlock==nullptr || _lastBlock->size<=sizeof(void*)) return nullptr;
        else{
            _lastBlock->size-=sizeof(void*);
            _pointerCount++;
            *reinterpret_cast<void**>(ptr_add(_base,_baseLength-sizeof(void*)*_pointerCount))=ptr;
            return ptr_add(_base,_baseLength-sizeof(void*)*_pointerCount);
        }
    }
}

Simple::FreeBlockHeader *Simple::getLastFreeBlock()
{
    FreeBlockHeader *fb=_freeBlocks;
    for(fb;fb!=nullptr && fb->next!=nullptr;fb=fb->next);
    return fb;
}

void Simple::updatePointers(void *start, void *end, long diff)
{
    void** pointers=reinterpret_cast<void**>(((u8*)_base)+_baseLength-sizeof(void*)*_pointerCount);
    for(size_t i=0;i<_pointerCount;++i){
        if(pointers[i]<=end && pointers[i]>=start)
            pointers[i]=ptr_add(pointers[i],diff);
    }
}

} // namespace Allocator
} // namespace Afina
