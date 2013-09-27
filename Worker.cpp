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
//#include <boost/asio/local/stream_protocol.hpp>
#include <linux/un.h>

#include <sys/epoll.h>

#include <openssl/bio.h> // BIO objects for I/O
#include <openssl/ssl.h> // SSL and SSL_CTX for SSL connections
#include <openssl/err.h> // Error reporting


Worker::Worker(int argc, char** argv) {
    parseOptions(argc, argv);

    /* Initializing OpenSSL */
    SSL_library_init();
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();
    
    meth = SSLv3_method();
    ctx = SSL_CTX_new(meth);
    if (NULL == ctx) {
        std::cerr << "unable to create SSL ctx" << std::endl;
        abort();
    }

    /* Load the RSA CA certificate into the SSL_CTX structure */
    /* This will allow this client to verify the server's     */
    /* certificate.                                           */
    
    const std::string RSA_SERVER_CERT("./cluster.cert");
    const std::string RSA_SERVER_KEY("./cluster.key");
    /* Load the server certificate into the SSL_CTX structure */
    if (SSL_CTX_use_certificate_file(ctx, RSA_SERVER_CERT.c_str(), SSL_FILETYPE_PEM) <= 0) {        
        ERR_print_errors_fp(stderr);
        abort();
    }
 
    /* Load the private-key corresponding to the server certificate */
    if (SSL_CTX_use_PrivateKey_file(ctx, RSA_SERVER_KEY.c_str(), SSL_FILETYPE_PEM) <= 0) {        
        ERR_print_errors_fp(stderr);
        abort();
    }
    
    /* Check if the server certificate and private-key matches */
    if (!SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr,"Private key does not match the certificate public key\n");
        abort();
    }

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
    std::stringstream registration;
    registration << identifier << " " << secure;
    send(multiConn, registration.str().c_str(), registration.str().size(), 0); 


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
            if (secure) {
                SSL* sslTemp = SSL_new(ctx);
                if (NULL == sslTemp) {
                    std::cerr << "unable to create new SSL connection" << std::endl;
                    abort();
                }
                ssl[newFd] = sslTemp;
                // flag it as server side
                SSL_set_accept_state(sslTemp);

                int err = SSL_set_fd(sslTemp, newFd);
                if (err == 0) {
                    std::cerr << "unable to set socket into ssl structure" << std::endl;
                    abort();
                }
                err = SSL_accept(sslTemp);
                while (err < 1 ) {
                    err = SSL_accept(sslTemp);
                }
                if (err == 0) {
                    std::cerr << "unable to negotiate ssl handshake" << std::endl;
                    abort();
                }
            }
        } else {
            // handle client data
            static const unsigned int messageLength = 200; 
            char message[messageLength];
            int bytesRead=0;
            if (secure) {
                bytesRead = SSL_read(ssl[event.data.fd], &message, messageLength);
            } else {
                bytesRead = recv(event.data.fd, &message, messageLength-1, 0);
            }
            if (bytesRead > 0) {
                message[bytesRead]='\0';
                printf("%d:%s\n", bytesRead, message);
            } else {
                if (bytesRead <= 0) {
                    if (secure) {
                        SSL_shutdown(ssl[event.data.fd]);
                        SSL_free(ssl[event.data.fd]);
                        ssl.erase(event.data.fd);
                    }
                    std::cout << "connection error or closed" << std::endl;
                    removeFromEpoll(event.data.fd, epollFd);
                    close(event.data.fd);
                }                        
            }
        }
    }
    close(multiConn);
    SSL_CTX_free(ctx);
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

    std::cout << "acepted new file descriptor " << newfd << std::endl; 
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
        
    domainPath = "/tmp/shared.fd";
    secure = false;
    identifier = 123456;
   try {
        boost::program_options::options_description desc("Options");
        desc.add_options()
        ("help", "print help messages")
        ("identifier,id", boost::program_options::value<unsigned int>(&identifier), "identifier used for communications")
        ("domainPath,d", boost::program_options::value<std::string>(&domainPath), "file to be used as name for domain socket")
        ("secure,s", boost::program_options::bool_switch(&secure), "use ssl for communications")
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

   if (secure) {
       std::cout << "Will serve only secure connections" << std::endl;
   } else {
       std::cout << "Will serve plain text connections" << std::endl;
   }

   return;
}


