#include "HttpExample.h"

int main(int argc, char *argv[])
{
    HttpExample httpTest;
    httpTest.start();

    /*
    std::thread t1([&] () {
        while(true)
        {
            httpTest.run();
        }
    });
    t1.join();
    */

    while(true)
    {
        httpTest.run();
    }

    return 0;
}
