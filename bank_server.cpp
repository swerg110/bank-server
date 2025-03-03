#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include "bank.hpp"
#include "boost/asio.hpp"
using boost::asio::ip::tcp;

enum class Commands {
    BALANCE,
    TRANSACTIONS,
    MONITOR,
    TRANSFER,
    BAD_COMMAND

};
const std::unordered_map<std::string, Commands> command_map = {
    {"balance", Commands::BALANCE},
    {"transactions", Commands::TRANSACTIONS},
    {"monitor", Commands::MONITOR},
    {"transfer", Commands::TRANSFER}};

static Commands get_command(const std::string &cmd) {
    auto it = command_map.find(cmd);
    if (it != command_map.end()) {
        return it->second;
    }
    return Commands::BAD_COMMAND;
};

namespace bank {
class client_connection {
public:
    client_connection(  // NOLINT(cppcoreguidelines-pro-type-member-init)
        tcp::socket socket,
        ledger &ledger
    )
        : client_(std::move(socket)), ledger_(ledger) {
    }

    void run() {
        const auto remote_ep = client_.socket().remote_endpoint();
        const auto local_ep = client_.socket().local_endpoint();
        std::cout << "Connected " << remote_ep << " --> " << local_ep << '\n';

        authentication();
        std::string command;  //
        while (std::getline(client_, command)) {
            std::istringstream iss(command);
            std::string cmd;
            iss >> cmd;
            switch (get_command(cmd)) {
                case Commands::BALANCE:
                    client_ << user_->balance_xts() << '\n' << std::flush;
                    break;
                case Commands::TRANSACTIONS: {
                    std::size_t n;  // NOLINT(cppcoreguidelines-init-variables)
                    iss >> n;
                    get_transactions(n);
                } break;
                case Commands::MONITOR: {
                    std::size_t n;  // NOLINT(cppcoreguidelines-init-variables)
                    iss >> n;
                    monitor(n);
                } break;
                case Commands::TRANSFER: {
                    std::string counterparty;
                    std::string comment;
                    int amount;  // NOLINT(cppcoreguidelines-init-variables)
                    iss >> counterparty >> amount;
                    std::getline(iss, comment);
                    transfer(counterparty, amount, comment);
                } break;
                case Commands::BAD_COMMAND:
                    client_ << "Unknown command: '" << cmd << "'\n"
                            << std::flush;
            }
        }
        std::cout << "Disconnected " << remote_ep << " --> " << local_ep
                  << '\n';
    }

private:
    tcp::iostream client_;
    ledger &ledger_;
    user *user_ = nullptr;

    void authentication() {
        std::string name;
        client_ << "What is your name?\n" << std::flush;
        std::getline(client_, name);
        user_ = &ledger_.get_or_create_user(name);
        client_ << "Hi " << name << '\n' << std::flush;
    }

    void get_transactions(std::size_t n) {
        user_->snapshot_transactions([&](const auto &transactions,
                                         int balance) {
            client_ << "CPTY\tBAL\tCOMM\n";
            const int start =
                std::max(static_cast<int>(transactions.size() - n), 0);
            for (int i = start; i < static_cast<int>(transactions.size());
                 i++) {
                const bank::transaction &cur_transaction = transactions[i];
                client_ << (cur_transaction.counterparty == nullptr
                                ? "-"
                                : cur_transaction.counterparty->name())
                        << "\t" << cur_transaction.balance_delta_xts << "\t"
                        << cur_transaction.comment << "\n";
            }

            client_ << "===== BALANCE: " << balance << " XTS =====\n"
                    << std::flush;
        });
    }

    void monitor(std::size_t n) {
        get_transactions(n);
        auto it = user_->monitor();
        while (true) {
            const auto &cur_transaction = it.wait_next_transaction();
            client_ << (cur_transaction.counterparty == nullptr
                            ? "-"
                            : cur_transaction.counterparty->name())
                    << "\t" << cur_transaction.balance_delta_xts << "\t"
                    << cur_transaction.comment << "\n";
        }
    }

    void transfer(
        const std::string &counterparty,
        int amount,
        std::string &comment
    ) {
        if (!comment.empty() && comment[0] == ' ') {
            comment = comment.substr(1);
        }
        auto &to = ledger_.get_or_create_user(counterparty);
        try {
            user_->transfer(to, amount, comment);
            client_ << "OK\n" << std::flush;
        } catch (bank::transfer_error &e) {
            client_ << e.what() << '\n' << std::flush;
        }
    }
};

class server {
public:
    server(  // NOLINT(cppcoreguidelines-pro-type-member-init)
        boost::asio::io_context &io_context,
        unsigned short port
    )
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
    }

    void setup(  // NOLINT(readability-convert-member-functions-to-static)
        const std::string &port_file
    ) {
        try {
            std::ofstream f(port_file);
            f << acceptor_.local_endpoint().port();
        } catch (...) {
            std::cerr << "Unable to store port to file " << port_file << '\n';
            return;
        }
    };

    void run() {
        std::cout << "Listening at " << acceptor_.local_endpoint() << '\n';
        while (true) {
            tcp::socket socket = acceptor_.accept();  // NOLINT
            std::thread([socket = std::move(socket), this]() mutable {
                client_connection session(std::move(socket), ledger_);
                session.run();
            }).detach();
        }
    }

private:
    tcp::acceptor acceptor_;
    ledger ledger_;
};
}  // namespace bank

int main(int argc, char *argv[]) {
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    if (argc != 3) {
        std::cerr << "You're lose, seems in PMI3";
        return 1;
    }
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const int port = std::stoi(argv[1]);
    const std::string port_file = argv[2];
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    boost::asio::io_context io_context;  // NOLINT
    bank::server server(io_context, static_cast<unsigned short>(port));
    server.setup(port_file);
    server.run();
}
