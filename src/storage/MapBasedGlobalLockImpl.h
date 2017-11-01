#ifndef AFINA_STORAGE_MAP_BASED_GLOBAL_LOCK_IMPL_H
#define AFINA_STORAGE_MAP_BASED_GLOBAL_LOCK_IMPL_H

#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <string>


#include <afina/Storage.h>
//#include <Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation with global lock
 *
 *
 */
class MapBasedGlobalLockImpl : public Afina::Storage {
public:
    MapBasedGlobalLockImpl(size_t max_size = 1024) : _max_size(max_size),_priorityIndex(0){}
    ~MapBasedGlobalLockImpl() {}

    // Implements Afina::Storage interface
    bool Put(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool PutIfAbsent(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Set(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Delete(const std::string &key) override;

    // Implements Afina::Storage interface
    bool Get(const std::string &key, std::string &value) const override;
protected:
    bool UnblockedPut(const std::string &key, const std::string &value);
private:
    std::mutex _lock;

    size_t _max_size;
    size_t _priorityIndex;

    struct PriorityComparator{
        bool operator() (const std::pair<size_t,std::string>& a,const std::pair<size_t,std::string>& b)const{
            return a.first<b.first;
        }
    };

    std::set<std::pair<size_t,std::string>,PriorityComparator> _sortedPriorities;
    std::map<std::string, std::string> _backend;
    std::map<std::string,size_t> _priorities;
};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_MAP_BASED_GLOBAL_LOCK_IMPL_H
