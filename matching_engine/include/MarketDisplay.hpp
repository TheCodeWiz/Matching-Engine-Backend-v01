#ifndef MARKET_DISPLAY_HPP
#define MARKET_DISPLAY_HPP

#include <windows.h>
#include <string>
#include <memory>
#include "OrderBook.hpp"

class MarketDisplay {
public:
    MarketDisplay(std::shared_ptr<OrderBook> orderBook) 
        : orderBook_(orderBook)
        , running_(false) {
    }

    ~MarketDisplay() {
        stop();
    }

    void start() {
        running_ = true;
        displayThread_ = std::thread(&MarketDisplay::run, this);
    }

    void stop() {
        running_ = false;
        if (displayThread_.joinable()) {
            displayThread_.join();
        }
    }

private:
    void run() {
        while (running_) {
            system("cls");
            double bestBid = orderBook_->getBestBidPrice();
            double bestAsk = orderBook_->getBestAskPrice();

            std::stringstream display;
            display << "\n\n";
            display << "╔═══════════════ LIVE MARKET DATA ═══════════════╗\n";
            display << "║                                               ║\n";
            display << "║  " << std::string(15, ' ') << "BEST PRICES" << std::string(15, ' ') << "  ║\n";
            display << "║                                               ║\n";
            display << "║    BEST BID: $" << std::fixed << std::setprecision(2) 
                   << std::setw(8) << bestBid << std::string(15, ' ') << "║\n";
            display << "║                                               ║\n";
            display << "║    BEST ASK: $" << std::fixed << std::setprecision(2) 
                   << std::setw(8) << bestAsk << std::string(15, ' ') << "║\n";
            display << "║                                               ║\n";
            display << "╚═══════════════════════════════════════════════╝\n";
            display << "\n\n";
            display << "     Market Data Updates Every 500ms\n";
            display << "     DO NOT CLOSE THIS WINDOW!\n";

            // Write to standard output
            std::cout << display.str() << std::flush;

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    std::shared_ptr<OrderBook> orderBook_;
    std::atomic<bool> running_;
    std::thread displayThread_;

};

#endif // MARKET_DISPLAY_HPP