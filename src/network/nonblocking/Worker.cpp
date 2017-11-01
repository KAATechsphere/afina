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

#include <map>
#include <memory>

#include "Utils.h"

#include "../../protocol/Parser.h"
#include "afina/execute/Command.h"

namespace Afina {
namespace Network {
namespace NonBlocking {

const size_t dataBufferLength=0x100000+10;

struct ParseData{
    ParseData(){
        recvLength=readRecvLength=parsedLength=readBodyLength=0;
        bodyLength=0;
        isBody=false;
        dataBuffer.reset(new char[dataBufferLength]);
    }
    size_t recvLength,readRecvLength,parsedLength,readBodyLength;
    uint32_t bodyLength;
    bool isBody=false,isErrorLine=false;
    std::shared_ptr<char> dataBuffer;
    Afina::Protocol::Parser parser;
    std::unique_ptr<Execute::Command> command;
};

struct WorkerNet{
    Worker *worker;
    struct sockaddr_in sin;
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

    if (bind(server_socket, (struct sockaddr *)&(wn.sin), sizeof(wn.sin)) == -1) {
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

std::string Worker::ParseAndExecute(char *buffer, ParseData& parseData)
{
    std::string out;
    while(parseData.readRecvLength<parseData.recvLength){
        if(parseData.isErrorLine){
            for(parseData.readRecvLength;parseData.readRecvLength<parseData.recvLength && buffer[parseData.readRecvLength]!='\n';++parseData.readRecvLength);
            if(parseData.readRecvLength<parseData.recvLength && buffer[parseData.readRecvLength]=='\n') parseData.isErrorLine=false;
            parseData.readRecvLength++;
            continue;
        }
        if(parseData.isBody){
            size_t temp=std::min(parseData.bodyLength-parseData.readBodyLength+2,parseData.recvLength-parseData.readRecvLength);
            std::copy(buffer+parseData.readRecvLength,buffer+parseData.readRecvLength+temp,parseData.dataBuffer.get()+parseData.readBodyLength);
            parseData.readBodyLength+=temp;
            parseData.readRecvLength+=temp;
            if(parseData.readBodyLength==parseData.bodyLength+2){
                parseData.isBody=false;
                parseData.readBodyLength=0;
                try{
                    std::string temp;
                    std::cout<<"hmm: "<<parseData.dataBuffer.get()<<std::endl;
                    parseData.command->Execute(*pStorage,std::string(parseData.dataBuffer.get(),parseData.dataBuffer.get()+parseData.bodyLength),temp);
                    out +=temp+ "\r\n";
                }catch(std::exception &e){
                    out+=std::string("SERVER_ERROR ")+e.what()+"\r\n";
                }
            }
        }else{
            bool res;
            try{
                res=parseData.parser.Parse(buffer+parseData.readRecvLength, parseData.recvLength-parseData.readRecvLength, parseData.parsedLength);
            }catch(std::runtime_error& e){
                out+=std::string("SERVER ERROR ")+e.what()+"\r\n";
                parseData.isErrorLine=true;
                continue;
            }
            if(res){
                parseData.command=parseData.parser.Build(parseData.bodyLength);
                parseData.parser.Reset();
                if(parseData.bodyLength==0){
                    try{
                        std::string temp;
                        parseData.command->Execute(*pStorage,std::string(),temp);
                        out += temp+"\r\n";
                    }catch(std::exception &e){
                        out+=std::string("SERVER_ERROR ")+e.what()+"\r\n";
                    }
                }else parseData.isBody=true;
            }
            parseData.readRecvLength+=parseData.parsedLength;
            if(res)parseData.parsedLength=0;
        }
    }
    return out;
}

// See Worker.h
Worker::Worker(std::shared_ptr<Afina::Storage> ps):pStorage(ps),isStop(true) {
    // TODO: implementation here
}

// See Worker.h
Worker::~Worker() {
    // TODO: implementation here
}

// See Worker.h
void Worker::Start(int serv_socket) {
    //std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(serv_socket, (struct sockaddr *)&sin, &len) == -1)
        throw std::runtime_error("Error get socket info");

    pthread_t worker_thread;
    WorkerNet *wn=new WorkerNet();
    wn->sin=sin;
    wn->worker=this;

    if ((thread=pthread_create(&worker_thread, NULL, Worker::OnRunProxy, wn)) < 0) {
        delete wn;
        throw std::runtime_error("Could not create server thread");
    }
}

// See Worker.h
void Worker::Stop() {
    isStop=true;
    // TODO: implementation here
}

// See Worker.h
void Worker::Join() {
    pthread_join(thread, 0);
}

// See Worker.h
void Worker::OnRun(int server_socket) {
    //std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    isStop=false;

    int epoll_fd;
    if((epoll_fd = epoll_create1(0)) == -1) {
        close(server_socket);
        throw std::runtime_error("Could not create epoll");
    }

    struct epoll_event event;
    event.events=EPOLLIN|EPOLLOUT;
    event.data.fd=server_socket;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &event) == -1) {
        close(server_socket);
        throw std::runtime_error("Could not add to epoll socket");
    }

    const int max_events=100;
    struct epoll_event outEvents[max_events];
    std::map<int,ParseData> parseInfo;

    int sock_count=1;

    const size_t bufferLength=100000;
    char buffer[bufferLength];

    while(!isStop){
        //wait for 1 second
        int sock_event_count=epoll_wait(epoll_fd, outEvents, max_events, 1000);

        if(sock_event_count==-1 && errno!=EINTR) throw std::runtime_error("Epoll wait error");

        for(int i=0;i<sock_event_count;++i){
            if(outEvents[i].data.fd==server_socket){
                struct sockaddr in_addr;
                socklen_t in_len = sizeof(in_addr);
                int client_socket = -1;

                if((client_socket = accept(server_socket, &in_addr, &in_len)) == -1) {
                    std::cout<<"Error accept socket"<<std::endl;
                    close(client_socket);
                    break;
                }
                if (sock_count == max_events){
                    std::cout << "Event array is full" << std::endl;
                    close(client_socket);
                    break;
                }

                make_socket_non_blocking(client_socket);

                struct epoll_event client_sock_event;
                client_sock_event.events=EPOLLIN | EPOLLOUT | EPOLLRDHUP;
                client_sock_event.data.fd=client_socket;

                if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &client_sock_event) == -1) {
                    close(client_socket);
                    std::cout<<"Could not add to epoll socket"<<std::endl;
                    break;
                }
                parseInfo[client_socket]=ParseData();
                ++sock_count;
            }else{
                int client_socket=outEvents[i].data.fd;
                ParseData &parseData=parseInfo[client_socket];
                if (outEvents[i].events & EPOLLIN){
                    if((parseData.recvLength = read(client_socket, buffer, bufferLength)) >0) {
                        std::string out=ParseAndExecute(buffer,parseInfo[client_socket]);
                        if(out.size())write(client_socket,out.c_str(),out.size());
                    }else{
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, 0);
                        --sock_count;
                        close(client_socket);
                        parseInfo.erase(client_socket);
                    }
                }
                if(outEvents[i].events & EPOLLRDHUP){
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket,0);
                    --sock_count;
                    close(client_socket);
                    parseInfo.erase(client_socket);
                }
            }
        }
    }


    for(auto it=parseInfo.begin();it!=parseInfo.end();++it){
        close(it->first);
    }
    close(server_socket);
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
