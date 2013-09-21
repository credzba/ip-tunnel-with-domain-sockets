#include "Worker.h"
#include <iostream>
#include <cstdio>
#include <string>
#include <time.h>

#include <boost/program_options.hpp>
using namespace boost::program_options;

#include <boost/date_time.hpp>

// socket includes
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// domain socket includes
#include <linux/un.h>

Worker::Worker(int argc, char** argv) {
    parseOptions(argc, argv);
}

static const unsigned int CONTROLLEN = CMSG_LEN(sizeof(int ));

void Worker::run() {

    fd_set fds;
    FD_ZERO(&fds);

    int multiConn = setupConnection();

    FD_SET(multiConn, &fds);
    int fdMax = multiConn;

    while(1) {
        fd_set readfds = fds;
        int rc = select(fdMax+1, &readfds, NULL, NULL, NULL);
        if (rc == -1) {
            perror("Select");
            break;
        }

        // run through connections looking for data to read
        for (int i = 0; i < fdMax+1; i++) {
            if (FD_ISSET(i, &readfds)) {
                if (i == multiConn) {
                    int newFd = getNewFileDescriptor(i);
                    FD_SET(newFd, &fds);
                    if (newFd > fdMax) { fdMax = newFd; }
                } else {
                    // handle client data
                    static const unsigned int messageLength = 200; 
                    char message[messageLength];
                    int in = recv(i, &message, messageLength-1, 0);
                    message[in]='\0';
                    if (in > 0) {
                        printf("%d:%s\n", in, message);
                    } else {
                        if (in <= 0) {
                            std::cout << "connection error or closed" << std::endl;
                            close(i);
                            FD_CLR(i, &fds);
                        }                        
                    }
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
        options_description desc("Options");
        desc.add_options()
        ("help", "print help messages")
        ("domainPath,d", value<std::string>(&domainPath), "file to be used as name for domain socket")
        ;
        try {
            store(parse_command_line(argc, argv, desc), options_map);
            if ( options_map.count("help") ) {
                std::cout << desc << std::endl;
                exit(4);
            }
            notify(options_map);
        }
        catch (const error& e) {
            std::cout << "some parse error " << e.what() << std::endl; 
            throw;
        }
    }
    catch (const error& e) {
        std::cout << "some other parse error " << e.what() << std::endl; 
        throw;
    }    
    std::cout << "mac clients supported " << maxClients << std::endl;    
    return;
}


