#ifndef WORKER_H
#define WORKER_H

#include <boost/program_options.hpp>
#include <string>

class Worker {
public:
    Worker(int argc, char** argv);
    void run();

private:
    // methods
    void parseOptions(int argc, char** argv);
    int setupConnection();
    int getNewFileDescriptor(int socket);

private:    
    // variables
    std::string domainPath;

    // Parsed argument values
    boost::program_options::variables_map options_map;
    
};


#endif
