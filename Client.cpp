#include "Client.h"
#include <iostream>
#include <cstdio>
#include <string>
#include <time.h>

#include <boost/program_options.hpp>
using namespace boost::program_options;

#include <boost/date_time.hpp>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


Client::Client(int argc, char** argv) {
    parseOptions(argc, argv);
}

void Client::run() {
    struct sockaddr_in connectorAddr;
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (sock == -1) perror("Socket");

    bzero((void *) &connectorAddr, sizeof(connectorAddr));
    connectorAddr.sin_family = AF_INET;
    connectorAddr.sin_port = htons(connectPort);
    connectorAddr.sin_addr.s_addr = inet_addr(connectorAddress.c_str());

    if (-1 == connect(sock, (struct sockaddr *)&connectorAddr, sizeof(connectorAddr))) {
        perror("Connect");
        abort();
    }

    while(1) {

        std::string message;
        std::cin >> message;
        if (message[message.size()] == '\n') {
            message[message.size()] = '\0';
        }
        send(sock, message.c_str(), message.size(), 0);
    }


    close(sock);
}


void Client::parseOptions(int argc, char** argv) {
        
    unsigned int maxClients=5;
    connectPort = 6789;
    connectorAddress = "127.0.0.1";
    try {
        options_description desc("Options");
        desc.add_options()
        ("help", "print help messages")
        ("maxClients,m", value<unsigned int>(&maxClients), "maximum clients")
        ("port,p", value<unsigned int>(&connectPort), "port to connect to")
        ("ip,i", value<std::string>(&connectorAddress), "port to connect to")
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


