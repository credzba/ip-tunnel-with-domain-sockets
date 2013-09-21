#include "Worker.h"



int main(int argc, char *argv[])
{
    Worker worker(argc, argv);

    worker.run();
    return 0;
}

