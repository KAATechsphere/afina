#ifndef AFINA_NETWORK_NONBLOCKING_WORKER_H
#define AFINA_NETWORK_NONBLOCKING_WORKER_H

#include <memory>
#include <pthread.h>
#include <map>
#include <atomic>
#include <sys/epoll.h>
#include <list>

namespace Afina {

// Forward declaration, see afina/Storage.h
class Storage;

namespace Network {
namespace NonBlocking {

class ConnectionCloseException:public std::runtime_error{
public:
    ConnectionCloseException():std::runtime_error("Connection closed"){}
};

struct ConnectionData;

/**
 * # Thread running epoll
 * On Start spaws background thread that is doing epoll on the given server
 * socket and process incoming connections and its data
 */
class Worker {
public:
    Worker(std::shared_ptr<Afina::Storage> ps);
    Worker(const Worker&);
    ~Worker();

    /**
     * Spaws new background thread that is doing epoll on the given server
     * socket. Once connection accepted it must be registered and being processed
     * on this thread
     */
    void Start(uint32_t port);

    /**
     * Signal background thread to stop. After that signal thread must stop to
     * accept new connections and must stop read new commands from existing. Once
     * all readed commands are executed and results are send back to client, thread
     * must stop
     */
    void Stop();

    /**
     * Blocks calling thread until background one for this worker is actually
     * been destoryed
     */
    void Join();

protected:
    /**
     * Method executing by background thread
     */
    static void* OnRunProxy(void *args);
    void OnRun(int server_socket);
    void handleEvent(epoll_event &event);
    void closeConnection(ConnectionData * pd);
    void recvRequest(ConnectionData* pd);
    void sendAnswer(ConnectionData* pd);

    bool parseAndExecute(char* buffer,size_t bufferLen,ConnectionData &pd);

private:

    std::shared_ptr<Afina::Storage> _pStorage;

    std::list<ConnectionData*> connections;

    pthread_t _thread;

    int _serverSocket;

    static const int maxEvents=100;

    int _epoll;

    static const size_t bufferLength=10000;
    char _buffer[bufferLength];

    std::atomic<bool> _isStop;
};

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
#endif // AFINA_NETWORK_NONBLOCKING_WORKER_H
