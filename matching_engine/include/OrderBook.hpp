#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "PriceLevel.hpp"
#include "Trade.hpp"

class OrderBook {
public:

        // Expose buy and sell levels for display
        const std::map<double, std::shared_ptr<PriceLevel>, std::greater<double>>& getBuyLevels() const {
            return reinterpret_cast<const std::map<double, std::shared_ptr<PriceLevel>, std::greater<double>>&>(buyLevels_);
        }
        const std::map<double, std::shared_ptr<PriceLevel>>& getSellLevels() const {
            return sellLevels_;
        }
    void addOrder(std::shared_ptr<Order> order) {
        if (order->getSide() == OrderSide::BUY) {
            matchOrder(order, sellLevels_, buyLevels_);
        } else {
            matchOrder(order, buyLevels_, sellLevels_);
        }
    }

    void cancelOrder(const std::string& orderId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = orderMap_.find(orderId);
        if (it == orderMap_.end()) {
            // Order not found, nothing to cancel
            return;
        }
        auto order = it->second;
        if (!order) {
            // Null pointer safety
            orderMap_.erase(it);
            return;
        }
        if (order->getStatus() == OrderStatus::CANCELLED || order->getStatus() == OrderStatus::FILLED || order->getStatus() == OrderStatus::EXPIRED) {
            // Already cancelled, filled, or expired
            return;
        }
        // Remove from book only if not already removed
        removeOrderFromBook(order);
        order->cancel();
        // Do not erase again, removeOrderFromBook already erases from orderMap
    }

    std::vector<Trade> getRecentTrades() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return recentTrades_;
    }

    double getBestBidPrice() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buyLevels_.empty() ? 0.0 : buyLevels_.rbegin()->first;
    }

    double getBestAskPrice() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sellLevels_.empty() ? 0.0 : sellLevels_.begin()->first;
    }

private:
    void matchOrder(std::shared_ptr<Order> incomingOrder,
                   std::map<double, std::shared_ptr<PriceLevel>>& oppositeSide,
                   std::map<double, std::shared_ptr<PriceLevel>>& sameSide) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        bool isFullyMatched = false;
        while (!oppositeSide.empty() && !isFullyMatched) {
            auto bestPrice = (incomingOrder->getSide() == OrderSide::BUY) ?
                           oppositeSide.begin()->first : oppositeSide.rbegin()->first;
            
            if ((incomingOrder->getSide() == OrderSide::BUY && bestPrice > incomingOrder->getPrice()) ||
                (incomingOrder->getSide() == OrderSide::SELL && bestPrice < incomingOrder->getPrice())) {
                break;
            }

            auto priceLevel = (incomingOrder->getSide() == OrderSide::BUY) ?
                            oppositeSide.begin()->second : oppositeSide.rbegin()->second;
            
            while (!priceLevel->isEmpty() && incomingOrder->getRemainingQuantity() > 0) {
                auto restingOrder = priceLevel->getFirstOrder();
                auto matchQty = std::min(incomingOrder->getRemainingQuantity(),
                                       restingOrder->getRemainingQuantity());
                
                executeTrade(incomingOrder, restingOrder, matchQty, bestPrice);
                
                if (restingOrder->getRemainingQuantity() == 0) {
                    removeOrderFromBook(restingOrder);
                }
                
                if (incomingOrder->getRemainingQuantity() == 0) {
                    isFullyMatched = true;
                    break;
                }
            }
            
            if (priceLevel->isEmpty()) {
                oppositeSide.erase(bestPrice);
            }
        }

        if (!isFullyMatched && incomingOrder->getTimeInForce() != TimeInForce::IOC) {
            addToBook(incomingOrder, sameSide);
        }
    }

    void addToBook(std::shared_ptr<Order> order,
                  std::map<double, std::shared_ptr<PriceLevel>>& side) {
        auto price = order->getPrice();
        auto& priceLevel = side[price];
        if (!priceLevel) {
            priceLevel = std::make_shared<PriceLevel>(price);
        }
        priceLevel->addOrder(order);
        orderMap_[order->getOrderId()] = order;
    }

    void removeOrderFromBook(std::shared_ptr<Order> order) {
        auto price = order->getPrice();
        auto& side = (order->getSide() == OrderSide::BUY) ? buyLevels_ : sellLevels_;
        
        auto it = side.find(price);
        if (it != side.end()) {
            it->second->removeOrder(order->getOrderId());
            if (it->second->isEmpty()) {
                side.erase(it);
            }
        }
        
        orderMap_.erase(order->getOrderId());
    }

    void executeTrade(std::shared_ptr<Order> incomingOrder,
                     std::shared_ptr<Order> restingOrder,
                     size_t quantity,
                     double price) {
        incomingOrder->fill(quantity);
        restingOrder->fill(quantity);
        
        Trade trade(incomingOrder->getOrderId(), restingOrder->getOrderId(),
                   price, quantity, std::chrono::system_clock::now());
        recentTrades_.push_back(trade);
        
        // Keep only last 100 trades
        if (recentTrades_.size() > 100) {
            recentTrades_.erase(recentTrades_.begin());
        }
    }

    std::map<double, std::shared_ptr<PriceLevel>> buyLevels_;
    std::map<double, std::shared_ptr<PriceLevel>> sellLevels_;
    std::unordered_map<std::string, std::shared_ptr<Order>> orderMap_;
    mutable std::mutex mutex_;
    std::vector<Trade> recentTrades_;
};

#endif // ORDER_BOOK_HPP