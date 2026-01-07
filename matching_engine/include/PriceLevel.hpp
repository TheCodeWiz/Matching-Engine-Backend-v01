#ifndef PRICE_LEVEL_HPP
#define PRICE_LEVEL_HPP

#include <deque>
#include <memory>
#include <mutex>
#include <algorithm>
#include "Order.hpp"

class PriceLevel {
public:
    explicit PriceLevel(double price) : price_(price), totalQuantity_(0) {}

    void addOrder(std::shared_ptr<Order> order) {
        std::lock_guard<std::mutex> lock(mutex_);
        orders_.push_back(order);
        totalQuantity_ += order->getRemainingQuantity();
    }

    std::shared_ptr<Order> getFirstOrder() {
        std::lock_guard<std::mutex> lock(mutex_);
        return orders_.empty() ? nullptr : orders_.front();
    }

    void removeOrder(const std::string& orderId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(orders_.begin(), orders_.end(),
            [&orderId](const std::shared_ptr<Order>& order) {
                return order->getOrderId() == orderId;
            });
        
        if (it != orders_.end()) {
            totalQuantity_ -= (*it)->getRemainingQuantity();
            orders_.erase(it);
        }
    }

    bool isEmpty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return orders_.empty();
    }

    size_t getTotalQuantity() const {
        return totalQuantity_;
    }

    double getPrice() const {
        return price_;
    }

    const std::deque<std::shared_ptr<Order>>& getOrders() const {
        return orders_;
    }

private:
    double price_;
    std::atomic<size_t> totalQuantity_;
    std::deque<std::shared_ptr<Order>> orders_;
    mutable std::mutex mutex_;
};

#endif // PRICE_LEVEL_HPP