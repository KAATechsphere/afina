#include "Worker.h"

#include <iostream>

#include <pthread.h>
#include <signal.h>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <list>
#include <memory>

#include <iterator>

#include "Utils.h"

#include "../../protocol/Parser.h"
#include "afina/execute/Command.h"

#include <cstring>
#include <list>

namespace Afina {
namespace Network {
namespace NonBlocking {

enum class ParseState{CmdWaiting,ValueWaiting,CmdExecuting};

struct ConnectionData{
    int socket;
    ParseState state;
    size_t readBodyLength;
    uint32_t bodyLength;
    std::string dataBuffer;
    Afina::Protocol::Parser parser;
    std::unique_ptr<Execute::Command> command;

    std::list<std::string> out;

    ConnectionData(int connectionSocket){
        socket=connectionSocket;
        readBodyLength=0;
        bodyLength=0;
    }
};

struct WorkerNet{
    Worker *worker;
    uint32_t port;
};

void* Worker::OnRunProxy(void *args){
    WorkerNet wn=(*reinterpret_cast<WorkerNet*>(args));
    delete reinterpret_cast<WorkerNet*>(args);
    // If a client closes a connection, this will generally produce a SIGPIPE
    // signal that will kill the process. We want to ignore this signal, so send()
    // just returns -1 when this happens.
    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    // Create server socket
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;         // IPv4
    server_addr.sin_port = htons(wn.port);       // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address

    int server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == -1) {
        throw std::runtime_error("Failed to open socket");
    }

    int opts = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    opts=1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &opts, sizeof(opts))==-1){
        close(server_socket);
        throw std::runtime_error("Sockets setcockopt() failed");
    }

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    make_socket_non_blocking(server_socket);
    if (listen(server_socket, 10) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket listen() failed");
    }
    wn.worker->OnRun(server_socket);
}

bool Worker::parseAndExecute(char* buffer,size_t bufferLen,ConnectionData &pd){
    size_t parsedLength=10;
    size_t appendLength;
    char *parsedPtr=buffer;
    std::string out;
    bool res=true;
    bool isCommandParsed=false;
    while(res){
        switch(pd.state){
        case ParseState::CmdWaiting:
            try{
                //We will stop call parser if it parsed command
                //or it didn't parsed command and parsed length equals zero
                //or there are no any characters for parsing
                do{
                    isCommandParsed=pd.parser.Parse(parsedPtr, buffer+bufferLen-parsedPtr, parsedLength);
                    parsedPtr+=parsedLength;
                }while((parsedLength!=0 || !isCommandParsed) && !isCommandParsed && parsedPtr<buffer+bufferLen);
            }catch(std::runtime_error& e){
                out+=std::string("SERVER ERROR ")+e.what()+"\r\n";
                res=false;
                continue;
            }
            if(!isCommandParsed && parsedPtr<buffer+bufferLen){
                res=false;
                continue;
            }
            if(isCommandParsed){
                pd.command=pd.parser.Build(pd.bodyLength);
                pd.state=pd.bodyLength==0? ParseState::CmdExecuting : ParseState::ValueWaiting;
                pd.dataBuffer.reserve(pd.bodyLength);
            }
            break;
        case ParseState::ValueWaiting:
            appendLength=std::min(pd.bodyLength-pd.readBodyLength+2,bufferLen-(parsedPtr-buffer));
            std::copy(parsedPtr,parsedPtr+appendLength,std::back_inserter(pd.dataBuffer));
            parsedPtr+=appendLength;
            pd.readBodyLength+=appendLength;
            if(pd.readBodyLength==pd.bodyLength+2){
                pd.state=ParseState::CmdExecuting;
            }
            break;
        case ParseState::CmdExecuting:
            try{
                std::string temp;
                pd.command->Execute(*_pStorage,pd.dataBuffer,temp);
                out += temp+"\r\n";
            }catch(std::exception &e){
                out+=std::string("SERVER_ERROR ")+e.what()+"\r\n";
                res=false;
                continue;
            }
            pd.state=ParseState::CmdWaiting;
            break;
        }
        if(parsedPtr>=buffer+bufferLen && pd.state!=ParseState::CmdExecuting){
            break;
        }
    }
    pd.out.push_back(out);
    return res;
}
// See Worker.h
Worker::Worker(std::shared_ptr<Afina::Storage> ps):_pStorage(ps){}

Worker::Worker(const Worker &){
    _isStop.store(false);
}

// See Worker.h
Worker::~Worker() {
    // TODO: implementation here
}

// See Worker.h
void Worker::Start(uint32_t port) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    _isStop.store(false);
    pthread_t worker_thread;
    WorkerNet *wn=new WorkerNet();
    wn->port=port;
    wn->worker=this;

    if ((_thread=pthread_create(&worker_thread, NULL, Worker::OnRunProxy, wn)) < 0) {
        delete wn;
        throw std::runtime_error("Could not create server thread");
    }
}

