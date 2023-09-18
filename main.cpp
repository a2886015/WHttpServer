#include "HttpExample.h"

int main(int argc, char *argv[])
{
    HttpExample httpTest;
    httpTest.start();

    /*
    std::thread t1([&] () {
        while(true)
        {
            httpTest.run(2);
        }
    });
    t1.join();
    */

    while(true)
    {
        httpTest.run(2);
    }

    return 0;
}
