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

namespace Afina {
namespace Network {
namespace NonBlocking {

struct ParseData{
    size_t recvLength,readRecvLength,parsedLength,readBodyLength;
    uint32_t bodyLength;
    bool isBody=false,isErrorLine=false;
    std::string dataBuffer;
    Afina::Protocol::Parser parser;
    std::unique_ptr<Execute::Command> command;
    std::string outBuffer;

    ParseData(){
        recvLength=readRecvLength=parsedLength=readBodyLength=0;
        bodyLength=0;
        isBody=false;
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

void Worker::ParseAndExecute(char *buffer, ParseData& pd)
{
    std::string out;
    pd.readRecvLength=0;
    while(pd.readRecvLength<pd.recvLength){
        //In case of error in the command skip all characters before EOL
        if(pd.isErrorLine){
            for(pd.readRecvLength;pd.readRecvLength<pd.recvLength && buffer[pd.readRecvLength]!='\n';++pd.readRecvLength);
            if(pd.readRecvLength<pd.recvLength && buffer[pd.readRecvLength]=='\n') pd.isErrorLine=false;
            pd.readRecvLength++;
            continue;
        }

        if(pd.isBody){
            //Get maximum number of body's characters which can be readed
            size_t temp=std::min(pd.bodyLength-pd.readBodyLength+2,pd.recvLength-pd.readRecvLength);
            std::copy(buffer+pd.readRecvLength,buffer+pd.readRecvLength+temp,std::back_inserter(pd.dataBuffer));
            pd.readBodyLength+=temp;
            pd.readRecvLength+=temp;
            //If body is readed we'll try to execute command
            if(pd.readBodyLength==pd.bodyLength+2){
                pd.isBody=false;
                pd.readBodyLength=0;
                try{
                    std::string temp;
                    pd.command->Execute(*_pStorage,pd.dataBuffer,temp);
                    out +=temp+ "\r\n";
                }catch(std::exception &e){
                    out+=std::string("SERVER_ERROR ")+e.what()+"\r\n";
                }
                pd.dataBuffer.clear();
            }
        }else{
            bool res;
            try{
                res=pd.parser.Parse(buffer+pd.readRecvLength, pd.recvLength-pd.readRecvLength, pd.parsedLength);
            }catch(std::runtime_error& e){
                out+=std::string("SERVER ERROR ")+e.what()+"\r\n";
                pd.isErrorLine=true;
                continue;
            }
            //if command type successfully parsed
            if(res){
                pd.command=pd.parser.Build(pd.bodyLength);
                pd.parser.Reset();
                if(pd.bodyLength==0){
                    try{
                        std::string temp;
                        pd.command->Execute(*_pStorage,std::string(),temp);
                        out += temp+"\r\n";
                    }catch(std::exception &e){
                        out+=std::string("SERVER_ERROR ")+e.what()+"\r\n";
                    }
                }else{
                    pd.isBody=true;
                }
            }
            pd.readRecvLength+=pd.parsedLength;
            if(res)pd.parsedLength=0;
        }
    }
    pd.outBuffer+=out;
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

    _servedSocketsCount=1;

    while(!(_isStop.load())){
        //wait for 1 second
        int sock_event_count=epoll_wait(_epoll, outEvents, maxEvents, 1000);

        if(sock_event_count==-1 && errno!=EINTR) throw std::runtime_error("Epoll wait error");

        for(int i=0;i<sock_event_count;++i){
            try {
                handleEvent(outEvents[i]);
            } catch(std::exception& e) {
                std::cout << e.what() << std::endl;
            }
        }
    }

    for(auto it=_parseInfo.begin();it!=_parseInfo.end();++it){
        close(it->first);
    }
    close(server_socket);
}

void Worker::handleEvent(struct epoll_event &event)
{
    if(event.data.fd==_serverSocket){
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
            if (_servedSocketsCount == maxEvents){
                std::cout << "Event array is full" << std::endl;
                close(client_socket);
                return;
            }

            make_socket_non_blocking(client_socket);

            struct epoll_event client_sock_event;
            client_sock_event.events=EPOLLET | EPOLLIN | EPOLLOUT;
            client_sock_event.data.fd=client_socket;

            if(epoll_ctl(_epoll, EPOLL_CTL_ADD, client_socket, &client_sock_event) == -1) {
                close(client_socket);
                std::cout<<"Could not add to epoll socket"<<std::endl;
                return;
            }

            _parseInfo[client_socket]=ParseData();
            ++_servedSocketsCount;
        }
    }else{
        int client_socket=event.data.fd;
        ParseData &parseData=_parseInfo[client_socket];
        if (event.events & EPOLLIN){
            if((parseData.recvLength = read(client_socket, _buffer, bufferLength)) >0) {
                ParseAndExecute(_buffer,parseData);
                while(parseData.outBuffer.size()>0){
                    size_t sended=write(client_socket,parseData.outBuffer.c_str(),parseData.outBuffer.size());
                    if(sended>0) parseData.outBuffer.erase(0,sended);
                    else break;
                }
            }else if(parseData.recvLength!=EINTR && parseData.recvLength!=EWOULDBLOCK && parseData.recvLength!=EAGAIN){
                epoll_ctl(_epoll, EPOLL_CTL_DEL, client_socket,0);
                --_servedSocketsCount;
                close(client_socket);
                _parseInfo.erase(client_socket);
            }
        }
        if (event.events & EPOLLOUT){
            while(parseData.outBuffer.size()){
                size_t sended=write(client_socket,parseData.outBuffer.c_str(),parseData.outBuffer.size());
                if(sended>0)parseData.outBuffer.erase(0,sended);
                else break;
            }
        }

        if((event.events & EPOLLHUP)|| (event.events & EPOLLERR)){
            epoll_ctl(_epoll, EPOLL_CTL_DEL, client_socket,0);
            --_servedSocketsCount;
            close(client_socket);
            _parseInfo.erase(client_socket);
        }
    }

}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