// See Worker.h
void Worker::Stop() {
    _isStop.store(true);
}

// See Worker.h
void Worker::Join() {
    pthread_join(_thread, 0);
}

// See Worker.h
void Worker::OnRun(int server_socket) {
    //std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    if((_epoll = epoll_create1(0)) == -1) {
        close(server_socket);
        throw std::runtime_error("Could not create epoll");
    }

    struct epoll_event event;
    event.events=EPOLLIN|EPOLLET;
    event.data.fd=server_socket;

    if(epoll_ctl(_epoll, EPOLL_CTL_ADD, server_socket, &event) == -1) {
        close(server_socket);
        throw std::runtime_error("Could not add the socket to epoll");
    }

    _serverSocket=server_socket;

    struct epoll_event outEvents[Worker::maxEvents];

    while(!(_isStop.load())){
        int sock_event_count=epoll_wait(_epoll, outEvents, maxEvents, 100);

        if(sock_event_count==-1 && errno!=EINTR) throw std::runtime_error("Epoll wait error");

        for(int i=0;i<sock_event_count;++i){
            try {
                handleEvent(outEvents[i]);
            } catch(std::exception& e) {
                std::cout << e.what() << std::endl;
            }
        }
    }

    for(auto it=connections.begin();it!=connections.end();++it){
        ConnectionData *pd=*it;
        epoll_ctl(_epoll, EPOLL_CTL_DEL, pd->socket,0);
        close(pd->socket);
        delete pd;
    }
    connections.clear();

    close(server_socket);
}

void Worker::handleEvent(struct epoll_event &event)
{
    int socket=event.data.fd;
    if(socket==_serverSocket){
        while(true){
            struct sockaddr in_addr;
            socklen_t in_len = sizeof(in_addr);
            int client_socket = -1;

            if((client_socket = accept(_serverSocket, &in_addr, &in_len)) == -1) {
                if(errno!=EAGAIN && errno!=EWOULDBLOCK){
                    std::cout<<"Error accept socket"<<std::endl;
                    close(client_socket);
                }
                return;
            }
            if (connections.size() == maxEvents){
                std::cout << "Event array is full" << std::endl;
                close(client_socket);
                return;
            }

            make_socket_non_blocking(client_socket);

            struct epoll_event client_sock_event;
            client_sock_event.events=EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP;
            client_sock_event.data.fd=client_socket;
            ConnectionData *pd=new ConnectionData(client_socket);
            client_sock_event.data.ptr=(void*)pd;

            if(epoll_ctl(_epoll, EPOLL_CTL_ADD, client_socket, &client_sock_event) == -1) {
                close(client_socket);
                delete pd;
                std::cout<<"Could not add to epoll socket"<<std::endl;
                return;
            }

            std::cout<<"Connection accepted!"<<std::endl;
            connections.push_back(pd);
        }
    }else{
        std::cout<<"Client is being served"<<std::endl;
        ConnectionData &parseData=*static_cast<ConnectionData*>(event.data.ptr);
        if((event.events & EPOLLHUP)|| (event.events & EPOLLERR)){
            closeConnection(&parseData);
        }
        try{
            if (event.events & EPOLLIN){
                recvRequest(&parseData);
                sendAnswer(&parseData);
            }
            if (event.events & EPOLLOUT){
                sendAnswer(&parseData);
            }
        }catch(const ConnectionCloseException& e){
            closeConnection(&parseData);
        }
    }
}

void Worker::closeConnection(ConnectionData *pd)
{
    epoll_ctl(_epoll, EPOLL_CTL_DEL, pd->socket,0);
    close(pd->socket);
    connections.remove(pd);
    delete pd;
}

void Worker::recvRequest(ConnectionData *pd)
{
    int socket=pd->socket;
    int recvLength;
    bool isProtocolCorrect;
    do{
        if((recvLength = read(socket, _buffer, bufferLength))>0) {
            isProtocolCorrect=parseAndExecute(_buffer,recvLength,*pd);
        }else{
            if(errno!=EINTR && errno!=EWOULDBLOCK && errno!=EAGAIN){
                throw ConnectionCloseException();
            }else{
                break;
            }
        }
        if(!isProtocolCorrect){
            throw ConnectionCloseException();
        }
    }while(true);
}


void Worker::sendAnswer(ConnectionData *pd)
{
    int sendLength=10;
    while(pd->out.size()>0){
        sendLength=write(pd->socket,pd->out.begin()->c_str(),pd->out.begin()->size());
        if(sendLength>=0){
            if(sendLength==pd->out.begin()->size()){
                pd->out.pop_front();
            }else{
                pd->out.begin()->erase(sendLength);
            }
        }else{
            if(errno!=EINTR && errno!=EWOULDBLOCK && errno!=EAGAIN){
                throw ConnectionCloseException();
            }else{
                break;
            }
        }
    }
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
