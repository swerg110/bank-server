#ifndef BANK_H
#define BANK_H

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bank {
struct transaction;
class user_transactions_iterator;

class user {
public:
    explicit user(std::string name);
    [[nodiscard]] std::string name() const noexcept;
    [[nodiscard]] int balance_xts() const;

    user_transactions_iterator snapshot_transactions(
        const std::function<void(const std::vector<transaction> &, int)> &f
    ) const;

    void
    transfer(user &counterparty, int amount_xts, const std::string &comment);
    user_transactions_iterator monitor() const;

private:
    std::string name_;
    int balance_;
    std::vector<transaction> transactions_;
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_new_transaction_;

    void add_transaction(
        const user *to,
        int delta,
        const std::string &comment
    ) noexcept;
    friend class ledger;
    friend class user_transactions_iterator;
};

class ledger {
public:
    user &get_or_create_user(const std::string &name);

private:
    std::unordered_map<std::string, user> users_;
    std::mutex mutex_;
};

struct transaction {
public:
    const user *const counterparty;  // NOLINT
    const int balance_delta_xts;     // NOLINT
    const std::string comment;       // NOLINT
    transaction(const user *from, int balance_delta_xts, std::string comment)
        : counterparty(from),
          balance_delta_xts(balance_delta_xts),
          comment(std::move(comment)){};
};

class user_transactions_iterator {
public:
    user_transactions_iterator(const user *_user, std::size_t index);
    transaction wait_next_transaction();

private:
    const user *user_;
    std::size_t index_;
};

class transfer_error : public std::runtime_error {
public:
    explicit transfer_error(const std::string &msg) : std::runtime_error(msg){};
};

class not_enough_funds_error : public transfer_error {
public:
    explicit not_enough_funds_error(int current_balance, int requested_XTS)
        : transfer_error(create_message(current_balance, requested_XTS)){};

private:
    static std::string create_message(int current, int requested) {
        return "Not enough funds: " + std::to_string(current) +
               " XTS available, " + std::to_string(requested) +
               " XTS requested";
    }
};

class invalid_transfer_error : public transfer_error {
public:
    explicit invalid_transfer_error(const std::string &msg)
        : transfer_error(msg){};
};
}  // end namespace bank
#endif  // BANK_H
