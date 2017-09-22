#include <afina/allocator/Pointer.h>
//#include <Pointer.h>

namespace Afina {
namespace Allocator {

Pointer::Pointer() {_addr=nullptr;}
Pointer::Pointer(const Pointer &p) {
    _addr=p._addr;
}
Pointer::Pointer(Pointer &&p) {_addr=p._addr;}

Pointer &Pointer::operator=(const Pointer &p) { _addr=p._addr;return *this; }
Pointer &Pointer::operator=(Pointer &&p) {_addr=p._addr; return *this; }

} // namespace Allocator
} // namespace Afina
