#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include <cstring>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    char stackTop;
    uint32_t length = std::abs(this->StackBottom - &stackTop);

    std::get<1>(ctx.Stack) = length;

    std::get<0>(ctx.Stack)=(char*)realloc(std::get<0>(ctx.Stack),length);

    if (StackBottom < &stackTop) {
        memcpy(std::get<0>(ctx.Stack), StackBottom, length);
    } else {
        memcpy(std::get<0>(ctx.Stack), &stackTop, length);
    }
}

void Engine::Restore(context &ctx) {
    char stackTop;

    if (std::get<1>(ctx.Stack) > std::abs(StackBottom - &stackTop)) Restore(ctx);

    if (StackBottom < &stackTop) {
        memcpy(StackBottom, std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
    } else {
        memcpy(StackBottom - std::get<1>(ctx.Stack), std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
    }
    longjmp(ctx.Environment, 1);
}

void Engine::yield() {
    context* routine=alive;
    while(routine!=nullptr && routine==cur_routine){
        routine=routine->next;
    }

    if (routine!=nullptr) {
        if(cur_routine!=nullptr){
            if(setjmp(cur_routine->Environment)!=0){
                return;
            }
            Store(*cur_routine);
        }
        cur_routine = static_cast<context *>(routine);
        Restore(*cur_routine);
    }
}

void Engine::sched(void *routine_) {
    if(routine_==nullptr || routine_==cur_routine) return;

    if(cur_routine!=nullptr){
        if(setjmp(cur_routine->Environment)!=0){
            return;
        }
        Store(*cur_routine);
    }
    cur_routine = static_cast<context *>(routine_);
    Restore(*cur_routine);
}

} // namespace Coroutine
} // namespace Afina
