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
#include <atomic>
#include <fstream>
#include "OrderBook.hpp"
#include "MockTrader.hpp"
#include "Logger.hpp"
#include "MarketDisplay.hpp"
#include "Instrument.hpp"

// Thread-safe User ID Generator
// User IDs for real users start from 10001 (mock traders use 1-10000)
// Uses atomic counter and timestamp to ensure uniqueness even with concurrent access
class UserIdGenerator {
public:
    static UserIdGenerator& getInstance() {
        static UserIdGenerator instance;
        return instance;
    }
    
    // Generate a unique user ID (thread-safe, first-come-first-served based on atomic increment)
    std::string generateUserId() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Get current timestamp for logging/verification
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
        
        // Atomically increment and get the next user ID
        int userId = nextUserId_.fetch_add(1);
        
        // Store allocation info for verification
        userAllocations_[userId] = timestamp;
        
        // Format as "USR" + 5-digit number
        std::stringstream ss;
        ss << "USR" << userId;
        return ss.str();
    }
    
    // Check if a user ID has been allocated
    bool isUserIdAllocated(int userId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return userAllocations_.find(userId) != userAllocations_.end();
    }
    
    // Get the timestamp when a user ID was allocated
    long long getAllocationTimestamp(int userId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = userAllocations_.find(userId);
        if (it != userAllocations_.end()) {
            return it->second;
        }
        return -1;
    }
    
private:
    UserIdGenerator() : nextUserId_(10001) {
        // Load last used ID from file if exists (for persistence across sessions)
        loadLastUsedId();
    }
    
    ~UserIdGenerator() {
        // Save the current ID counter for next session
        saveLastUsedId();
    }
    
    void loadLastUsedId() {
        std::ifstream file("user_id_counter.dat");
        if (file.is_open()) {
            int lastId;
            if (file >> lastId && lastId >= 10001) {
                nextUserId_.store(lastId);
            }
            file.close();
        }
    }
    
    void saveLastUsedId() {
        std::ofstream file("user_id_counter.dat");
        if (file.is_open()) {
            file << nextUserId_.load();
            file.close();
        }
    }
    
    // Prevent copying
    UserIdGenerator(const UserIdGenerator&) = delete;
    UserIdGenerator& operator=(const UserIdGenerator&) = delete;
    
    std::atomic<int> nextUserId_;
    std::map<int, long long> userAllocations_;  // userId -> allocation timestamp (microseconds)
    mutable std::mutex mutex_;
};

// Structure to track user's active trades/positions
struct UserTrade {
    std::string orderId;
    int instrumentId;
    OrderSide side;
    size_t quantity;
    double entryPrice;
    bool isActive;
    
    UserTrade(const std::string& id, int instId, OrderSide s, size_t qty, double price)
        : orderId(id), instrumentId(instId), side(s), quantity(qty), entryPrice(price), isActive(true) {}
};

// Structure to store closed/squared-off trades in history
struct ClosedTrade {
    std::string orderId;
    int instrumentId;
    OrderSide side;
    size_t quantity;
    double entryPrice;
    double exitPrice;
    double realizedPnL;
    double pnlPercent;
    std::chrono::system_clock::time_point exitTime;
    
