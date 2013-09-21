#ifndef MULTICONNECTOR_H
#define MULTICONNECTOR_H

#include <boost/program_options.hpp>
#include <string>


class MultiConnector {
public:
    MultiConnector(int argc, char** argv);
    void run();

private:
    // methods
    void parseOptions(int argc, char** argv);
    int  sendFd(int fd_to_send, int fd_of_worker);
    void setupConnector();
    int  setupDomainSocket();
    int  setupClientV4Socket();
    int  setupClientV6Socket();
    int  getNewClientConnection(int clientSocket);
    int  getNewWorkerConnection(int domainSocket);
private:
    // variables
    boost::program_options::variables_map options_map;

    // options variables
    unsigned int maxClients;    
    std::string domainPath;
    unsigned int connectPort;
    std::string clientListenAddress;

    // internal variables
    int domainSocket;
    int clientSocket;
    int v6ClientSocket;

    typedef std::vector<int> WorkerProcesses;
    WorkerProcesses workerList;

};


#endif
