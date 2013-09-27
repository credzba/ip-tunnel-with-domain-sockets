#ifndef CLIENT_H
#define CLIENT_H

#include <openssl/ossl_typ.h>
#include <openssl/ssl.h>

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
    unsigned int identifier;
    unsigned int connectPort;
    std::string connectorAddress;
    bool secure;

    // Parsed argument values
    boost::program_options::variables_map options_map;

    SSL_CTX         *ctx;
    SSL            *ssl;
    const SSL_METHOD      *meth;

};



#endif
