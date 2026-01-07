#ifndef TRADE_HPP
#define TRADE_HPP

#include <string>
#include <chrono>

class Trade {
public:
    Trade(const std::string& buyOrderId,
          const std::string& sellOrderId,
          double price,
          size_t quantity,
          std::chrono::system_clock::time_point timestamp)
        : buyOrderId_(buyOrderId)
        , sellOrderId_(sellOrderId)
        , price_(price)
        , quantity_(quantity)
        , timestamp_(timestamp)
    {}

    const std::string& getBuyOrderId() const { return buyOrderId_; }
    const std::string& getSellOrderId() const { return sellOrderId_; }
    double getPrice() const { return price_; }
    size_t getQuantity() const { return quantity_; }
    const std::chrono::system_clock::time_point& getTimestamp() const { return timestamp_; }

private:
    std::string buyOrderId_;
    std::string sellOrderId_;
    double price_;
    size_t quantity_;
    std::chrono::system_clock::time_point timestamp_;
};

#endif // TRADE_HPP