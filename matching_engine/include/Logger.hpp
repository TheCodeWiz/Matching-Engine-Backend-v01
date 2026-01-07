#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <fstream>
#include <mutex>
#include <string>
#include "Order.hpp"
#include "Trade.hpp"

class Logger {
public:
    Logger(const std::string& filename)
        : file_(filename, std::ios::app)
    {}

    void logOrder(const Order& order) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Format: timestamp, order-id, type, side, price, qty-placed, order-status, qty-rem, user-id
        file_ << std::chrono::system_clock::to_time_t(order.getTimestamp()) << ","
              << order.getOrderId() << ","
              << (order.getType() == OrderType::LIMIT ? "LIMIT" : "MARKET") << ","
              << (order.getSide() == OrderSide::BUY ? "BUY" : "SELL") << ","
              << order.getPrice() << ","
              << order.getQuantity() << ",";
        // Status
        switch (order.getStatus()) {
            case OrderStatus::NEW: file_ << "NEW"; break;
            case OrderStatus::PARTIALLY_FILLED: file_ << "PARTIAL"; break;
            case OrderStatus::FILLED: file_ << "FILLED"; break;
            case OrderStatus::CANCELLED: file_ << "CANCELLED"; break;
            case OrderStatus::EXPIRED: file_ << "EXPIRED"; break;
        }
        file_ << "," << order.getRemainingQuantity() << ",1234567890" << std::endl;
    }

    void logTrade(const Trade& trade) {
        std::lock_guard<std::mutex> lock(mutex_);
        file_ << "TRADE|"
              << trade.getBuyOrderId() << "|"
              << trade.getSellOrderId() << "|"
              << trade.getPrice() << "|"
              << trade.getQuantity() << "|"
              << std::chrono::system_clock::to_time_t(trade.getTimestamp())
              << std::endl;
    }

    void logCancel(const std::string& orderId) {
        std::lock_guard<std::mutex> lock(mutex_);
        file_ << "CANCEL|" << orderId << "|"
              << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())
              << std::endl;
    }

private:
    std::ofstream file_;
    std::mutex mutex_;
};

#endif // LOGGER_HPP