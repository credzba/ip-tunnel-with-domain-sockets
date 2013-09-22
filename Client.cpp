#include "Client.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <time.h>

#include <boost/program_options.hpp>
#include <boost/date_time.hpp>
#include <boost/asio.hpp>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/epoll.h>


Client::Client(int argc, char** argv) {
    parseOptions(argc, argv);
}

inline void addToEpoll(int newFd, int epollFd) {
    epoll_event  event;
    event.events = EPOLLIN|EPOLLPRI|EPOLLERR;
    event.data.fd = newFd;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, newFd, &event) != 0) {
        perror("epoll_ctr add fd failed.");
        abort();
    }
}

inline void removeFromEpoll(int removedFd, int epollFd) {
    epoll_event  event;
    event.events = EPOLLIN|EPOLLPRI|EPOLLERR;
    event.data.fd = removedFd;
    if (epoll_ctl(epollFd, EPOLL_CTL_DEL, removedFd, &event) != 0) {
        perror("epoll_ctr remove fd failed.");
        abort();
    }
}

void Client::run() {
    int epollFd = epoll_create1(0);
    epoll_event event;

    addToEpoll(STDIN_FILENO, epollFd);

    sockaddr_storage connectorAddr;
    bzero((void *) &connectorAddr, sizeof(connectorAddr));    
    boost::asio::ip::address connectorIp = boost::asio::ip::address::from_string(connectorAddress) ;

    int family = AF_INET;
    if (connectorIp.is_v6()) {
        family=AF_INET6;
    }
    int sock = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1) perror("Socket");

    if (connectorIp.is_v4()) {
        sockaddr_in& sockAddr = *reinterpret_cast<sockaddr_in*>(&connectorAddr);
        sockAddr.sin_family = AF_INET;
        sockAddr.sin_port = htons(connectPort);
        inet_pton(sockAddr.sin_family, connectorAddress.c_str(), &(sockAddr.sin_addr.s_addr));
    }

    if (connectorIp.is_v6()) {
        sockaddr_in6& sockAddr = *reinterpret_cast<sockaddr_in6*>(&connectorAddr);
        sockAddr.sin6_family = AF_INET6;
        sockAddr.sin6_port = htons(connectPort);
        inet_pton(sockAddr.sin6_family, connectorAddress.c_str(), &(sockAddr.sin6_addr));
    }

    if (-1 == connect(sock, (struct sockaddr *)&connectorAddr, sizeof(connectorAddr))) {
        perror("Connect");
        abort();
    }
    addToEpoll(sock, epollFd);

    while(1) {
        int fds = epoll_wait(epollFd, &event, 1, -1);
        if (fds < 0) {
            perror("epoll error");
            abort();
        }
        if (fds == 0) continue;

        if (event.data.fd == sock) {
            // handle server information
            static const unsigned int messageLength = 200; 
            char message[messageLength];
            int in = recv(event.data.fd, &message, messageLength-1, 0);
            message[in]='\0';
            if (in > 0) {
                std::cout << message << std::endl;
            } else {
                if (in <= 0) {
                    std::cout << "connection error or closed" << std::endl;
                    close(event.data.fd);
                    break;
                }                        
            }
        }
        if (event.data.fd == STDIN_FILENO) {
            std::string message;
            std::cin >> message;
            if (message[message.size()] == '\n') {
                message[message.size()] = '\0';
            }
            send(sock, message.c_str(), message.size(), 0);
        }
    }
        
    close(sock);
}


void Client::parseOptions(int argc, char** argv) {
        
    unsigned int maxClients=5;
    connectPort = 6789;
    connectorAddress = "127.0.0.1";
    try {
        boost::program_options::options_description desc("Options");
        desc.add_options()
        ("help", "print help messages")
        ("maxClients,m", boost::program_options::value<unsigned int>(&maxClients), "maximum clients")
        ("port,p", boost::program_options::value<unsigned int>(&connectPort), "port to connect to")
        ("ip,i", boost::program_options::value<std::string>(&connectorAddress), "port to connect to")
        ;
        try {
            store(parse_command_line(argc, argv, desc), options_map);
            if ( options_map.count("help") ) {
                std::cout << desc << std::endl;
                exit(4);
            }
            notify(options_map);
        }
        catch (const boost::program_options::error& e) {
            std::cout << "some parse error " << e.what() << std::endl; 
            throw;
        }
    }
    catch (const boost::program_options::error& e) {
        std::cout << "some other parse error " << e.what() << std::endl; 
        throw;
    }    
    std::cout << "mac clients supported " << maxClients << std::endl;    
    return;
}


