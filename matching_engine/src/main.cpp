#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <conio.h>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <windows.h>
#include <map>
#include "OrderBook.hpp"
#include "MockTrader.hpp"
#include "Logger.hpp"
#include "MarketDisplay.hpp"
#include "Instrument.hpp"


class TradingApplication {
    // Display all trades (user and mock traders) for the selected instrument
    void displayAllTrades() {
        addToHistory("=== Recent Trades (User + Mock Traders) ===");
        auto orderBook = orderBooks_[currentInstrumentId_];
        auto trades = orderBook->getRecentTrades();
        if (trades.empty()) {
            addToHistory("No trades found for this instrument.");
            return;
        }
        for (const auto& trade : trades) {
            std::stringstream ss;
            ss << "BuyOrderID: " << trade.getBuyOrderId()
               << " | SellOrderID: " << trade.getSellOrderId()
               << " | Price: $" << std::fixed << std::setprecision(2) << trade.getPrice()
               << " | Qty: " << trade.getQuantity();
            std::time_t t = std::chrono::system_clock::to_time_t(trade.getTimestamp());
            ss << " | Time: " << std::put_time(std::localtime(&t), "%F %T");
            addToHistory(ss.str());
        }
    }
public:
    TradingApplication()
        : logger_("trading_log.txt")
        , userTradeCount_(0)
    {
        // Create order books for each instrument
        for (const auto& instrument : InstrumentManager::getInstance().getInstruments()) {
            orderBooks_[instrument.instrumentId] = std::make_shared<OrderBook>();
            marketDisplays_[instrument.instrumentId] = std::make_shared<MarketDisplay>(orderBooks_[instrument.instrumentId]);
        }
        // No static price range is set; all prices are determined by order flow and mock traders
        currentInstrumentId_ = 1; // Default to first instrument
    }

