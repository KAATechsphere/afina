#include "MapBasedGlobalLockImpl.h"

#include <mutex>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);
    UnblockedPut(key,value);
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);
    if(_backend.count(key)) return false;
    else return UnblockedPut(key,value);
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Set(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);
    if(_backend.count(key)) return UnblockedPut(key,value);
    else return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Delete(const std::string &key) {
    std::unique_lock<std::mutex> guard(_lock);
    //change count to erase
    if(_backend.count(key)){
        _backend.erase(key);
        auto prioIterator=_priorities.find(key);
        _sortedPriorities.erase(std::make_pair(prioIterator->second,key));
        _priorities.erase(prioIterator);
    }
    else return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Get(const std::string &key, std::string &value) const {
    std::unique_lock<std::mutex> guard(*const_cast<std::mutex *>(&_lock));
    auto it=_backend.find(key);
    if(it!=_backend.end()){
        value=it->second;
        return true;
    }else return false;
}

bool MapBasedGlobalLockImpl::UnblockedPut(const std::string &key, const std::string &value)
{
    ++_priorityIndex;
    _backend[key]=value;
    std::pair<size_t,std::string> priorPair;
    std::map<std::string,size_t>::iterator priorIterator;

    //TODO: find not count
    if(_backend.count(key)){
        priorIterator=_priorities.find(key);
        priorPair=std::make_pair(priorIterator->second,key);
        _sortedPriorities.erase(priorPair);
        priorPair.first=_priorityIndex;
        priorIterator->second=_priorityIndex;
    }else{
        if(_backend.size()>_max_size){
            priorPair=*_sortedPriorities.begin();
            _backend.erase(priorPair.second);
            _priorities.erase(priorPair.second);
            _sortedPriorities.erase(_sortedPriorities.begin());
        }
        priorPair=std::make_pair(_priorityIndex,key);
        _priorities[key]=_priorityIndex;
    }
    _sortedPriorities.insert(priorPair);
    return true;
}

} // namespace Backend
} // namespace Afina
