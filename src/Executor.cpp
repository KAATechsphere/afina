#include "afina/Executor.h"
#include <iostream>


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
    state.store(State::kStopping);
    empty_condition.notify_all();
    for(int i=0;i<threads.size();++i)
        pthread_join(threads[i],NULL);
    state.store(State::kStopped);
}

void *Afina::perform(void *exec){
    Executor *executor=reinterpret_cast<Executor*>(exec);
    std::unique_lock<std::mutex> lock(executor->mutex);

    while(executor->state.load()==Executor::State::kRun){
        while(executor->tasks.size()==0){
            executor->empty_condition.wait_for(lock,std::chrono::milliseconds(500));
            if(executor->state.load()!=Executor::State::kRun)break;
        }
        std::function<void()> ff=executor->tasks.front();
        executor->tasks.pop_front();
        lock.unlock();
        try{
            ff();
        }catch(const std::exception& e){
            std::cout<<e.what()<<std::endl;
        }
        lock.lock();
    }
}
