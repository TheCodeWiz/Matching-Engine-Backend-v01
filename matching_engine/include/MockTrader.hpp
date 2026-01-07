#ifndef MOCK_TRADER_HPP
#define MOCK_TRADER_HPP

#include <random>
#include <thread>
#include <chrono>
#include "OrderBook.hpp"
#include "Instrument.hpp"
#include "Logger.hpp"

class MockTrader {
public:
    static int mockTraderCount;
    MockTrader(std::shared_ptr<OrderBook> orderBook, int instrumentId, Logger* logger = nullptr)
        : orderBook_(orderBook)
        , instrumentId_(instrumentId)
        , running_(false)
        , engine_(std::random_device{}())
        , priceDistribution_(0.95, 1.05) // relative to market price
        , quantityDistribution_(1, 100)
        , sleepDistribution_(100, 2000)
        , sideDistribution_(0, 1)
        , logger_(logger)
    {
        if (mockTraderCount >= 10000) throw std::runtime_error("Max 10000 mock traders allowed");
        traderId_ = std::to_string(mockTraderCount++); // Unique traderId for each mock trader (0-9999)
    }

    void start() {
        running_ = true;
        thread_ = std::thread(&MockTrader::run, this);
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    Logger* logger_;
    void run() {
        while (running_) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(sleepDistribution_(engine_)));
            auto side = sideDistribution_(engine_) == 0 ? OrderSide::BUY : OrderSide::SELL;
            // Randomize order type for realism
            OrderType orderType = (quantityDistribution_(engine_) % 2 == 0) ? OrderType::LIMIT : OrderType::MARKET;
            // Get the current market price for this instrument
            const Instrument* instr = InstrumentManager::getInstance().getInstrumentById(instrumentId_);
            double basePrice = instr ? instr->marketPrice : 100.0;
            double price = basePrice * priceDistribution_(engine_);
            auto quantity = quantityDistribution_(engine_);
            auto order = std::make_shared<Order>(
                orderType,
                side,
                price,
                quantity,
                TimeInForce::GTC,
                traderId_, // Use unique traderId
                instrumentId_
            );
            orderBook_->addOrder(order);
            if (logger_) logger_->logOrder(*order);
        }
    }

    std::shared_ptr<OrderBook> orderBook_;
    std::string traderId_;
    std::atomic<bool> running_;
    std::thread thread_;
    
    std::mt19937 engine_;
    std::uniform_real_distribution<double> priceDistribution_;
    std::uniform_int_distribution<size_t> quantityDistribution_;
    std::uniform_int_distribution<size_t> sleepDistribution_;
    std::uniform_int_distribution<int> sideDistribution_;
    int instrumentId_;
};

int MockTrader::mockTraderCount = 0;

#endif // MOCK_TRADER_HPP