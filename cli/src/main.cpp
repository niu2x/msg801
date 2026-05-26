#include <boost/program_options.hpp>
#include <boost/version.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "msg801/lib.hpp"
#include "msg801/udp_sender.hpp"
#include "msg801/udp_server.hpp"
#include "msg801/tunnel.hpp"

namespace po = boost::program_options;

static int cmd_about()
{
    std::cout << "msg801 version: " << msg801::version() << '\n';
    std::cout << "Boost version: " << BOOST_LIB_VERSION << '\n';
    return EXIT_SUCCESS;
}

static int cmd_send(int argc, char* argv[])
{
    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "Show help")
        ("ip", po::value<std::string>()->required(), "Target IP address")
        ("port", po::value<uint16_t>()->required(), "Target port")
        ("message", po::value<std::string>()->required(), "Message to send")
    ;

    po::positional_options_description pos;
    pos.add("ip", 1);
    pos.add("port", 1);
    pos.add("message", 1);

    po::variables_map vm;
    po::store(po::command_line_parser(std::vector<std::string>(argv, argv + argc))
        .options(desc)
        .positional(pos)
        .run(), vm);

    if (vm.count("help")) {
        std::cout << "Usage: msg801 send <ip> <port> <message>\n\n" << desc << '\n';
        return EXIT_SUCCESS;
    }

    try {
      po::notify(vm);
    } catch (const po::required_option &) {
      std::cout << "Usage: msg801 send <ip> <port> <message>\n\n"
                << desc << '\n';
      return EXIT_FAILURE;
    }

    auto ip = vm["ip"].as<std::string>();
    auto port = vm["port"].as<uint16_t>();
    auto message = vm["message"].as<std::string>();

    auto result = msg801::send_udp(ip, port, message);
    if (!result.success) {
        std::cerr << "Error: " << result.error << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "Sent\n";
    return EXIT_SUCCESS;
}

static int cmd_serve(int argc, char* argv[])
{
    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "Show help")
        ("port", po::value<uint16_t>()->required(), "Listening port")
    ;

    po::positional_options_description pos;
    pos.add("port", 1);

    po::variables_map vm;
    po::store(po::command_line_parser(std::vector<std::string>(argv, argv + argc))
        .options(desc)
        .positional(pos)
        .run(), vm);

    if (vm.count("help")) {
        std::cout << "Usage: msg801 serve <port>\n\n" << desc << '\n';
        return EXIT_SUCCESS;
    }

    try {
        po::notify(vm);
    } catch (const po::required_option&) {
        std::cout << "Usage: msg801 serve <port>\n\n" << desc << '\n';
        return EXIT_FAILURE;
    }

    msg801::serve_udp(vm["port"].as<uint16_t>());
    return EXIT_SUCCESS;
}

static int cmd_tunnel(int argc, char* argv[])
{
    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "Show help")
        ("listen", po::value<std::string>()->required(), "Listen address (e.g. 0.0.0.0:8080)")
        ("remote", po::value<std::string>()->required(), "Remote address (e.g. 10.0.0.1:80)")
        ("processor", po::value<std::vector<std::string>>()->composing(),
            "Processor in pipeline order, format: name[:k=v,...] (repeatable)")
    ;

    po::variables_map vm;
    po::store(po::command_line_parser(std::vector<std::string>(argv, argv + argc))
        .options(desc)
        .run(), vm);

    if (vm.count("help")) {
        std::cout << "Usage: msg801 tunnel --listen <ip:port> --remote <ip:port>\n"
                     "       [--processor <spec>]...\n\n"
                  << desc << '\n';
        return EXIT_SUCCESS;
    }

    try {
        po::notify(vm);
    } catch (const po::required_option&) {
        std::cout << "Usage: msg801 tunnel --listen <ip:port> --remote <ip:port>\n"
                     "       [--processor <spec>]...\n\n"
                  << desc << '\n';
        return EXIT_FAILURE;
    }

    auto processors = vm.count("processor") ? vm["processor"].as<std::vector<std::string>>()
                                             : std::vector<std::string>{};
    msg801::run_tunnel(vm["listen"].as<std::string>(), vm["remote"].as<std::string>(), processors);
    return EXIT_SUCCESS;
}

static void show_help(const po::options_description& desc)
{
    std::cout << "Usage: msg801 [--help] [--about] <command>\n\n"
              << desc << "\n\n"
              << "Commands:\n"
              << "  send    Send a UDP message to ip:port\n"
              << "  serve   Listen for UDP messages on a port\n"
              << "  tunnel  TCP tunnel: listen -> forward to remote\n";
}

static int cmd_global(int argc, char* argv[])
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
        show_help(desc);
        return EXIT_SUCCESS;
    }

    if (vm.count("about")) {
        return cmd_about();
    }

    show_help(desc);

    return EXIT_SUCCESS;
}

int main(int argc, char* argv[])
{
    if (argc >= 2 && std::strcmp(argv[1], "send") == 0) {
        return cmd_send(argc - 2, argv + 2);
    }

    if (argc >= 2 && std::strcmp(argv[1], "serve") == 0) {
        return cmd_serve(argc - 2, argv + 2);
    }

    if (argc >= 2 && std::strcmp(argv[1], "tunnel") == 0) {
        return cmd_tunnel(argc - 2, argv + 2);
    }

    return cmd_global(argc, argv);
}
