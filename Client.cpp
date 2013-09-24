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


Client::Client(int argc, char** argv) {
    parseOptions(argc, argv);
}

void Client::run() {
    fd_set fds;
    FD_ZERO(&fds);

    FD_SET(STDIN_FILENO, &fds);
    int fdMax = STDIN_FILENO;

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
    FD_SET(sock, &fds);
    if (sock > fdMax) { fdMax = sock; }
    while(1) {
        fd_set readfds = fds;
        int rc = select(fdMax+1, &readfds, NULL, NULL, NULL);        
        if (rc == -1) {
            perror("Select failure");
            break;
        }
        
        // run through connections looking for data to read
        for (int i = 0; i < fdMax+1; i++) {
            if (FD_ISSET(i, &readfds)) {
                if (i  == sock) {
                    // handle server information
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
                            break;
                        }
                    }
                }
                if (i == STDIN_FILENO) {
                    std::string message;
                    std::cin >> message;
                    if (message[message.size()] == '\n') {
                        message[message.size()] = '\0';
                    }
                    send(sock, message.c_str(), message.size(), 0);
                }
            }
        }
    }
        
    close(sock);
}


void Client::parseOptions(int argc, char** argv) {
        
    unsigned int maxClients=5;
    connectPort = 6789;
    connectorAddress = "127.0.0.1";
    secure=false;
    try {
        boost::program_options::options_description desc("Options");
        desc.add_options()
        ("help", "print help messages")
        ("maxClients,m", boost::program_options::value<unsigned int>(&maxClients), "maximum clients")
        ("port,p", boost::program_options::value<unsigned int>(&connectPort), "port to connect to")
        ("ip,i", boost::program_options::value<std::string>(&connectorAddress), "port to connect to")
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
    std::cout << "mac clients supported " << maxClients << std::endl;    
    return;
}


