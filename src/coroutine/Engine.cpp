#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include <cstring>
#include <cstddef>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    char stackTop;
    uint32_t length = this->StackBottom - &stackTop;

    if (std::get<0>(ctx.Stack) != nullptr) delete[] std::get<0>(ctx.Stack);

    std::get<1>(ctx.Stack) = length;
    std::get<0>(ctx.Stack) = new char[length];
    memcpy(std::get<0>(ctx.Stack), &stackTop, length);
}

void Engine::Restore(context &ctx) {
    char stackTop;
    if (std::get<1>(ctx.Stack) > StackBottom - &stackTop) Restore(ctx);
    memcpy(StackBottom-std::get<1>(ctx.Stack),std::get<0>(ctx.Stack),std::get<1>(ctx.Stack));
    longjmp(ctx.Environment, 1);
}

void Engine::yield() {
    context* routine=alive;
    while(routine!=nullptr) if(routine!=cur_routine) break;

    if (routine!=nullptr) {
        if(cur_routine==nullptr){
            cur_routine = static_cast<context *>(routine);
            Restore(*cur_routine);
        }
        if (setjmp(cur_routine->Environment) > 0) {
            return;
        } else {
            Store(*cur_routine);
            cur_routine = static_cast<context *>(routine);
            Restore(*cur_routine);
        }
    }
}

void Engine::sched(void *routine_) {
    if(routine_==nullptr || routine_==cur_routine) return;
    if(cur_routine==nullptr){
        cur_routine = static_cast<context *>(routine_);
        Restore(*cur_routine);
    }
    if (setjmp(cur_routine->Environment) > 0) {
        return;
    } else {
       Store(*cur_routine);
       cur_routine = static_cast<context *>(routine_);
       Restore(*cur_routine);
    }
}

} // namespace Coroutine
} // namespace Afina
