#ifndef WORKER_H
#define WORKER_H

#include <openssl/ossl_typ.h>
#include <openssl/ssl.h>

#include <boost/program_options.hpp>
#include <string>
#include <map>

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
    bool secure;

    // Parsed argument values
    boost::program_options::variables_map options_map;

    SSL_CTX         *ctx;
    std::map<int, SSL*> ssl;
    const SSL_METHOD      *meth;
    //X509            *server_cert;
    //EVP_PKEY        *pkey;

};


#endif
