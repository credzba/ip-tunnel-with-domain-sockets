#include "Worker.h"
#include <iostream>
#include <cstdio>
#include <string>
#include <time.h>

#include <boost/program_options.hpp>
#include <boost/date_time.hpp>

// socket includes
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// domain socket includes
#include <boost/asio/local/stream_protocol.hpp>

#include <sys/epoll.h>

Worker::Worker(int argc, char** argv) {
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

static const unsigned int CONTROLLEN = CMSG_LEN(sizeof(int ));

void Worker::run() {

    int epollFd = epoll_create1(0);
    epoll_event event;

    int multiConn = setupConnection();

    addToEpoll(multiConn, epollFd);

    while(1) {
        int fds = epoll_wait(epollFd, &event, 1, -1);
        if (fds < 0) {
            perror("epoll error");
            abort();
        }
        if (fds == 0) continue;

        // run through connections looking for data to read
        if (event.data.fd == multiConn) {
            int newFd = getNewFileDescriptor(event.data.fd);
            addToEpoll(newFd, epollFd);
        } else {
            // handle client data
            static const unsigned int messageLength = 200; 
            char message[messageLength];
            int in = recv(event.data.fd, &message, messageLength-1, 0);
            message[in]='\0';
            if (in > 0) {
                printf("%d:%s\n", in, message);
            } else {
                if (in <= 0) {
                    std::cout << "connection error or closed" << std::endl;
                    removeFromEpoll(event.data.fd, epollFd);
                    close(event.data.fd);
                }                        
            }
        }
    }
    close(multiConn);
}

int Worker::getNewFileDescriptor(int socket) {
    static const unsigned int MAXLINE=200;
    char            buf[MAXLINE];

    /* size of control buffer to send/recv one file descriptor */
    static const unsigned int CONTROLLEN = CMSG_LEN(sizeof(int));
    char controlBuffer[CONTROLLEN];
    static struct cmsghdr* cmptr  = reinterpret_cast<cmsghdr *>(&controlBuffer[0]);
    
    struct iovec    iov[1];
    iov[0].iov_base = buf;
    iov[0].iov_len  = sizeof(buf);

    struct msghdr   msg;
    msg.msg_iov     = iov;
    msg.msg_iovlen  = 1;
    msg.msg_name    = NULL;
    msg.msg_namelen = 0;
    msg.msg_control    = cmptr;
    msg.msg_controllen = CONTROLLEN;
    int bytesReceived = recvmsg(socket, &msg, 0);
    if (bytesReceived < 0 ) {
        printf("recvmsg error");
    } else if (bytesReceived == 0) {
        printf("connection closed by server");
        exit(0);
    }
    /*
     * See if this is the final data with null & status.  Null
     * is next to last byte of buffer; status byte is last byte.
     * Zero status means there is a file descriptor to receive.
     */
    char* ptr=&buf[0];
    int newfd = -1;
    for (ptr = buf; ptr < &buf[bytesReceived]; ) {
        if (*ptr++ == 0) {
            if (ptr != &buf[bytesReceived-1])
            printf("message format error");
            int status = *ptr & 0xFF;  /* prevent sign extension */
            if (status == 0) {
                newfd = *(int *)CMSG_DATA(cmptr);
            } else {
                newfd = -status;
            }
            bytesReceived -= 2;
        }
    }
    return newfd;
}


int Worker::setupConnection() {
    struct sockaddr_un multiConnAddr;
    multiConnAddr.sun_family = AF_UNIX;
    strcpy(multiConnAddr.sun_path, domainPath.c_str());
    
    int multiConn = socket(PF_UNIX, SOCK_STREAM, 0);
    if (multiConn < 0) {
        perror("Unable to connect to multiConnector .. terminating");
        abort();
    }
    
    int ret = connect(multiConn, reinterpret_cast<sockaddr*>(&multiConnAddr), sizeof(multiConnAddr));
    if (ret < 0) {
        perror("unable to connect to multiConnector server .. terminating");
        abort();
    }
    return multiConn;
}



void Worker::parseOptions(int argc, char** argv) {
        
    unsigned int maxClients=5;
    domainPath = "/tmp/shared.fd";

   try {
        boost::program_options::options_description desc("Options");
        desc.add_options()
        ("help", "print help messages")
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


