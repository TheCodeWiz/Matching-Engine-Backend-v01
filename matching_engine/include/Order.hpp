#ifndef ORDER_HPP
#define ORDER_HPP

#include <string>
#include <chrono>
#include <atomic>
#include <random>
#include "Instrument.hpp"

enum class OrderType {
    LIMIT,
    MARKET
};

enum class OrderSide {
    BUY,
    SELL
};

enum class TimeInForce {
    GTC,    // Good Till Cancelled
    IOC,    // Immediate or Cancel
    FOK,    // Fill or Kill
    DAY     // Day Order
};

enum class OrderStatus {
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    CANCELLED,
    EXPIRED
};

class Order {
public:
    Order(OrderType type, OrderSide side, double price, size_t quantity, 
          TimeInForce tif, const std::string& traderId, int instrumentId)
        : orderId_(generateOrderId(instrumentId, traderId))
        , type_(type)
        , side_(side)
        , price_(price)
        , quantity_(quantity)
        , remainingQuantity_(quantity)
        , timeInForce_(tif)
        , traderId_(traderId)
        , instrumentId_(instrumentId)
        , status_(OrderStatus::NEW)
        , timestamp_(std::chrono::system_clock::now())
    {}

    // Getters
    const std::string& getOrderId() const { return orderId_; }
    OrderType getType() const { return type_; }
    OrderSide getSide() const { return side_; }
    double getPrice() const { return price_; }
    size_t getQuantity() const { return quantity_; }
    size_t getRemainingQuantity() const { return remainingQuantity_; }
    TimeInForce getTimeInForce() const { return timeInForce_; }
    const std::string& getTraderId() const { return traderId_; }
    OrderStatus getStatus() const { return status_; }
    const std::chrono::system_clock::time_point& getTimestamp() const { return timestamp_; }
    int getInstrumentId() const { return instrumentId_; } // Added getter for instrumentId

    // Order execution methods
    void fill(size_t quantity) {
        remainingQuantity_ -= quantity;
        if (remainingQuantity_ == 0) {
            status_ = OrderStatus::FILLED;
        } else {
            status_ = OrderStatus::PARTIALLY_FILLED;
        }
    }

    void cancel() {
        if (status_ != OrderStatus::CANCELLED && status_ != OrderStatus::FILLED && status_ != OrderStatus::EXPIRED) {
            status_ = OrderStatus::CANCELLED;
        }
    }

    void expire() {
        status_ = OrderStatus::EXPIRED;
    }

private:
    // Generates orderId as instrumentId-random10DigitNumber-traderId
    static std::string generateOrderId(int instrumentId, const std::string& traderId) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<long long> dis(1000000000LL, 9999999999LL);
        // Format: instrumentId-random10DigitNumber-traderId
        return std::to_string(instrumentId) + "-" + std::to_string(dis(gen)) + "-" + traderId;
    }

    std::string orderId_;
    OrderType type_;
    OrderSide side_;
    double price_;
    size_t quantity_;
    int instrumentId_;
    std::atomic<size_t> remainingQuantity_;
    TimeInForce timeInForce_;
    std::string traderId_;
    std::atomic<OrderStatus> status_;
    std::chrono::system_clock::time_point timestamp_;
};

#endif // ORDER_HPP