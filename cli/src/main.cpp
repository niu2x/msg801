#include <boost/program_options.hpp>
#include <boost/version.hpp>

#include <cstdlib>
#include <iostream>

#include "msg801/lib.hpp"

namespace po = boost::program_options;

static int cmd_about()
{
    std::cout << "msg801 version: " << msg801::version() << '\n';
    std::cout << "Boost version: " << BOOST_LIB_VERSION << '\n';
    return EXIT_SUCCESS;
}

int main(int argc, char* argv[])
{
    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "Show help")
        ("about", "Show about")
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << '\n';
        return EXIT_SUCCESS;
    }

    if (vm.count("about")) {
        return cmd_about();
    }

    std::cout << msg801::greeting() << '\n';
    return EXIT_SUCCESS;
}
