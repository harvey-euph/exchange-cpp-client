#include "AlgoTradingClient.hpp"
#include "ClientAccount.hpp"
#include "L2Book.hpp"
#include "LogUtil.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <mutex>
#include <random>
#include <vector>
#include <cmath>
#include <atomic>
#include <algorithm>

#define LAYER 5

namespace Exchange {

// --- Observer Pattern ---

template <typename T>
class Observer {
public:
    virtual ~Observer() = default;
    virtual void on_update(const T& value) = 0;
};

template <typename T>
class Observable {
public:
    void add_observer(Observer<T>* observer) {
        std::lock_guard<std::mutex> lock(mtx_);
        observers_.push_back(observer);
    }
    
    void notify_observers(const T& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto observer : observers_) {
            observer->on_update(value);
        }
    }
private:
    std::vector<Observer<T>*> observers_;
    std::mutex mtx_;
};

// --- Fair Price Source (Observable) ---

class FairPriceSource : public Observable<double> {
public:
    FairPriceSource(double initial_price) : price_(initial_price) {}
    
    void start() {
        running_ = true;
        thread_ = std::thread([this]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::normal_distribution<> d(0, 1); // Small volatility
            
            while (running_) {
                price_ = price_ + d(gen);
                notify_observers(price_);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
    }
    
    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }
    
    double get_price() const { return price_; }
    
private:
    std::atomic<double> price_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

// --- Market Maker (AlgoTradingClient + Observer) ---

class MarketMaker : public AlgoTradingClient, public Observer<double> {
public:
    MarketMaker(const Config& config, uint32_t target_symbol) 
        : AlgoTradingClient(config), target_symbol_(target_symbol) {
        book_.symbol_id = target_symbol;
    }

    // AlgoTradingClient callbacks
    void on_l2_update(const L2Update* update) override {
        if (update->symbol_id() == target_symbol_) {
            book_.update(update->side(), update->p(), update->q());
        }
    }

    void on_l3_update(const L3Update* /*update*/) override {}

    void on_order_response(const OrderResponse* response) override {
        account_.handle_order_response(response);
    }

    void on_position_response(const PositionResponse* response) override {
        account_.handle_position_response(response);
    }

    // Observer callback (Fair Price Update)
    void on_update(const double& fair_price) override {
        if (!is_ready()) return;

        std::lock_guard<std::mutex> lock(strategy_mtx_);
        fair_price_ = fair_price;
        
        // Strategy parameters
        const int64_t base_spread = 2; 
        const uint64_t order_qty = 5;
        const int64_t price_tolerance = 1;

        // Basic inventory management: shade prices based on position
        int64_t pos = account_.get_position(target_symbol_);
        int64_t skew = 0;
        if (pos > 10) skew = -1;
        else if (pos < -10) skew = 1;

        std::vector<int64_t> target_bids(LAYER);
        std::vector<int64_t> target_asks(LAYER);

        for (int i = 0; i < LAYER; ++i) {
            target_bids[i] = static_cast<int64_t>(std::round(fair_price_ - (base_spread + i))) + skew;
            target_asks[i] = static_cast<int64_t>(std::round(fair_price_ + (base_spread + i))) + skew;
        }

        auto open_orders = account_.get_open_orders();
        std::vector<uint64_t> bid_order_ids;
        std::vector<uint64_t> ask_order_ids;

        for (const auto& order : open_orders) {
            if (order.symbol_id != target_symbol_) continue;
            if (order.side == Side_Buy) bid_order_ids.push_back(order.order_id);
            else if (order.side == Side_Sell) ask_order_ids.push_back(order.order_id);
        }

        // Sort existing orders by price to match layers (highest bid first, lowest ask first)
        auto get_order = [&](uint64_t id) {
            for (const auto& o : open_orders) if (o.order_id == id) return &o;
            return (const OrderResponseT*)nullptr;
        };

        std::sort(bid_order_ids.begin(), bid_order_ids.end(), [&](uint64_t a, uint64_t b) {
            auto oa = get_order(a);
            auto ob = get_order(b);
            if (!oa || !ob) return false;
            return oa->p > ob->p;
        });
        std::sort(ask_order_ids.begin(), ask_order_ids.end(), [&](uint64_t a, uint64_t b) {
            auto oa = get_order(a);
            auto ob = get_order(b);
            if (!oa || !ob) return false;
            return oa->p < ob->p;
        });

        // Maintain Bid Layers
        for (int i = 0; i < LAYER; ++i) {
            if (i < (int)bid_order_ids.size()) {
                uint64_t oid = bid_order_ids[i];
                auto o = get_order(oid);
                if (o && std::abs(o->p - target_bids[i]) > price_tolerance) {
                    replace_order(oid, target_bids[i], order_qty, target_symbol_, Side_Buy);
                }
            } else {
                new_limit_order(target_symbol_, Side_Buy, target_bids[i], order_qty);
            }
        }
        // Cancel extra bids
        for (size_t i = LAYER; i < bid_order_ids.size(); ++i) {
            cancel_order(bid_order_ids[i], target_symbol_, Side_Buy);
        }

        // Maintain Ask Layers
        for (int i = 0; i < LAYER; ++i) {
            if (i < (int)ask_order_ids.size()) {
                uint64_t oid = ask_order_ids[i];
                auto o = get_order(oid);
                if (o && std::abs(o->p - target_asks[i]) > price_tolerance) {
                    replace_order(oid, target_asks[i], order_qty, target_symbol_, Side_Sell);
                }
            } else {
                new_limit_order(target_symbol_, Side_Sell, target_asks[i], order_qty);
            }
        }
        // Cancel extra asks
        for (size_t i = LAYER; i < ask_order_ids.size(); ++i) {
            cancel_order(ask_order_ids[i], target_symbol_, Side_Sell);
        }

        display_status();
    }

    void start_fair_price_source(FairPriceSource& source) {
        source.add_observer(this);
    }

private:
    void display_status() {
        static int count = 0;
        if (++count % 5 != 0) return; 

        std::cout << "\033[H\033[J";
        std::cout << "========== Market Maker (Client ID: " << config_.client_id << ") ==========" << std::endl;
        std::cout << "Target Symbol: " << target_symbol_ << " | Layers: " << LAYER << std::endl;
        std::cout << "Fair Price:    " << std::fixed << std::setprecision(2) << fair_price_ << std::endl;
        std::cout << "Position:      " << account_.get_position(target_symbol_) << std::endl;
        std::cout << "Cash:          " << account_.get_cash() << std::endl;
        
        std::cout << "\n[Open Orders]" << std::endl;
        auto orders = account_.get_open_orders();
        // Sort for display
        std::sort(orders.begin(), orders.end(), [](const auto& a, const auto& b) {
            if (a.side != b.side) return a.side == Side_Sell; // Asks on top
            return (a.side == Side_Sell) ? a.p > b.p : a.p > b.p; // Sell: High to Low, Buy: High to Low
        });

        for (const auto& o : orders) {
            if (o.symbol_id != target_symbol_) continue;
            std::cout << "  " << (o.side == Side_Buy ? "BUY " : "SELL") 
                      << " " << std::setw(6) << o.p << " @ " << std::setw(4) << o.q 
                      << " (ID: " << o.order_id << ")" << std::endl;
        }
        std::cout << "============================================================" << std::endl;
    }

    uint32_t target_symbol_;
    L2Book book_;
    ClientAccount account_;
    double fair_price_ = 0.0;
    std::mutex strategy_mtx_;
};

} // namespace Exchange

int main() {
    Exchange::AlgoTradingConfig config;
    config.client_id = 201;
    config.symbol_ids = {1};

    uint32_t target_symbol = 1;
    Exchange::MarketMaker mm(config, target_symbol);
    
    Exchange::FairPriceSource fair_source(5000.0);
    mm.start_fair_price_source(fair_source);
    fair_source.start();

    std::cout << "Starting Market Maker for symbol " << target_symbol << "..." << std::endl;
    
    // In a separate thread, wait until ready and then query initial position
    std::thread init_thread([&mm, target_symbol]() {
        mm.wait_until_ready();
        mm.query_position(target_symbol);
        mm.query_position(0); // Cash
    });

    int ret = mm.run();
    
    fair_source.stop();
    if (init_thread.joinable()) init_thread.join();
    
    return ret;
}
