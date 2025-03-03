#include "bank.hpp"
#include <algorithm>
#include <string>

bank::user::user(std::string name) : name_(std::move(name)), balance_(100) {
    const std::unique_lock lock(mutex_);
    add_transaction(nullptr, 100, "Initial deposit for " + name_);
}

std::string bank::user::name() const noexcept {
    return name_;
}

int bank::user::balance_xts() const {
    const std::unique_lock lock(mutex_);
    return balance_;
}

void bank::user::add_transaction(
    const bank::user *to,
    int delta,
    const std::string &comment
) noexcept {
    transactions_.emplace_back(to, delta, comment);
    cv_new_transaction_.notify_all();
}

void bank::user::transfer(
    bank::user &counterparty,
    int amount_xts,
    const std::string &comment
) {
    if (this == &counterparty) {
        throw invalid_transfer_error("Self-transfer");
    }
    if (amount_xts < 0) {
        throw invalid_transfer_error("Negative amount, you're lose:(");
    }

    const std::scoped_lock lock(mutex_, counterparty.mutex_);

    const int new_user_amount = balance_ - amount_xts;
    if (new_user_amount < 0) {
        throw not_enough_funds_error(balance_, amount_xts);
    }

    balance_ -= amount_xts;
    add_transaction(&counterparty, -amount_xts, comment);
    counterparty.balance_ += amount_xts;
    counterparty.add_transaction(this, amount_xts, comment);
}

bank::user_transactions_iterator bank::user::snapshot_transactions(
    const std::function<void(const std::vector<transaction> &, int)> &f
) const {
    const std::unique_lock lock(mutex_);
    f(transactions_, balance_);
    return bank::user_transactions_iterator{this, transactions_.size()};
}

bank::user_transactions_iterator bank::user::monitor() const {
    const std::unique_lock lock(mutex_);
    return bank::user_transactions_iterator{this, transactions_.size()};
}

bank::user &bank::ledger::get_or_create_user(const std::string &name) {
    const std::unique_lock lock(mutex_);
    if (users_.contains(name)) {
        return users_.at(name);
    } else {
        users_.emplace(
            std::piecewise_construct, std::tuple{name}, std::tuple{name}
        );
        return users_.at(name);
    }
}

bank::user_transactions_iterator::user_transactions_iterator(
    const user *_user,
    std::size_t index
)
    : user_(_user), index_(index){};

bank::transaction bank::user_transactions_iterator::wait_next_transaction() {
    std::unique_lock lock(user_->mutex_);
    user_->cv_new_transaction_.wait(lock, [this] {
        return index_ < user_->transactions_.size();
    });
    return user_->transactions_[index_++];
}
