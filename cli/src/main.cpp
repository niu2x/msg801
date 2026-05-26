#include <boost/version.hpp>

#include <cstdlib>
#include <iostream>

#include "CLI11.hpp"

#include "msg801/lib.hpp"
#include "msg801/udp_sender.hpp"
#include "msg801/udp_server.hpp"
#include "msg801/tunnel.hpp"

static int cmd_about()
{
    std::cout << "msg801 version: " << msg801::version() << '\n';
    std::cout << "Boost version: " << BOOST_LIB_VERSION << '\n';
    return EXIT_SUCCESS;
}

int main(int argc, char* argv[])
{
    class LinuxFormatter : public CLI::Formatter {
    public:
        LinuxFormatter()
        {
            column_width(28);
            label("SUBCOMMANDS", "COMMANDS");
        }

        std::string make_usage(const CLI::App* app, std::string name) const override
        {
            std::string u     = Formatter::make_usage(app, name);
            auto        start = u.find_first_not_of('\n');
            auto        end   = u.find_last_not_of('\n');
            if (start == std::string::npos)
                return "Usage: " + name + "\n";
            return "Usage: " + u.substr(start, end - start + 1) + "\n";
        }

        std::string make_group(std::string                     group,
                               bool                            pos,
                               std::vector<const CLI::Option*> opts) const override
        {
            if (group == "OPTIONS")
                group = "Options";
            return Formatter::make_group(group, pos, std::move(opts));
        }

        std::string make_help(const CLI::App*    app,
                              std::string        name,
                              CLI::AppFormatMode mode) const override
        {
            if (mode == CLI::AppFormatMode::Sub)
                return make_expanded(app, mode);

            std::stringstream out;
            out << make_usage(app, name);
            std::string desc = app->get_description();
            if (!desc.empty())
                out << desc << "\n";
            out << make_positionals(app);
            out << make_groups(app, mode);
            out << make_subcommands(app, mode);
            out << make_footer(app);
            return out.str();
        }
    };

    CLI::App app {};
    argv = app.ensure_utf8(argv);
    app.name("msg801");
    app.formatter(std::make_shared<LinuxFormatter>());

    bool show_about = false;
    app.add_flag("--about", show_about, "Show about");

    // --- tunnel subcommand ---
    auto* tunnel_cmd = app.add_subcommand("tunnel", "TCP tunnel: listen -> forward to remote")
                           ->group("Commands");
    std::string              tunnel_listen;
    std::string              tunnel_remote;
    std::vector<std::string> tunnel_processors;
    tunnel_cmd->add_option("--listen", tunnel_listen, "Listen address (e.g. 0.0.0.0:8080)")
        ->required();
    tunnel_cmd->add_option("--remote", tunnel_remote, "Remote address (e.g. 10.0.0.1:80)")
        ->required();
    tunnel_cmd->add_option("--processor",
                           tunnel_processors,
                           "Processor in pipeline order, format: name[:k=v,...] (repeatable)");

    // --- udp subcommand ---
    auto* udp_cmd = app.add_subcommand("udp", "UDP commands")->group("Commands");

    // --- udp send ---
    auto*       send_cmd = udp_cmd->add_subcommand("send", "Send a UDP message");
    std::string send_ip;
    uint16_t    send_port {};
    std::string send_message;
    send_cmd->add_option("ip", send_ip, "Target IP address")->required();
    send_cmd->add_option("port", send_port, "Target port")->required()->type_name("PORT");
    send_cmd->add_option("message", send_message, "Message to send")->required();

    // --- udp serve ---
    auto*    serve_cmd = udp_cmd->add_subcommand("serve", "Listen for UDP messages on a port");
    uint16_t serve_port {};
    serve_cmd->add_option("port", serve_port, "Listening port")->required()->type_name("PORT");

    CLI11_PARSE(app, argc, argv);

    if (show_about) {
        return cmd_about();
    }

    if (tunnel_cmd->parsed()) {
        msg801::run_tunnel(tunnel_listen, tunnel_remote, tunnel_processors);
        return EXIT_SUCCESS;
    }

    if (send_cmd->parsed()) {
        auto result = msg801::send_udp(send_ip, send_port, send_message);
        if (!result.success) {
            std::cerr << "Error: " << result.error << '\n';
            return EXIT_FAILURE;
        }
        std::cout << "Sent\n";
        return EXIT_SUCCESS;
    }

    if (serve_cmd->parsed()) {
        msg801::serve_udp(serve_port);
        return EXIT_SUCCESS;
    }

    std::cout << app.help();
    return EXIT_SUCCESS;
}
