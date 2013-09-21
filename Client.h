#ifndef CLIENT_H
#define CLIENT_H

#include <boost/program_options.hpp>
#include <string>

class Client {
public:
    Client(int argc, char** argv);
    void run();

private:
    // methods
    void parseOptions(int argc, char** argv);

private:    
    // variables
    unsigned int connectPort;
    std::string connectorAddress;

    // Parsed argument values
    boost::program_options::variables_map options_map;

};



#endif
