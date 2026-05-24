#include <cstdlib>
#include <iostream>

#include "msg801/lib.hpp"

int main()
{
    std::cout << msg801::greeting() << '\n';
    return EXIT_SUCCESS;
}