    void start() {
        // Start mock traders for each instrument
        std::cout << "Starting mock traders..." << std::endl;
        int instrumentsCount = static_cast<int>(orderBooks_.size());
        int maxMockTraders = 10000;
        int tradersPerInstrument = maxMockTraders / instrumentsCount;
        for (const auto& [instrumentId, orderBook] : orderBooks_) {
            for (int i = 0; i < tradersPerInstrument; ++i) {
                auto trader = std::make_shared<MockTrader>(
                    orderBook,
                    instrumentId,
                    &logger_ // Pass logger to log mock trader orders
                );
                mockTraders_.push_back(trader);
                trader->start();
            }
        }

        // Start market data display thread
        displayThread_ = std::thread(&TradingApplication::displayMarketData, this);

        // Main trading loop
        running_ = true;
        while (running_) {
            if (_kbhit()) {
                char choice = _getch();
                switch (choice) {
                    case 'a':
                        handleBuyOrder();
                        break;
                    case 'b':
                        handleSellOrder();
                        break;
                    case 'c':
                        viewUserOrders();
                        break;
                    case 'd':
                        queryOrderStatus();
                        break;
                    case 't':
                        displayAllTrades();
                        break;
                    case 'f':
                        handleCancelOrder();
                        break;
                    case 'e':
                        running_ = false;
                        break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Cleanup
        for (auto& trader : mockTraders_) {
            trader->stop();
        }
        
        if (displayThread_.joinable()) {
            displayThread_.join();
        }
    }

private:

        void displayOrderBookTable(std::shared_ptr<OrderBook> orderBook, double marketPrice) {
            // Gather buy and sell levels
            const auto& buyLevels = orderBook->getBuyLevels();
            const auto& sellLevels = orderBook->getSellLevels();

            std::cout << "\nOrder Book (Top 5 Levels)\n";
            std::cout << "+---------------------------------------------------------------------------------------------+\n";
            std::cout << "|  Bid Price  |  Buy Orders  |  Qty (Buyers)  ||  Ask Price  |  Sell Orders  |  Qty (Sellers)  |\n";
            std::cout << "+---------------------------------------------------------------------------------------------+\n";

            size_t totalBuyQty = 0, totalSellQty = 0;
            size_t totalBuyOrders = 0, totalSellOrders = 0;
            // Prepare top 5 buy and sell levels
            std::vector<std::tuple<std::string, std::string, std::string>> buyRows;
            std::vector<std::tuple<std::string, std::string, std::string>> sellRows;

            size_t count = 0;
            for (auto it = buyLevels.begin(); it != buyLevels.end() && count < 5; ++it, ++count) {
                double price = it->first;
                size_t qty = it->second->getTotalQuantity();
                size_t orders = it->second->getOrders().size();
                std::stringstream priceStream;
                priceStream << std::fixed << std::setprecision(2) << price;
                buyRows.emplace_back(priceStream.str(), std::to_string(orders), std::to_string(qty));
                totalBuyQty += qty;
                totalBuyOrders += orders;
            }
            count = 0;
            for (auto it = sellLevels.begin(); it != sellLevels.end() && count < 5; ++it, ++count) {
                double price = it->first;
                size_t qty = it->second->getTotalQuantity();
                size_t orders = it->second->getOrders().size();
                std::stringstream priceStream;
                priceStream << std::fixed << std::setprecision(2) << price;
                sellRows.emplace_back(priceStream.str(), std::to_string(orders), std::to_string(qty));
                totalSellQty += qty;
                totalSellOrders += orders;
            }

            // Print up to 5 rows
            for (size_t i = 0; i < 5; ++i) {
                // Buy side
                std::string bidPrice = i < buyRows.size() ? std::get<0>(buyRows[i]) : "";
                std::string buyOrders = i < buyRows.size() ? std::get<1>(buyRows[i]) : "";
                std::string buyQty = i < buyRows.size() ? std::get<2>(buyRows[i]) : "";
                // Sell side
                std::string askPrice = i < sellRows.size() ? std::get<0>(sellRows[i]) : "";
                std::string sellOrders = i < sellRows.size() ? std::get<1>(sellRows[i]) : "";
                std::string sellQty = i < sellRows.size() ? std::get<2>(sellRows[i]) : "";
                std::cout << "| " << std::setw(10) << bidPrice << " | " << std::setw(11) << buyOrders << " | " << std::setw(13) << buyQty
                          << " || " << std::setw(10) << askPrice << " | " << std::setw(12) << sellOrders << " | " << std::setw(14) << sellQty << " |\n";
            }
            std::cout << "+---------------------------------------------------------------------------------------------+\n";
            std::cout << "| Totals      | " << std::setw(11) << totalBuyOrders << " | " << std::setw(13) << totalBuyQty
                      << " || Totals      | " << std::setw(12) << totalSellOrders << " | " << std::setw(14) << totalSellQty << " |\n";
            std::cout << "+---------------------------------------------------------------------------------------------+\n";
        }
    void selectInstrument() {
        system("cls");
        std::cout << "\n=== Select Instrument ===\n";
        const auto& instruments = InstrumentManager::getInstance().getInstruments();
        for (const auto& instrument : instruments) {
            std::cout << instrument.instrumentId << ". " << instrument.name 
                     << " (" << instrument.symbol << ")\n";
        }
        
        int selectedId;
        do {
            std::cout << "\nEnter instrument number: ";
            std::cin >> selectedId;
        } while (selectedId < 1 || selectedId > instruments.size());
        
        currentInstrumentId_ = selectedId;
        const auto* instrument = InstrumentManager::getInstance().getInstrumentById(selectedId);
        addToHistory("Selected instrument: " + instrument->name + " (" + instrument->symbol + ")");
    }

    void handleBuyOrder() {
        addToHistory("=== Placing Buy Order ===");
        selectInstrument();
        addToHistory("Enter order type (1 for Market, 2 for Limit):");
        int type;
        std::cin >> type;

        addToHistory("Enter quantity:");
        size_t quantity;
        std::cin >> quantity;

        double price = 0.0;
        if (type == 2) {
            addToHistory("Enter price:");
            std::cin >> price;
        } else {
            // Market order: set price to current best ask
            price = orderBooks_[currentInstrumentId_]->getBestAskPrice();
            if (price == 0.0) {
                addToHistory("No available ask price for this instrument. Market order cannot be placed.");
                return;
            }
        }

        auto order = std::make_shared<Order>(
            type == 1 ? OrderType::MARKET : OrderType::LIMIT,
            OrderSide::BUY,
            price,
            quantity,
            TimeInForce::GTC,
            "10001", // Static user traderId
            currentInstrumentId_
        );

        orderBooks_[currentInstrumentId_]->addOrder(order);
        logger_.logOrder(*order);
        userOrders_.push_back(order);

        std::stringstream ss;
        ss << "BUY Order placed - ID: " << order->getOrderId() 
           << " | Type: " << (type == 1 ? "MARKET" : "LIMIT")
           << " | Quantity: " << quantity;
        if (type == 2) {
            ss << " | Price: $" << std::fixed << std::setprecision(2) << price;
        } else {
            ss << " | Market Price: $" << std::fixed << std::setprecision(2) << price;
        }
        addToHistory(ss.str());
    }

    void handleSellOrder() {
        addToHistory("=== Placing Sell Order ===");
        selectInstrument();
        addToHistory("Enter order type (1 for Market, 2 for Limit):");
        int type;
        std::cin >> type;

        addToHistory("Enter quantity:");
        size_t quantity;
        std::cin >> quantity;

        double price = 0.0;
        if (type == 2) {
            addToHistory("Enter price:");
            std::cin >> price;
        } else {
            // Market order: set price to current best bid
            price = orderBooks_[currentInstrumentId_]->getBestBidPrice();
            if (price == 0.0) {
                addToHistory("No available bid price for this instrument. Market order cannot be placed.");
                return;
            }
        }

        auto order = std::make_shared<Order>(
            type == 1 ? OrderType::MARKET : OrderType::LIMIT,
            OrderSide::SELL,
            price,
            quantity,
            TimeInForce::GTC,
            "10001", // Static user traderId
            currentInstrumentId_
        );

        orderBooks_[currentInstrumentId_]->addOrder(order);
        logger_.logOrder(*order);
        userOrders_.push_back(order);

        std::stringstream ss;
        ss << "SELL Order placed - ID: " << order->getOrderId() 
           << " | Type: " << (type == 1 ? "MARKET" : "LIMIT")
           << " | Quantity: " << quantity;
        if (type == 2) {
            ss << " | Price: $" << std::fixed << std::setprecision(2) << price;
        } else {
            ss << " | Market Price: $" << std::fixed << std::setprecision(2) << price;
        }
        addToHistory(ss.str());
    }

    void viewUserOrders() {
        addToHistory("=== Your Orders ===");
        if (userOrders_.empty()) {
            addToHistory("No orders found.");
            return;
        }

        for (const auto& order : userOrders_) {
            std::stringstream ss;
            ss << "ID: " << order->getOrderId() 
               << " | Type: " << (order->getType() == OrderType::LIMIT ? "LIMIT" : "MARKET")
               << " | Side: " << (order->getSide() == OrderSide::BUY ? "BUY" : "SELL")
               << " | Price: $" << std::fixed << std::setprecision(2) << order->getPrice()
               << " | Qty: " << order->getQuantity()
               << " | Remaining: " << order->getRemainingQuantity()
               << " | Status: ";
            
            switch (order->getStatus()) {
                case OrderStatus::NEW: ss << "NEW"; break;
                case OrderStatus::PARTIALLY_FILLED: ss << "PARTIAL"; break;
                case OrderStatus::FILLED: ss << "FILLED"; break;
                case OrderStatus::CANCELLED: ss << "CANCELLED"; break;
                case OrderStatus::EXPIRED: ss << "EXPIRED"; break;
            }
            addToHistory(ss.str());
        }
    }

    void queryOrderStatus() {
        addToHistory("=== Query Order Status ===");
        addToHistory("Enter Order ID:");
        std::string orderId;
        std::cin >> orderId;

        auto it = std::find_if(userOrders_.begin(), userOrders_.end(),
            [&orderId](const std::shared_ptr<Order>& order) {
                return order->getOrderId() == orderId;
            });

        if (it != userOrders_.end()) {
            auto order = *it;
            std::stringstream ss;
            ss << "Order Details - ID: " << orderId << "\n"
               << "Type: " << (order->getType() == OrderType::LIMIT ? "LIMIT" : "MARKET") << "\n"
               << "Side: " << (order->getSide() == OrderSide::BUY ? "BUY" : "SELL") << "\n"
               << "Price: $" << std::fixed << std::setprecision(2) << order->getPrice() << "\n"
               << "Original Quantity: " << order->getQuantity() << "\n"
               << "Remaining Quantity: " << order->getRemainingQuantity() << "\n"
               << "Status: ";
            
            switch (order->getStatus()) {
                case OrderStatus::NEW: ss << "NEW"; break;
                case OrderStatus::PARTIALLY_FILLED: ss << "PARTIALLY FILLED"; break;
                case OrderStatus::FILLED: ss << "FILLED"; break;
                case OrderStatus::CANCELLED: ss << "CANCELLED"; break;
                case OrderStatus::EXPIRED: ss << "EXPIRED"; break;
            }
            addToHistory(ss.str());
        } else {
            addToHistory("Order not found: " + orderId);
        }
    }

    // Handle order cancellation
    void handleCancelOrder() {
        try {
            addToHistory("=== Cancel Order ===");
            addToHistory("Enter Order ID:");
            std::string orderId;
            std::cin >> orderId;
            auto it = std::find_if(userOrders_.begin(), userOrders_.end(),
                [&orderId](const std::shared_ptr<Order>& order) {
                    return order && order->getOrderId() == orderId;
                });
            if (it == userOrders_.end() || !(*it)) {
                addToHistory("Order not found: " + orderId);
                std::cout << "\nPress Enter to return to menu..."; std::cin.ignore(); std::cin.get();
                return;
            }
            auto order = *it;
            if (!order) {
                addToHistory("Order pointer is null.");
                std::cout << "\nPress Enter to return to menu..."; std::cin.ignore(); std::cin.get();
                return;
            }
            if (order->getStatus() == OrderStatus::CANCELLED) {
                addToHistory("Order is already cancelled.");
                std::cout << "\nPress Enter to return to menu..."; std::cin.ignore(); std::cin.get();
                return;
            }
            if (order->getStatus() == OrderStatus::FILLED || order->getStatus() == OrderStatus::EXPIRED) {
                addToHistory("Filled or expired orders cannot be cancelled.");
                std::cout << "\nPress Enter to return to menu..."; std::cin.ignore(); std::cin.get();
                return;
            }
            // Check for valid quantity
            if (order->getQuantity() <= 0) {
                addToHistory("Order quantity is zero or negative. Cannot cancel.");
                std::cout << "\nPress Enter to return to menu..."; std::cin.ignore(); std::cin.get();
                return;
            }
            addToHistory("Do you want to cancel this order? (1: Cancel, 2: Cancel Order)");
            int choice;
            std::cin >> choice;
            if (choice == 1) {
                addToHistory("Order cancellation aborted.");
                std::cout << "\nPress Enter to return to menu..."; std::cin.ignore(); std::cin.get();
                return;
            } else if (choice == 2) {
                auto orderBookIt = orderBooks_.find(order->getInstrumentId());
                if (orderBookIt != orderBooks_.end() && orderBookIt->second) {
                    orderBookIt->second->cancelOrder(orderId);
                } else {
                    order->cancel();
                }
                for (auto& uo : userOrders_) {
                    if (uo && uo->getOrderId() == orderId) {
                        uo->cancel();
                    }
                }
                logger_.logOrder(*order);
                addToHistory("Order cancelled: " + orderId);
                std::cout << "\nOrder cancelled successfully. Press Enter to return to menu..."; std::cin.ignore(); std::cin.get();
                return;
            } else {
                addToHistory("Invalid choice.");
                std::cout << "\nPress Enter to return to menu..."; std::cin.ignore(); std::cin.get();
                return;
            }
        } catch (const std::exception& ex) {
            addToHistory(std::string("Error during cancellation: ") + ex.what());
            std::cout << "\nAn error occurred. Press Enter to return to menu..."; std::cin.ignore(); std::cin.get();
            return;
        } catch (...) {
            addToHistory("Unknown error during cancellation.");
            std::cout << "\nAn unknown error occurred. Press Enter to return to menu..."; std::cin.ignore(); std::cin.get();
            return;
        }
    }

    std::vector<std::string> messageHistory_;
    std::mutex historyMutex_;

    void addToHistory(const std::string& message) {
        std::lock_guard<std::mutex> lock(historyMutex_);
        messageHistory_.push_back(message);
        if (messageHistory_.size() > 10) {  // Keep last 10 messages
            messageHistory_.erase(messageHistory_.begin());
        }
    }

    void displayMarketData() {
        while (running_) {
            system("cls");
            // Update all instruments' market prices in real time
            for (auto& instrument : const_cast<std::vector<Instrument>&>(InstrumentManager::getInstance().getInstruments())) {
                auto it = orderBooks_.find(instrument.instrumentId);
                if (it != orderBooks_.end()) {
                    auto orderBook = it->second;
                    double bestBid = orderBook->getBestBidPrice();
                    double bestAsk = orderBook->getBestAskPrice();
                    double price = 0.0;
                    if (bestBid > 0.0 && bestAsk > 0.0) {
                        price = (bestBid + bestAsk) / 2.0;
                    } else if (bestBid > 0.0) {
                        price = bestBid;
                    } else if (bestAsk > 0.0) {
                        price = bestAsk;
                    } else {
                        price = instrument.marketPrice;
                    }
                    instrument.marketPrice = price;
                }
            }

            const auto* currentInstrument = InstrumentManager::getInstance().getInstrumentById(currentInstrumentId_);
            auto currentOrderBook = orderBooks_[currentInstrumentId_];
            double bestBid = currentOrderBook->getBestBidPrice();
            double bestAsk = currentOrderBook->getBestAskPrice();
            double marketPrice = 0.0;
            if (bestBid > 0.0 && bestAsk > 0.0) {
                marketPrice = (bestBid + bestAsk) / 2.0;
            } else if (bestBid > 0.0) {
                marketPrice = bestBid;
            } else if (bestAsk > 0.0) {
                marketPrice = bestAsk;
            } else {
                marketPrice = currentInstrument->marketPrice;
            }
            const_cast<Instrument*>(currentInstrument)->marketPrice = marketPrice;

            // Display message history
            std::cout << "\n=== Transaction History ===\n";
            {
                std::lock_guard<std::mutex> lock(historyMutex_);
                for (const auto& msg : messageHistory_) {
                    std::cout << msg << "\n";
                }
            }

            // Display current price of all instruments
            std::cout << "\n=== Current Price Of All Instruments ===\n";
            std::cout << "+------------------------------------------+\n";
            std::cout << "| Instrument Name           | Symbol         | Current Price   |\n";
            std::cout << "+------------------------------------------+\n";
            for (const auto& instrument : InstrumentManager::getInstance().getInstruments()) {
                double price = instrument.marketPrice;
                std::cout << "| " << std::left << std::setw(25) << instrument.name
                          << "| " << std::setw(13) << instrument.symbol
                          << "| ₹" << std::fixed << std::setprecision(2) << std::setw(14) << price << "|\n";
            }
            std::cout << "+------------------------------------------+\n";

            // Display market data
            std::cout << "\n=== Live Market Data ===\n";
            std::cout << "+------------------------------------------+\n";
            std::cout << "|               MARKET DATA                |\n";
            std::cout << "+------------------------------------------+\n";
            std::cout << "| Current Instrument: " << std::left << std::setw(20) << currentInstrument->name << "|\n";
            std::cout << "| Symbol: " << std::left << std::setw(31) << currentInstrument->symbol << "|\n";
            std::cout << "| Market Price: " << std::fixed << std::setprecision(2) << std::right << std::setw(10) << marketPrice << std::setw(12) << "|\n";
            std::cout << "| Best Bid:    " << std::fixed << std::setprecision(2) << std::right << std::setw(10) << bestBid << std::setw(12) << "|\n";
            std::cout << "| Best Ask:    " << std::fixed << std::setprecision(2) << std::right << std::setw(10) << bestAsk << std::setw(12) << "|\n";
            std::cout << "| Total Volume: " << getTotalVolumeForInstrument(currentInstrumentId_) << std::setw(25) << "|\n";
            std::cout << "+------------------------------------------+\n";
            // Display order book for the selected instrument
            displayOrderBookTable(currentOrderBook, marketPrice);
            // Display menu
            std::cout << "\n+------------------+\n";
            std::cout << "|       MENU        |\n";
            std::cout << "+------------------+\n";
            std::cout << "| a. Place Buy     |\n";
            std::cout << "| b. Place Sell    |\n";
            std::cout << "| c. View Orders   |\n";
            std::cout << "| d. Query Order   |\n";
            std::cout << "| e. Exit          |\n";
            std::cout << "| f. Cancel Order  |\n";
            std::cout << "+------------------+\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    size_t getTotalVolumeForInstrument(int instrumentId) {
        size_t total = 0;
        // User orders
        for (const auto& order : userOrders_) {
            if (order->getStatus() != OrderStatus::CANCELLED && order->getStatus() != OrderStatus::EXPIRED && order->getStatus() != OrderStatus::NEW)
                if (order->getInstrumentId() == instrumentId)
                    total += order->getQuantity();
        }
        // Mock trader orders
        auto orderBook = orderBooks_[instrumentId];
        const auto& buyLevels = orderBook->getBuyLevels();
        const auto& sellLevels = orderBook->getSellLevels();
        for (const auto& [price, level] : buyLevels) {
            for (const auto& order : level->getOrders()) {
                if (order->getStatus() != OrderStatus::CANCELLED && order->getStatus() != OrderStatus::EXPIRED && order->getStatus() != OrderStatus::NEW)
                    total += order->getQuantity();
            }
        }
        for (const auto& [price, level] : sellLevels) {
            for (const auto& order : level->getOrders()) {
                if (order->getStatus() != OrderStatus::CANCELLED && order->getStatus() != OrderStatus::EXPIRED && order->getStatus() != OrderStatus::NEW)
                    total += order->getQuantity();
            }
        }
        return total;
    }

    std::map<int, std::shared_ptr<OrderBook>> orderBooks_;
    std::map<int, std::shared_ptr<MarketDisplay>> marketDisplays_;
    Logger logger_;
    std::vector<std::shared_ptr<MockTrader>> mockTraders_;
    std::vector<std::shared_ptr<Order>> userOrders_;
    std::atomic<bool> running_{false};
    std::thread displayThread_;
    std::atomic<int> userTradeCount_;
    int currentInstrumentId_;
    std::condition_variable cvRefresh_;
    std::mutex cvMutex_;
};

int main() {
    TradingApplication app;
    app.start();
    return 0;
}