    ClosedTrade(const std::string& id, int instId, OrderSide s, size_t qty, double entry, double exit, double pnl, double pnlPct)
        : orderId(id), instrumentId(instId), side(s), quantity(qty), entryPrice(entry), exitPrice(exit), 
          realizedPnL(pnl), pnlPercent(pnlPct), exitTime(std::chrono::system_clock::now()) {}
};

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
        , userId_(UserIdGenerator::getInstance().generateUserId())
        , totalBalance_(5000000.0)
        , totalRealizedPnL_(0.0)
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
                    case 'g':
                        handleAddBalance();
                        break;
                    case 'h':
                    case 'H':
                        handleExitTrade();
                        break;
                    case 'i':
                    case 'I':
                        handleWithdrawBalance();
                        break;
                    case 'j':
                    case 'J':
                        handleExitAllTrades();
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
            std::cout << "+-------------------------------------------------------------+\n";
            std::cout << "|  Qty (Buyers)  |  Bid Price  ||  Ask Price  |  Qty (Sellers)  |\n";
            std::cout << "+-------------------------------------------------------------+\n";

            size_t totalBuyQty = 0, totalSellQty = 0;
            // Prepare top 5 buy and sell levels
            std::vector<std::pair<std::string, std::string>> buyRows;  // qty, price
            std::vector<std::pair<std::string, std::string>> sellRows; // price, qty

            size_t count = 0;
            for (auto it = buyLevels.begin(); it != buyLevels.end() && count < 5; ++it, ++count) {
                double price = it->first;
                size_t qty = it->second->getTotalQuantity();
                std::stringstream priceStream;
                priceStream << std::fixed << std::setprecision(2) << price;
                buyRows.emplace_back(std::to_string(qty), priceStream.str());
                totalBuyQty += qty;
            }
            count = 0;
            for (auto it = sellLevels.begin(); it != sellLevels.end() && count < 5; ++it, ++count) {
                double price = it->first;
                size_t qty = it->second->getTotalQuantity();
                std::stringstream priceStream;
                priceStream << std::fixed << std::setprecision(2) << price;
                sellRows.emplace_back(priceStream.str(), std::to_string(qty));
                totalSellQty += qty;
            }

            // Print up to 5 rows
            for (size_t i = 0; i < 5; ++i) {
                // Buy side: Qty (Buyers), Bid Price
                std::string buyQty = i < buyRows.size() ? buyRows[i].first : "";
                std::string bidPrice = i < buyRows.size() ? buyRows[i].second : "";
                // Sell side: Ask Price, Qty (Sellers)
                std::string askPrice = i < sellRows.size() ? sellRows[i].first : "";
                std::string sellQty = i < sellRows.size() ? sellRows[i].second : "";
                std::cout << "| " << std::setw(13) << buyQty << " | " << std::setw(10) << bidPrice
                          << " || " << std::setw(10) << askPrice << " | " << std::setw(14) << sellQty << " |\n";
            }
            std::cout << "+-------------------------------------------------------------+\n";
            std::cout << "| " << std::setw(13) << totalBuyQty << " | Totals     || Totals      | " << std::setw(14) << totalSellQty << " |\n";
            std::cout << "+-------------------------------------------------------------+\n";
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

    // Handle adding balance
    void handleAddBalance() {
        addToHistory("=== Add Balance ===");
        std::cout << "\nCurrent Balance: Rs." << std::fixed << std::setprecision(2) << totalBalance_ << std::endl;
        std::cout << "Enter amount to add: Rs.";
        double amount;
        std::cin >> amount;
        
        if (amount <= 0) {
            addToHistory("Invalid amount. Please enter a positive value.");
            std::cout << "\nPress Enter to return to menu..."; 
            std::cin.ignore(); 
            std::cin.get();
            return;
        }
        
        totalBalance_ += amount;
        std::stringstream ss;
        ss << "Balance added: Rs." << std::fixed << std::setprecision(2) << amount 
           << " | New Balance: Rs." << totalBalance_;
        addToHistory(ss.str());
        std::cout << "\nBalance added successfully! New Balance: Rs." << std::fixed << std::setprecision(2) << totalBalance_;
        std::cout << "\nPress Enter to return to menu..."; 
        std::cin.ignore(); 
        std::cin.get();
    }

    // Handle withdrawing balance
    void handleWithdrawBalance() {
        addToHistory("=== Withdraw Balance ===");
        std::cout << "\nCurrent Balance: Rs." << std::fixed << std::setprecision(2) << totalBalance_ << std::endl;
        std::cout << "Enter amount to withdraw: Rs.";
        double amount;
        std::cin >> amount;
        
        if (amount <= 0) {
            addToHistory("Invalid amount. Please enter a positive value.");
            std::cout << "\nPress Enter to return to menu..."; 
            std::cin.ignore(); 
            std::cin.get();
            return;
        }
        
        if (amount > totalBalance_) {
            std::cout << "\n========================================" << std::endl;
            std::cout << "WITHDRAWAL FAILED!" << std::endl;
            std::cout << "========================================" << std::endl;
            std::cout << "Withdrawing amount (Rs." << std::fixed << std::setprecision(2) << amount 
                      << ") is more than the total balance (Rs." << totalBalance_ << ")." << std::endl;
            std::cout << "Please try entering a lesser amount or the same amount as total balance." << std::endl;
            std::cout << "========================================" << std::endl;
            addToHistory("Withdrawal failed: Amount exceeds total balance.");
            std::cout << "\nPress Enter to return to menu..."; 
            std::cin.ignore(); 
            std::cin.get();
            return;
        }
        
        totalBalance_ -= amount;
        std::stringstream ss;
        ss << "Balance withdrawn: Rs." << std::fixed << std::setprecision(2) << amount 
           << " | New Balance: Rs." << totalBalance_;
        addToHistory(ss.str());
        std::cout << "\nWithdrawal successful! New Balance: Rs." << std::fixed << std::setprecision(2) << totalBalance_;
        std::cout << "\nPress Enter to return to menu..."; 
        std::cin.ignore(); 
        std::cin.get();
    }

    // Check if user has sufficient balance for trade
    bool checkAndPromptBalance(double netAmount) {
        if (netAmount > totalBalance_) {
            std::cout << "\n========================================" << std::endl;
            std::cout << "INSUFFICIENT BALANCE!" << std::endl;
            std::cout << "Required: Rs." << std::fixed << std::setprecision(2) << netAmount << std::endl;
            std::cout << "Available: Rs." << std::fixed << std::setprecision(2) << totalBalance_ << std::endl;
            std::cout << "Shortfall: Rs." << std::fixed << std::setprecision(2) << (netAmount - totalBalance_) << std::endl;
            std::cout << "========================================" << std::endl;
            std::cout << "\nWould you like to add balance? (1: Yes, 2: No): ";
            int choice;
            std::cin >> choice;
            
            if (choice == 1) {
                std::cout << "Enter amount to add: Rs.";
                double amount;
                std::cin >> amount;
                
                if (amount > 0) {
                    totalBalance_ += amount;
                    std::stringstream ss;
                    ss << "Balance added: Rs." << std::fixed << std::setprecision(2) << amount 
                       << " | New Balance: Rs." << totalBalance_;
                    addToHistory(ss.str());
                    std::cout << "Balance updated! New Balance: Rs." << std::fixed << std::setprecision(2) << totalBalance_ << std::endl;
                    
                    // Check again if balance is now sufficient
                    if (netAmount <= totalBalance_) {
                        return true;
                    } else {
                        std::cout << "Still insufficient balance. Trade cancelled." << std::endl;
                        std::cout << "\nPress Enter to return to menu..."; 
                        std::cin.ignore(); 
                        std::cin.get();
                        return false;
                    }
                }
            }
            addToHistory("Trade cancelled due to insufficient balance.");
            std::cout << "\nPress Enter to return to menu..."; 
            std::cin.ignore(); 
            std::cin.get();
            return false;
        }
        return true;
    }

    // Calculate total unrealized P&L
    double calculateTotalUnrealizedPnL() {
        double totalPnL = 0.0;
        std::lock_guard<std::mutex> lock(tradesMutex_);
        for (const auto& trade : userActiveTrades_) {
            if (!trade.isActive) continue;
            
            const auto* instrument = InstrumentManager::getInstance().getInstrumentById(trade.instrumentId);
            if (!instrument) continue;
            
            double currentPrice = instrument->marketPrice;
            double pnl = 0.0;
            
            if (trade.side == OrderSide::BUY) {
                pnl = (currentPrice - trade.entryPrice) * trade.quantity;
            } else {
                pnl = (trade.entryPrice - currentPrice) * trade.quantity;
            }
            totalPnL += pnl;
        }
        return totalPnL;
    }

    // Handle exit trade
    void handleExitTrade() {
        addToHistory("=== Exit Trade ===");
        
        // Check if there are any active trades
        {
            std::lock_guard<std::mutex> lock(tradesMutex_);
            bool hasActive = false;
            for (const auto& trade : userActiveTrades_) {
                if (trade.isActive) {
                    hasActive = true;
                    break;
                }
            }
            if (!hasActive) {
                addToHistory("No active trades to exit.");
                std::cout << "\nNo active trades found. Press Enter to return to menu...";
                std::cin.ignore();
                std::cin.get();
                return;
            }
        }
        
        // Ask for Order ID
        std::cout << "\n=== Exit Trade ===" << std::endl;
        std::cout << "Enter Order ID to exit (press Enter to confirm): ";
        std::string orderId;
        std::cin >> orderId;
        
        // Find the trade
        UserTrade* foundTrade = nullptr;
        int tradeIndex = -1;
        {
            std::lock_guard<std::mutex> lock(tradesMutex_);
            for (size_t i = 0; i < userActiveTrades_.size(); ++i) {
                if (userActiveTrades_[i].orderId == orderId && userActiveTrades_[i].isActive) {
                    foundTrade = &userActiveTrades_[i];
                    tradeIndex = static_cast<int>(i);
                    break;
                }
            }
        }
        
        if (!foundTrade) {
            addToHistory("Trade not found or already exited: " + orderId);
            std::cout << "\nTrade not found or already exited. Press Enter to return to menu...";
            std::cin.ignore();
            std::cin.get();
            return;
        }
        
        // Get current price and calculate P&L
        const auto* instrument = InstrumentManager::getInstance().getInstrumentById(foundTrade->instrumentId);
        double currentPrice = instrument ? instrument->marketPrice : 0.0;
        double pnl = 0.0;
        double pnlPercent = 0.0;
        
        if (foundTrade->side == OrderSide::BUY) {
            pnl = (currentPrice - foundTrade->entryPrice) * foundTrade->quantity;
        } else {
            pnl = (foundTrade->entryPrice - currentPrice) * foundTrade->quantity;
        }
        
        if (foundTrade->entryPrice > 0) {
            pnlPercent = (pnl / (foundTrade->entryPrice * foundTrade->quantity)) * 100.0;
        }
        
        // Exit the trade - add P&L to balance and track realized P&L
        totalBalance_ += pnl + (foundTrade->entryPrice * foundTrade->quantity);
        totalRealizedPnL_ += pnl;
        
        // Create closed trade record and add to history
        ClosedTrade closedTrade(
            foundTrade->orderId,
            foundTrade->instrumentId,
            foundTrade->side,
            foundTrade->quantity,
            foundTrade->entryPrice,
            currentPrice,
            pnl,
            pnlPercent
        );
        
        {
            std::lock_guard<std::mutex> lock(tradesMutex_);
            userTradeHistory_.push_back(closedTrade);
            // Mark trade as inactive
            for (auto& trade : userActiveTrades_) {
                if (trade.orderId == orderId) {
                    trade.isActive = false;
                    break;
                }
            }
        }
        
        std::stringstream ss;
        ss << "Trade SQUARED OFF - ID: " << orderId 
           << " | Exit Price: Rs." << std::fixed << std::setprecision(2) << currentPrice
           << " | Realized P&L: Rs." << pnl;
        addToHistory(ss.str());
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "TRADE SQUARED OFF SUCCESSFULLY!" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Order ID: " << orderId << std::endl;
        std::cout << "Instrument: " << (instrument ? instrument->symbol : "Unknown") << std::endl;
        std::cout << "Side: " << (foundTrade->side == OrderSide::BUY ? "BUY" : "SELL") << std::endl;
        std::cout << "Quantity: " << foundTrade->quantity << std::endl;
        std::cout << "Entry Price: Rs." << std::fixed << std::setprecision(2) << foundTrade->entryPrice << std::endl;
        std::cout << "Exit Price: Rs." << std::fixed << std::setprecision(2) << currentPrice << std::endl;
        std::cout << "Realized P&L: Rs." << std::fixed << std::setprecision(2) << pnl << " (" << pnlPercent << "%)" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "New Balance: Rs." << std::fixed << std::setprecision(2) << totalBalance_ << std::endl;
        std::cout << "Total Realized P&L: Rs." << std::fixed << std::setprecision(2) << totalRealizedPnL_ << std::endl;
        
        std::cout << "\nPress Enter to return to menu...";
        std::cin.ignore();
        std::cin.get();
    }

    // Handle exit all trades - square off all active trades immediately at current LTP
    void handleExitAllTrades() {
        addToHistory("=== Exit All Trades ===");
        
        // Check if there are any active trades
        std::vector<UserTrade*> activeTrades;
        {
            std::lock_guard<std::mutex> lock(tradesMutex_);
            for (auto& trade : userActiveTrades_) {
                if (trade.isActive) {
                    activeTrades.push_back(&trade);
                }
            }
        }
        
        if (activeTrades.empty()) {
            addToHistory("No active trades to exit.");
            std::cout << "\nNo active trades found. Press Enter to return to menu...";
            std::cin.ignore();
            std::cin.get();
            return;
        }
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "SQUARING OFF ALL ACTIVE TRADES..." << std::endl;
        std::cout << "========================================" << std::endl;
        
        double totalPnL = 0.0;
        int tradesExited = 0;
        
        // Process each active trade
        for (auto* trade : activeTrades) {
            // Get current price (LTP) for the instrument
            const auto* instrument = InstrumentManager::getInstance().getInstrumentById(trade->instrumentId);
            double currentPrice = instrument ? instrument->marketPrice : 0.0;
            double pnl = 0.0;
            double pnlPercent = 0.0;
            
            if (trade->side == OrderSide::BUY) {
                pnl = (currentPrice - trade->entryPrice) * trade->quantity;
            } else {
                pnl = (trade->entryPrice - currentPrice) * trade->quantity;
            }
            
            if (trade->entryPrice > 0) {
                pnlPercent = (pnl / (trade->entryPrice * trade->quantity)) * 100.0;
            }
            
            // Exit the trade - add P&L to balance and track realized P&L
            totalBalance_ += pnl + (trade->entryPrice * trade->quantity);
            totalRealizedPnL_ += pnl;
            totalPnL += pnl;
            
            // Create closed trade record
            ClosedTrade closedTrade(
                trade->orderId,
                trade->instrumentId,
                trade->side,
                trade->quantity,
                trade->entryPrice,
                currentPrice,
                pnl,
                pnlPercent
            );
            
            {
                std::lock_guard<std::mutex> lock(tradesMutex_);
                userTradeHistory_.push_back(closedTrade);
            }
            
            // Display individual trade exit details
            std::cout << "Squared Off: " << trade->orderId 
                      << " | " << (instrument ? instrument->symbol : "Unknown")
                      << " | " << (trade->side == OrderSide::BUY ? "BUY" : "SELL")
                      << " | Qty: " << trade->quantity
                      << " | Entry: Rs." << std::fixed << std::setprecision(2) << trade->entryPrice
                      << " | Exit: Rs." << currentPrice
                      << " | P&L: Rs." << pnl << std::endl;
            
            std::stringstream ss;
            ss << "Trade SQUARED OFF - ID: " << trade->orderId 
               << " | Exit Price: Rs." << std::fixed << std::setprecision(2) << currentPrice
               << " | P&L: Rs." << pnl;
            addToHistory(ss.str());
            
            tradesExited++;
        }
        
        // Mark all trades as inactive
        {
            std::lock_guard<std::mutex> lock(tradesMutex_);
            for (auto& trade : userActiveTrades_) {
                trade.isActive = false;
            }
        }
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "ALL TRADES SQUARED OFF SUCCESSFULLY!" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Total Trades Exited: " << tradesExited << std::endl;
        std::cout << "Total Realized P&L: Rs." << std::fixed << std::setprecision(2) << totalPnL << std::endl;
        std::cout << "New Balance: Rs." << std::fixed << std::setprecision(2) << totalBalance_ << std::endl;
        std::cout << "Cumulative Realized P&L: Rs." << std::fixed << std::setprecision(2) << totalRealizedPnL_ << std::endl;
        std::cout << "========================================" << std::endl;
        
        std::stringstream summaryss;
        summaryss << "All trades squared off - Total P&L: Rs." << std::fixed << std::setprecision(2) << totalPnL;
        addToHistory(summaryss.str());
        
        std::cout << "\nPress Enter to return to menu...";
        std::cin.ignore();
        std::cin.get();
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

        // Calculate net amount and check balance
        double netAmount = price * quantity;
        if (!checkAndPromptBalance(netAmount)) {
            return;
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

        // Deduct from balance
        totalBalance_ -= netAmount;

        // Add to active trades for tracking
        {
            std::lock_guard<std::mutex> lock(tradesMutex_);
            userActiveTrades_.emplace_back(order->getOrderId(), currentInstrumentId_, OrderSide::BUY, quantity, price);
        }

        std::stringstream ss;
        ss << "BUY Order placed - ID: " << order->getOrderId() 
           << " | Type: " << (type == 1 ? "MARKET" : "LIMIT")
           << " | Quantity: " << quantity
           << " | Net Amount: Rs." << std::fixed << std::setprecision(2) << netAmount;
        if (type == 2) {
            ss << " | Price: Rs." << std::fixed << std::setprecision(2) << price;
        } else {
            ss << " | Market Price: Rs." << std::fixed << std::setprecision(2) << price;
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

        // Calculate net amount and check balance
        double netAmount = price * quantity;
        if (!checkAndPromptBalance(netAmount)) {
            return;
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

        // Deduct from balance
        totalBalance_ -= netAmount;

        // Add to active trades for tracking
        {
            std::lock_guard<std::mutex> lock(tradesMutex_);
            userActiveTrades_.emplace_back(order->getOrderId(), currentInstrumentId_, OrderSide::SELL, quantity, price);
        }

        std::stringstream ss;
        ss << "SELL Order placed - ID: " << order->getOrderId() 
           << " | Type: " << (type == 1 ? "MARKET" : "LIMIT")
           << " | Quantity: " << quantity
           << " | Net Amount: Rs." << std::fixed << std::setprecision(2) << netAmount;
        if (type == 2) {
            ss << " | Price: Rs." << std::fixed << std::setprecision(2) << price;
        } else {
            ss << " | Market Price: Rs." << std::fixed << std::setprecision(2) << price;
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

            // Display User Info Section
            double unrealizedPnL = calculateTotalUnrealizedPnL();
            std::cout << "\n+============================================================+\n";
            std::cout << "|                       USER INFO                            |\n";
            std::cout << "+============================================================+\n";
            std::cout << "| User ID: " << std::left << std::setw(49) << userId_ << "|\n";
            std::cout << "| Total Balance: Rs." << std::fixed << std::setprecision(2) << std::setw(39) << totalBalance_ << "|\n";
            std::cout << "| Total Unrealized P&L: Rs." << std::fixed << std::setprecision(2) << std::setw(32) << unrealizedPnL << "|\n";
            std::cout << "| Total Realized P&L: Rs." << std::fixed << std::setprecision(2) << std::setw(34) << totalRealizedPnL_ << "|\n";
            std::cout << "+============================================================+\n";

            // Display Volume Information Section (between USER INFO and Transaction History)
            size_t globalTotalVolume = getGlobalTotalVolume();
            size_t globalBuyVolume = getGlobalBuyVolume();
            size_t globalSellVolume = getGlobalSellVolume();
            size_t globalTradeCount = getGlobalTradeCount();
            std::cout << "\n+============================================================+\n";
            std::cout << "|                   VOLUME INFORMATION                       |\n";
            std::cout << "+============================================================+\n";
            std::cout << "| Total Volume (All Instruments): " << std::left << std::setw(24) << globalTotalVolume << "|\n";
            std::cout << "| Total Buy Volume:               " << std::left << std::setw(24) << globalBuyVolume << "|\n";
            std::cout << "| Total Sell Volume:              " << std::left << std::setw(24) << globalSellVolume << "|\n";
            std::cout << "| Total Trades (All Instruments): " << std::left << std::setw(24) << globalTradeCount << "|\n";
            std::cout << "+============================================================+\n";

            // Display message history
            std::cout << "\n=== Transaction History ===\n";
            {
                std::lock_guard<std::mutex> lock(historyMutex_);
                for (const auto& msg : messageHistory_) {
                    std::cout << msg << "\n";
                }
            }

            // Display User's Active Trades Section
            displayUserTradesSection();

            // Display User's Trade History Section (Squared Off Trades)
            displayTradeHistorySection();

            // Display current price of all instruments
            std::cout << "\n=== Current Price Of All Instruments ===\n";
            std::cout << "+------------------------------------------+\n";
            std::cout << "| Instrument Name           | Symbol         | Current Price   |\n";
            std::cout << "+------------------------------------------+\n";
            for (const auto& instrument : InstrumentManager::getInstance().getInstruments()) {
                double price = instrument.marketPrice;
                std::cout << "| " << std::left << std::setw(25) << instrument.name
                          << "| " << std::setw(13) << instrument.symbol
                          << "| â‚¹" << std::fixed << std::setprecision(2) << std::setw(14) << price << "|\n";
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
            std::cout << "+------------------------------------------+\n";
            // Per-instrument volume statistics
            size_t instTotalVolume = getTotalVolumeForInstrument(currentInstrumentId_);
            size_t instBuyVolume = getTotalBuyVolumeForInstrument(currentInstrumentId_);
            size_t instSellVolume = getTotalSellVolumeForInstrument(currentInstrumentId_);
            size_t instTradeCount = getTotalTradeCountForInstrument(currentInstrumentId_);
            std::cout << "| Total Volume:      " << std::left << std::setw(20) << instTotalVolume << "|\n";
            std::cout << "| Total Buy Volume:  " << std::left << std::setw(20) << instBuyVolume << "|\n";
            std::cout << "| Total Sell Volume: " << std::left << std::setw(20) << instSellVolume << "|\n";
            std::cout << "| Total Trades:      " << std::left << std::setw(20) << instTradeCount << "|\n";
            std::cout << "+------------------------------------------+\n";
            // Display order book for the selected instrument
            displayOrderBookTable(currentOrderBook, marketPrice);
            // Display menu
            std::cout << "\n+----------------------+\n";
            std::cout << "|         MENU         |\n";
            std::cout << "+----------------------+\n";
            std::cout << "| a. Place Buy         |\n";
            std::cout << "| b. Place Sell        |\n";
            std::cout << "| c. View Orders       |\n";
            std::cout << "| d. Query Order       |\n";
            std::cout << "| e. Exit Application  |\n";
            std::cout << "| f. Cancel Order      |\n";
            std::cout << "| g. Add Balance       |\n";
            std::cout << "| h. Exit Trade        |\n";
            std::cout << "| i. Withdraw Balance  |\n";
            std::cout << "| j. Exit All Trades   |\n";
            std::cout << "+----------------------+\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    // Display user's active trades with live P&L
    void displayUserTradesSection() {
        std::cout << "\n+======================================================================================================================+\n";
        std::cout << "|                                              YOUR ACTIVE TRADES                                                      |\n";
        std::cout << "+======================================================================================================================+\n";
        std::cout << "| Order ID         | Instrument         | Side   | Qty     | Entry Price | LTP (Current) | P&L          | P&L %       |\n";
        std::cout << "+------------------+--------------------+--------+---------+-------------+---------------+--------------+-------------+\n";
        
        std::lock_guard<std::mutex> lock(tradesMutex_);
        
        bool hasActiveTrades = false;
        for (const auto& trade : userActiveTrades_) {
            if (!trade.isActive) continue;
            hasActiveTrades = true;
            
            const auto* instrument = InstrumentManager::getInstance().getInstrumentById(trade.instrumentId);
            if (!instrument) continue;
            
            double currentPrice = instrument->marketPrice;
            double pnl = 0.0;
            double pnlPercent = 0.0;
            
            if (trade.side == OrderSide::BUY) {
                pnl = (currentPrice - trade.entryPrice) * trade.quantity;
            } else {
                pnl = (trade.entryPrice - currentPrice) * trade.quantity;
            }
            
            if (trade.entryPrice > 0) {
                pnlPercent = (pnl / (trade.entryPrice * trade.quantity)) * 100.0;
            }
            
            std::string pnlStr = (pnl >= 0 ? "+" : "") + std::to_string(pnl);
            std::string pnlPercentStr = (pnlPercent >= 0 ? "+" : "") + std::to_string(pnlPercent);
            
            std::stringstream pnlSS, pnlPercentSS;
            pnlSS << std::fixed << std::setprecision(2) << pnl;
            pnlPercentSS << std::fixed << std::setprecision(2) << pnlPercent << "%";
            
            std::cout << "| " << std::left << std::setw(16) << trade.orderId.substr(0, 16)
                      << " | " << std::setw(18) << (instrument->symbol.length() > 18 ? instrument->symbol.substr(0, 18) : instrument->symbol)
                      << " | " << std::setw(6) << (trade.side == OrderSide::BUY ? "BUY" : "SELL")
                      << " | " << std::setw(7) << trade.quantity
                      << " | Rs." << std::fixed << std::setprecision(2) << std::setw(8) << trade.entryPrice
                      << " | Rs." << std::setw(10) << currentPrice
                      << " | Rs." << std::setw(9) << pnlSS.str()
                      << " | " << std::setw(11) << pnlPercentSS.str() << " |\n";
        }
        
        if (!hasActiveTrades) {
            std::cout << "|                                        No active trades. Place an order to start trading!                           |\n";
        }
        
        std::cout << "+======================================================================================================================+\n";
    }

    // Display user's closed/squared-off trade history
    void displayTradeHistorySection() {
        std::cout << "\n+======================================================================================================================+\n";
        std::cout << "|                                              YOUR TRADE HISTORY (Squared Off)                                       |\n";
        std::cout << "+======================================================================================================================+\n";
        std::cout << "| Order ID         | Instrument         | Side   | Qty     | Entry Price | Exit Price    | P&L          | P&L %       |\n";
        std::cout << "+------------------+--------------------+--------+---------+-------------+---------------+--------------+-------------+\n";
        
        std::lock_guard<std::mutex> lock(tradesMutex_);
        
        if (userTradeHistory_.empty()) {
            std::cout << "|                                       No closed trades yet. Exit a trade to see history!                            |\n";
        } else {
            // Show last 5 closed trades (most recent first)
            int count = 0;
            for (auto it = userTradeHistory_.rbegin(); it != userTradeHistory_.rend() && count < 5; ++it, ++count) {
                const auto& trade = *it;
                const auto* instrument = InstrumentManager::getInstance().getInstrumentById(trade.instrumentId);
                
                std::stringstream pnlSS, pnlPercentSS;
                pnlSS << std::fixed << std::setprecision(2) << trade.realizedPnL;
                pnlPercentSS << std::fixed << std::setprecision(2) << trade.pnlPercent << "%";
                
                std::cout << "| " << std::left << std::setw(16) << trade.orderId.substr(0, 16)
                          << " | " << std::setw(18) << (instrument ? instrument->symbol.substr(0, 18) : "Unknown")
                          << " | " << std::setw(6) << (trade.side == OrderSide::BUY ? "BUY" : "SELL")
                          << " | " << std::setw(7) << trade.quantity
                          << " | Rs." << std::fixed << std::setprecision(2) << std::setw(8) << trade.entryPrice
                          << " | Rs." << std::setw(10) << trade.exitPrice
                          << " | Rs." << std::setw(9) << pnlSS.str()
                          << " | " << std::setw(11) << pnlPercentSS.str() << " |\n";
            }
        }
        
        std::cout << "+======================================================================================================================+\n";
    }

    // Get total volume for a specific instrument from OrderBook trades
    size_t getTotalVolumeForInstrument(int instrumentId) {
        auto it = orderBooks_.find(instrumentId);
        if (it != orderBooks_.end()) {
            return it->second->getTotalVolume();
        }
        return 0;
    }

    // Get total buy volume for a specific instrument
    size_t getTotalBuyVolumeForInstrument(int instrumentId) {
        auto it = orderBooks_.find(instrumentId);
        if (it != orderBooks_.end()) {
            return it->second->getTotalBuyVolume();
        }
        return 0;
    }

    // Get total sell volume for a specific instrument
    size_t getTotalSellVolumeForInstrument(int instrumentId) {
        auto it = orderBooks_.find(instrumentId);
        if (it != orderBooks_.end()) {
            return it->second->getTotalSellVolume();
        }
        return 0;
    }

    // Get total trade count for a specific instrument
    size_t getTotalTradeCountForInstrument(int instrumentId) {
        auto it = orderBooks_.find(instrumentId);
        if (it != orderBooks_.end()) {
            return it->second->getTotalTradeCount();
        }
        return 0;
    }

    // Get total volume across ALL instruments
    size_t getGlobalTotalVolume() {
        size_t total = 0;
        for (const auto& [id, orderBook] : orderBooks_) {
            total += orderBook->getTotalVolume();
        }
        return total;
    }

    // Get total buy volume across ALL instruments
    size_t getGlobalBuyVolume() {
        size_t total = 0;
        for (const auto& [id, orderBook] : orderBooks_) {
            total += orderBook->getTotalBuyVolume();
        }
        return total;
    }

    // Get total sell volume across ALL instruments
    size_t getGlobalSellVolume() {
        size_t total = 0;
        for (const auto& [id, orderBook] : orderBooks_) {
            total += orderBook->getTotalSellVolume();
        }
        return total;
    }

    // Get total trade count across ALL instruments
    size_t getGlobalTradeCount() {
        size_t total = 0;
        for (const auto& [id, orderBook] : orderBooks_) {
            total += orderBook->getTotalTradeCount();
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
    
    // User account management
    std::string userId_;
    double totalBalance_;
    double totalRealizedPnL_;
    std::vector<UserTrade> userActiveTrades_;
    std::vector<ClosedTrade> userTradeHistory_;
    mutable std::mutex tradesMutex_;
};

int main() {
    TradingApplication app;
    app.start();
    return 0;
}

