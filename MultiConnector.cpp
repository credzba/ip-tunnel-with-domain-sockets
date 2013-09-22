#include "MultiConnector.h"

#include <iostream>
#include <cstdio>
#include <string>
#include <time.h>

#include <boost/date_time.hpp>

// socket includes
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// domain socket includes
#include <linux/un.h>

#include <vector>

#include <sys/epoll.h>

static const unsigned int MAXLINE=200;


MultiConnector::MultiConnector(int argc, char** argv) {

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

void MultiConnector::run() {

    int epollFd = epoll_create1(0);
    epoll_event event;
    
    domainSocket = setupDomainSocket();
    if (domainSocket < 0 ) {
        std::cout << "Failed to create domain socket" << std::endl;
        abort();
    }

    addToEpoll(domainSocket, epollFd);

    clientSocket = setupClientV4Socket();
    if (clientSocket < 0 ) {
        std::cout << "Failed to create v4 client socket" << std::endl;
        close(domainSocket);
        abort();
    }
    addToEpoll(clientSocket, epollFd);

    v6ClientSocket = setupClientV6Socket();
    if (v6ClientSocket < 0 ) {
        std::cout << "Failed to create v6 client socket" << std::endl;
        std::cout << "Continue with v4 only" << std::endl;
    } else {
        addToEpoll(v6ClientSocket, epollFd);
    }

    int numClients = 0;
    bool waitOnFirstConnection=true;
    while(waitOnFirstConnection || (numClients > 0) || (workerList.size() > 0) ) {
        int fds = epoll_wait(epollFd, &event, 1, -1);
        if (fds < 0) {
            perror("epoll error");
            abort();
        }
        if (fds == 0) continue;

        if (event.data.fd == clientSocket) {
            int newFd = getNewClientConnection(event.data.fd);
            if (newFd > 0) {
                // if we have workers let them handle it
                if (workerList.size() != 0) {
                    int fdToAssignTo=-1;
                    int fdCount = 9999;
                    for (WorkerList::iterator iter = workerList.begin();
                         iter != workerList.end();
                         iter++) {
                        if (iter->second.useCount < fdCount) {
                            fdToAssignTo = iter->first;
                            fdCount = iter->second.useCount;
                        }
                    }
                    workerList[fdToAssignTo].useCount = fdCount+1;
                    std::stringstream reply;
                    reply << "There are " << workerList.size() << " workers, your assigned to pid " << workerList[fdToAssignTo].credentials.pid;
                    send(newFd, reply.str().c_str(), reply.str().size(), 0); 
                    sendFd(newFd, fdToAssignTo);                        
                    close(newFd);
                } else {
                    // else we'll handle it ourselves     
                    std::stringstream reply;
                    reply << "There are no workers, server will deal with you";
                    send(newFd, reply.str().c_str(), reply.str().size(), 0); 
                    numClients++;
                    waitOnFirstConnection=false;
                    addToEpoll(newFd, epollFd);
                }
            } 
        } else if (event.data.fd == v6ClientSocket) {
            int newFd = getNewClientConnection(event.data.fd);
            if (newFd > 0) {
                // if we have workers let them handle it
                if (workerList.size() != 0) {
                    int fdToAssignTo=-1;
                    int fdCount = 9999;
                    for (WorkerList::iterator iter = workerList.begin();
                         iter != workerList.end();
                         iter++) {
                        if (iter->second.useCount < fdCount) {
                            fdToAssignTo = iter->first;
                            fdCount = iter->second.useCount;
                        }
                    }
                    workerList[fdToAssignTo].useCount = fdCount+1;
                    std::stringstream reply;
                    reply << "There are " << workerList.size() << " workers, your assigned to pid " << workerList[fdToAssignTo].credentials.pid;
                    send(newFd, reply.str().c_str(), reply.str().size(), 0); 
                    sendFd(newFd, fdToAssignTo);                        
                    close(newFd);
                } else {
                    // else we'll handle it ourselves     
                    numClients++;
                    waitOnFirstConnection=false;
                    addToEpoll(newFd, epollFd);
                }
            } 
        } else if (event.data.fd == domainSocket) {
            int newFd = getNewWorkerConnection(event.data.fd);
            if (newFd > 0) {
                addToEpoll(newFd, epollFd);
                WorkerData workData;
                workData.useCount = 0;
                socklen_t ucred_length = sizeof(workData.credentials);
                if (getsockopt(newFd,
                               SOL_SOCKET,
                               SO_PEERCRED,
                               &workData.credentials,
                               &ucred_length)) {
                        std::cout << "unable to get credentials for fd " << newFd << std::endl;
                    }
                workerList[newFd] = workData;
                waitOnFirstConnection=false;
            }
        } else {
            // handle client data
            static const unsigned int messageLength = 200; 
            char message[messageLength];
            int in = recv(event.data.fd, &message, messageLength-1, 0);
            message[in]='\0';
            if (in > 0) {
                std::cout << message << std::endl;
            } else {
                if (in <= 0) {
                    std::cout << "connection error or closed" << std::endl;
                    removeFromEpoll(event.data.fd, epollFd);
                    close(event.data.fd);
                    WorkerList::iterator iter = workerList.find(event.data.fd);
                    if (iter != workerList.end()) {
                        workerList.erase(iter);
                    } else {
                        numClients--;
                    }
                }                        
            }
        }
    }

    close(clientSocket);
    close(domainSocket);
    std::cout << "All connections closed. Exiting" << std::endl;
    return;
}

int MultiConnector::getNewWorkerConnection(int domainSocket) {
    struct sockaddr_storage clientaddr;  
    int  clientaddrlen = sizeof(clientaddr);
    int newFd = accept(domainSocket,
                       (struct sockaddr *) &clientaddr,
                       (socklen_t *)&clientaddrlen);
    if (newFd < 0) return newFd;

    sockaddr_in& ipv4Sock = *reinterpret_cast<sockaddr_in*>(&clientaddr);
    sockaddr_in6& ipv6Sock = *reinterpret_cast<sockaddr_in6*>(&clientaddr);
    char ipAddrStr[INET6_ADDRSTRLEN];
    if (ipv4Sock.sin_family == PF_LOCAL) {
        std::cout << "New local connection" << std::endl;
    }
    if (ipv4Sock.sin_family == AF_INET) {
        inet_ntop(ipv4Sock.sin_family, &(ipv4Sock.sin_addr), ipAddrStr, sizeof(ipAddrStr)); 
        std::cout << "New ipv4 connection from " << ipAddrStr 
                  <<  " on socket " << newFd << std::endl;
    } 
    if (ipv6Sock.sin6_family == AF_INET6) {
        inet_ntop(ipv6Sock.sin6_family, &(ipv6Sock.sin6_addr), ipAddrStr, sizeof(ipAddrStr)); 
        std::cout << "New ipv6 connection from " << ipAddrStr 
                  <<  " on socket " << newFd << std::endl;
    } 
    return newFd;
}




int MultiConnector::getNewClientConnection(int clientSocket) {
    struct sockaddr_storage clientaddr;  
    int  clientaddrlen = sizeof(clientaddr);
    int newFd = accept(clientSocket,
                       (struct sockaddr *) &clientaddr,
                       (socklen_t *)&clientaddrlen);
    if (newFd == -1) perror("Accept");
    
    sockaddr_in& ipv4Sock = *reinterpret_cast<sockaddr_in*>(&clientaddr);
    sockaddr_in6& ipv6Sock = *reinterpret_cast<sockaddr_in6*>(&clientaddr);
    char ipAddrStr[INET6_ADDRSTRLEN];
    if (ipv4Sock.sin_family == PF_LOCAL) {
        std::cout << "New local connection" << std::endl;
    }
    if (ipv4Sock.sin_family == AF_INET) {
        inet_ntop(ipv4Sock.sin_family, &(ipv4Sock.sin_addr), ipAddrStr, sizeof(ipAddrStr)); 
        std::cout << "New ipv4 connection from " << ipAddrStr 
                  <<  " on socket " << newFd << std::endl;
    } 
    if (ipv6Sock.sin6_family == AF_INET6) {
        inet_ntop(ipv6Sock.sin6_family, &(ipv6Sock.sin6_addr), ipAddrStr, sizeof(ipAddrStr)); 
        std::cout << "New ipv6 connection from " << ipAddrStr 
                  <<  " on socket " << newFd << std::endl;
    } 
 
    return newFd;
}


int MultiConnector::sendFd(int fd_to_send, int fd_of_worker)
{
    struct iovec    iov[1];
    struct msghdr   msg;
    char            buf[2]; /* send_fd()/recv_fd() 2-byte protocol */
    
    iov[0].iov_base = buf;
    iov[0].iov_len  = 2;
    msg.msg_iov     = iov;
    msg.msg_iovlen  = 1;
    msg.msg_name    = NULL;
    msg.msg_namelen = 0;
    if (fd_to_send < 0) {
        msg.msg_control    = NULL;
        msg.msg_controllen = 0;
        buf[1] = -fd_to_send;   /* nonzero status means error */
        if (buf[1] == 0)
        buf[1] = 1; /* -256, etc. would screw up protocol */
    } else {
        /* size of control buffer to send/recv one file descriptor */
        static const unsigned int CONTROLLEN = CMSG_LEN(sizeof(int));
        char controlMsgBuffer[CONTROLLEN];
        cmsghdr* cmptr = reinterpret_cast<cmsghdr *>(&controlMsgBuffer[0]);
        cmptr->cmsg_level  = SOL_SOCKET;
        cmptr->cmsg_type   = SCM_RIGHTS;
        cmptr->cmsg_len    = CONTROLLEN;
        msg.msg_control    = cmptr;
        msg.msg_controllen = CONTROLLEN;
        *(int *)CMSG_DATA(cmptr) = fd_to_send;     /* the fd to pass */
        buf[1] = 0;          /* zero status means OK */
    }
    buf[0] = 0;              /* null byte flag to recv_fd() */
    if (sendmsg(fd_of_worker, &msg, 0) != 2)
        return(-1);
    return(0);
}




void MultiConnector::parseOptions(int argc, char** argv) {
    
    // default values
    maxClients=5;
    domainPath = "/tmp/shared.fd";
    connectPort = 6789;
    try {
        boost::program_options::options_description desc("Options");
        desc.add_options()
        ("help", "print help messages")
        ("maxClients,m", boost::program_options::value<unsigned int>(&maxClients), "maximum clients")
        ("port,p", boost::program_options::value<unsigned int>(&connectPort), "port to connect to")
        ("domainPath,d", boost::program_options::value<std::string>(&domainPath), "file to be used as name for domain socket")
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

int MultiConnector::setupDomainSocket() {        
    sockaddr_un domainAddr;
    domainAddr.sun_family = AF_UNIX;
    strcpy(domainAddr.sun_path, domainPath.c_str());
    unlink (domainAddr.sun_path);
    int dSocket = socket(PF_UNIX, SOCK_STREAM, 0);
    if (dSocket < 0) {
        perror("unable to create domain socket");
        return dSocket;
    }
    int ret = bind(dSocket, reinterpret_cast<sockaddr*>(&domainAddr), sizeof(domainAddr));
    if (ret < 0) {
        perror("failed to bind domain socket");
        return -1;
    }
    ret = listen(dSocket, SOMAXCONN);
    if (ret < 0) {
        perror("Unable to listen on domain socket");
        return -1;
    }
    return dSocket;
}

int MultiConnector::setupClientV6Socket() {
    int cSocket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (cSocket == -1) {
        perror("Socket");
        return cSocket;
    }

    int on = 1;
    if (-1 == setsockopt(cSocket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&on, sizeof(on))) {
        perror("unable to set IPV6_ONLY socket option");
        // attempt to continue
    }

    sockaddr_in6 clientConnectAddr;
    bzero(&clientConnectAddr, sizeof(struct sockaddr_in6));

    clientConnectAddr.sin6_family = AF_INET6;
    clientConnectAddr.sin6_port = htons(connectPort);
    clientConnectAddr.sin6_addr = in6addr_any;

    if (-1 == bind(cSocket, (struct sockaddr *)&clientConnectAddr, 
                   sizeof(clientConnectAddr))) {
        perror("Unable to bind ipv6 client socket");
        return -1;
    }

    if (-1 == listen(cSocket, SOMAXCONN)) {
        perror("Unable to listen on ipv6 client socket");
        return -1;
    }
    return cSocket;
}

int MultiConnector::setupClientV4Socket() {
    int cSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (cSocket == -1) {
        perror("Socket");
        return cSocket;
    }

    sockaddr_in clientConnectAddr;

    bzero(&clientConnectAddr, sizeof(struct sockaddr_in));
    clientConnectAddr.sin_family = AF_INET;
    clientConnectAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    clientConnectAddr.sin_port = htons(connectPort);

    if (-1 == bind(cSocket, (struct sockaddr *)&clientConnectAddr, 
                   sizeof(struct sockaddr_in))) {
        perror("Unable to bind ipv4 client socket");
        return -1;
    }

    if (-1 == listen(cSocket, SOMAXCONN)) {
        perror("Unable to listen on ipv4 client socket");
        return -1;
    }
    return cSocket;
}
