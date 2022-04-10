#include "HttpExample.h"

int main(int argc, char *argv[])
{
    HttpExample httpTest;
    httpTest.start();

    while(true)
    {
        httpTest.run();
    }
    return 0;
}
