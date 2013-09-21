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

static const unsigned int MAXLINE=200;


MultiConnector::MultiConnector(int argc, char** argv) {

    parseOptions(argc, argv);
}

void MultiConnector::run() {
    
    fd_set fds;
    FD_ZERO(&fds);

    setupDomainSocket();

    FD_SET(domainSocket, &fds);
    
    clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == -1) {
        perror("Socket");
        abort();
    }

    struct sockaddr_in clientConnectAddr;

    bzero(&clientConnectAddr, sizeof(struct sockaddr_in));
    clientConnectAddr.sin_family = AF_INET;
    clientConnectAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    clientConnectAddr.sin_port = htons(connectPort);

    if (-1 == bind(clientSocket, (struct sockaddr *)&clientConnectAddr, 
                   sizeof(struct sockaddr_in))) {
        perror("Unable to bind client socket");
        abort();
    }

    if (-1 == listen(clientSocket, SOMAXCONN)) {
        perror("Unable to listen on client socket");
        abort();
    }

    FD_SET(clientSocket, &fds);
    int fdMax = clientSocket;
    int numClients = 0;
    bool waitOnFirstConnection=true;
    while(waitOnFirstConnection || (numClients > 0) || (workerList.size() > 0) ) {
        fd_set readfds = fds;
        int rc = select(fdMax+1, &readfds, NULL, NULL, NULL);        
        if (rc == -1) {
            perror("Select failure");
            break;
        }
        
        // run through connections looking for data to read
        for (int i = 0; i < fdMax+1; i++) {
            if (FD_ISSET(i, &readfds)) {
                if (i == clientSocket) {
                    int newFd = getNewClientConnection(i);
                    if (newFd > 0) {
                        // if we have workers let them handle it
                        if (workerList.size() != 0) {
                            int index = numClients % workerList.size(); 
                            sendFd(newFd, workerList[index]);                        
                            close(newFd);
                        } else {
                        // else we'll handle it ourselves     
                            numClients++;
                            waitOnFirstConnection=false;
                            FD_SET(newFd, &fds);
                            if (newFd > fdMax) { fdMax = newFd; }
                        }
                    } 
                } else if (i == domainSocket) {
                    int newFd = getNewWorkerConnection(i);
                    if (newFd > 0) {
                        FD_SET(newFd, &fds);
                        if (newFd > fdMax) { fdMax = newFd; }
                        workerList.push_back(newFd);
                        waitOnFirstConnection=false;
                    }
                } else {
                    // handle client data
                    static const unsigned int messageLength = 200; 
                    char message[messageLength];
                    int in = recv(i, &message, messageLength-1, 0);
                    message[in]='\0';
                    if (in > 0) {
                        std::cout << message << std::endl;
                    } else {
                        if (in <= 0) {
                            std::cout << "connection error or closed" << std::endl;
                            close(i);
                            FD_CLR(i, &fds);
                            WorkerProcesses::iterator iter = find(workerList.begin(), workerList.end(), i);
                            if (iter != workerList.end()) {
                                workerList.erase(iter);
                            } else {
                                numClients--;
                            }
                        }                        
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
    char ipAddrStr[INET_ADDRSTRLEN];
    inet_ntop(ipv4Sock.sin_family, &clientaddr, ipAddrStr, clientaddrlen); 
    if (ipv4Sock.sin_family == PF_LOCAL) {
        std::cout << "New local connection" << std::endl;
    }
    if (ipv4Sock.sin_family == AF_INET) {
        std::cout << "New ipv4 connection from " << ipAddrStr 
                  <<  " on socket " << newFd << std::endl;
    } 
    if (ipv6Sock.sin6_family == AF_INET6) {
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
    char ipAddrStr[INET_ADDRSTRLEN];
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

void MultiConnector::setupDomainSocket() {        
    sockaddr_un domainAddr;
    domainAddr.sun_family = AF_UNIX;
    strcpy(domainAddr.sun_path, domainPath.c_str());
    unlink (domainAddr.sun_path);
    domainSocket = socket(PF_UNIX, SOCK_STREAM, 0);
    if (domainSocket < 0) {
        perror("unable to create domain socket");
        abort();
    }
    int ret = bind(domainSocket, reinterpret_cast<sockaddr*>(&domainAddr), sizeof(domainAddr));
    if (ret < 0) {
        perror("failed to bind domain socket");
        abort();
    }
    ret = listen(domainSocket, SOMAXCONN);
    if (ret < 0) {
        perror("Unable to listen on domain socket");
        abort();
    }
}
