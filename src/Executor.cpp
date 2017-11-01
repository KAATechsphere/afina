#include "afina/Executor.h"



Afina::Executor::Executor(std::string name, int size){
    std::unique_lock<std::mutex> lock(mutex);
    for(int i=0;i<size;++i){
        pthread_t thread;
        if(pthread_create(&thread,NULL,perform,this)<0){
            throw std::runtime_error("Could not create executor thread");
        }
        threads.push_back(thread);
    }
}

void Afina::Executor::Stop(bool await){
    state=State::kStopping;
    for(int i=0;i<threads.size();++i)
        pthread_join(threads[i],NULL);
    state=State::kStopped;
}

void *Afina::perform(void *exec){
    Executor *executor=reinterpret_cast<Executor*>(exec);
    std::unique_lock<std::mutex> lock(executor->mutex);
    while(executor->state==Executor::State::kRun){
        while(executor->tasks.size()==0)
            executor->empty_condition.wait(lock);
        std::function<void()> ff=executor->tasks.front();
        executor->tasks.pop_front();
        lock.unlock();
        ff();
        lock.lock();
    }
}
