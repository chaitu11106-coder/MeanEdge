#ifndef TRADING_ENGINE_HPP
#define TRADING_ENGINE_HPP

#include <string>
#include <vector>
#include <memory>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cmath>

// ============================================================================
// CORE DATA STRUCTURES
// ============================================================================

/**
 * @brief OHLC candle representing 5-minute market data
 * 
 * Standard financial time-series representation used across all major
 * trading platforms. Each candle captures complete price action within
 * the time interval.
 */
struct Candle {
    std::string timestamp;  // Format: "HH:MM"
    double open;
    double high;
    double low;
    double close;
    
    Candle() : open(0), high(0), low(0), close(0) {}
    
    Candle(const std::string& ts, double o, double h, double l, double c)
        : timestamp(ts), open(o), high(h), low(l), close(c) {}
};

/**
 * @brief Trade execution record
 * 
 * Immutable record of each trade execution. Used for audit trail,
 * regulatory compliance, and post-trade analysis.
 */
struct Trade {
    enum class Side { BUY, SELL };
    enum class Type { ENTRY, EXIT };
    
    std::string timestamp;
    Side side;
    Type type;
    double price;
    int quantity;
    double pnl;  // Only relevant for exit trades
    
    Trade(const std::string& ts, Side s, Type t, double p, int q, double pnl_val = 0.0)
        : timestamp(ts), side(s), type(t), price(p), quantity(q), pnl(pnl_val) {}
    
    std::string getSideStr() const {
        return side == Side::BUY ? "BUY" : "SELL";
    }
    
    std::string getTypeStr() const {
        return type == Type::ENTRY ? "ENTRY" : "EXIT";
    }
};

/**
 * @brief Active position state
 * 
 * Tracks open position with entry details. Critical for risk calculations
 * and PnL marking. Single position model (no hedging).
 */
struct Position {
    bool is_open;
    Trade::Side side;
    double entry_price;
    int quantity;
    std::string entry_timestamp;
    
    Position() : is_open(false), side(Trade::Side::BUY), 
                 entry_price(0), quantity(0) {}
    
    void open(Trade::Side s, double price, int qty, const std::string& ts) {
        is_open = true;
        side = s;
        entry_price = price;
        quantity = qty;
        entry_timestamp = ts;
    }
    
    void close() {
        is_open = false;
        entry_price = 0;
        quantity = 0;
        entry_timestamp.clear();
    }
    
    /**
     * @brief Calculate unrealized PnL for current position
     * @param current_price Market price for marking
     * @return Unrealized profit/loss in currency units
     */
    double getUnrealizedPnL(double current_price) const {
        if (!is_open) return 0.0;
        
        if (side == Trade::Side::BUY) {
            return (current_price - entry_price) * quantity;
        } else {
            return (entry_price - current_price) * quantity;
        }
    }
};

/**
 * @brief Market data container loaded from JSON
 * 
 * Encapsulates all market data for a single instrument's trading session.
 * Immutable after load for thread-safety in production systems.
 */
struct MarketData {
    std::string instrument;
    double previous_day_close;
    double capital;
    std::vector<Candle> candles;
    
    MarketData() : previous_day_close(0), capital(0) {}
};


// ============================================================================
// EXPONENTIAL MOVING AVERAGE CALCULATOR
// ============================================================================

/**
 * @class EMACalculator
 * @brief Efficient incremental EMA calculation
 * 
 * Uses standard exponential smoothing formula: EMA_t = α * Price_t + (1-α) * EMA_{t-1}
 * where α = 2/(period+1). Minimizes computation in real-time loops.
 */
class EMACalculator {
private:
    int period_;
    double multiplier_;  // Pre-calculated α for efficiency
    double ema_;
    bool initialized_;
    
public:
    explicit EMACalculator(int period) 
        : period_(period), 
          multiplier_(2.0 / (period + 1.0)),
          ema_(0.0),
          initialized_(false) {}
    
    /**
     * @brief Update EMA with new price tick
     * @param price Current close price
     * 
     * First value uses simple initialization (price itself).
     * Subsequent values use exponential smoothing.
     */
    void update(double price) {
        if (!initialized_) {
            ema_ = price;
            initialized_ = true;
        } else {
            ema_ = (price * multiplier_) + (ema_ * (1.0 - multiplier_));
        }
    }
    
    double getValue() const { return ema_; }
    bool isInitialized() const { return initialized_; }
    void reset() { ema_ = 0.0; initialized_ = false; }
};


// ============================================================================
// STRATEGY SIGNAL GENERATOR
// ============================================================================

/**
 * @class TwoCandelPatternStrategy
 * @brief Implements gap-up rejection pattern with EMA filters
 * 
 * STRATEGY THESIS:
 * Identifies weak gap-ups that fail to sustain momentum. When price opens
 * significantly above previous close but fails to hold (breaks first candle low),
 * it signals exhaustion and potential mean reversion.
 * 
 * EMA FILTER PURPOSE:
 * First candle must stay above EMA(5) to confirm strength. Break below
 * suggests failed breakout rather than consolidation.
 * 
 * RISK PROFILE: Mean-reversion, counter-trend
 * TYPICAL HOLD: Intraday only (squared off by 3 PM)
 */
class TwoCandelPatternStrategy {
private:
    const double GAP_THRESHOLD = 0.03;  // 3% gap requirement
    
    Candle first_candle_;
    bool first_candle_valid_;
    double previous_day_close_;
    
    EMACalculator ema3_;
    EMACalculator ema5_;
    
public:
    TwoCandelPatternStrategy() 
        : first_candle_valid_(false),
          previous_day_close_(0),
          ema3_(3),
          ema5_(5) {}
    
    void initialize(double prev_close) {
        previous_day_close_ = prev_close;
        first_candle_valid_ = false;
        ema3_.reset();
        ema5_.reset();
    }
    
    /**
     * @brief Process new candle and check for trade signal
     * @param candle Current 5-min candle
     * @return true if SELL signal generated
     * 
     * State machine:
     * 1. Wait for valid first candle (gap-up above EMA5)
     * 2. Check if second candle breaks first candle low
     * 3. Generate signal and reset state
     */
    bool processCandle(const Candle& candle) {
        // Update indicators
        ema3_.update(candle.close);
        ema5_.update(candle.close);
        
        // Need at least 5 candles for EMA(5) to stabilize
        if (!ema5_.isInitialized()) {
            return false;
        }
        
        // Check for valid first candle
        if (!first_candle_valid_) {
            // Condition 1: Gap-up >= 3%
            bool gap_condition = candle.open >= previous_day_close_ * (1.0 + GAP_THRESHOLD);
            
            // Condition 2: Low stays above EMA(5)
            bool ema_condition = candle.low > ema5_.getValue();
            
            if (gap_condition && ema_condition) {
                first_candle_ = candle;
                first_candle_valid_ = true;
            }
            return false;
        }
        
        // Check for second candle breakdown
        if (first_candle_valid_) {
            bool breakdown = candle.low < first_candle_.low;
            
            if (breakdown) {
                // Signal generated - reset state for next opportunity
                first_candle_valid_ = false;
                return true;
            }
        }
        
        return false;
    }
    
    double getEMA3() const { return ema3_.getValue(); }
    double getEMA5() const { return ema5_.getValue(); }
    bool isEMA5Ready() const { return ema5_.isInitialized(); }
};


// ============================================================================
// RISK MANAGEMENT ENGINE
// ============================================================================

/**
 * @class RiskManager
 * @brief Capital-based risk control and position sizing
 * 
 * RISK MODEL:
 * - Stop loss: 2% of total capital (not position size)
 * - Take profit: 7% of total capital
 * - Max positions: 1 (no pyramiding)
 * - Max daily trades: 2 (prevent overtrading)
 * 
 * This is a conservative intraday model suitable for prop desks
 * with strict drawdown limits.
 */
class RiskManager {
private:
    const double STOP_LOSS_PCT = 0.02;   // 2% capital stop
    const double TAKE_PROFIT_PCT = 0.07; // 7% capital target
    const int MAX_DAILY_TRADES = 2;
    
    double initial_capital_;
    double current_capital_;
    int trades_today_;
    
    double stop_loss_amount_;
    double take_profit_amount_;
    
public:
    explicit RiskManager(double capital) 
        : initial_capital_(capital),
          current_capital_(capital),
          trades_today_(0) {
        
        stop_loss_amount_ = initial_capital_ * STOP_LOSS_PCT;
        take_profit_amount_ = initial_capital_ * TAKE_PROFIT_PCT;
    }
    
    /**
     * @brief Calculate position size for new trade
     * @param entry_price Intended entry price
     * @return Number of shares to trade
     * 
     * Simple model: use all available capital. In production,
     * this would incorporate volatility-based sizing (e.g., Kelly criterion).
     */
    int calculatePositionSize(double entry_price) const {
        if (entry_price <= 0) return 0;
        return static_cast<int>(current_capital_ / entry_price);
    }
    
    bool canTrade() const {
        return trades_today_ < MAX_DAILY_TRADES;
    }
    
    void recordTrade() {
        trades_today_++;
    }
    
    /**
     * @brief Check if stop loss hit based on unrealized PnL
     */
    bool isStopLossHit(double unrealized_pnl) const {
        return unrealized_pnl <= -stop_loss_amount_;
    }
    
    /**
     * @brief Check if take profit hit based on unrealized PnL
     */
    bool isTakeProfitHit(double unrealized_pnl) const {
        return unrealized_pnl >= take_profit_amount_;
    }
    
    void updateCapital(double pnl) {
        current_capital_ += pnl;
    }
    
    double getCurrentCapital() const { return current_capital_; }
    double getInitialCapital() const { return initial_capital_; }
    double getTotalPnL() const { return current_capital_ - initial_capital_; }
    double getTotalPnLPercent() const { 
        return ((current_capital_ - initial_capital_) / initial_capital_) * 100.0; 
    }
    int getTradesCount() const { return trades_today_; }
    
    double getStopLossAmount() const { return stop_loss_amount_; }
    double getTakeProfitAmount() const { return take_profit_amount_; }
};


// ============================================================================
// TRADING ENGINE ORCHESTRATOR
// ============================================================================

/**
 * @class TradingEngine
 * @brief Main event-driven trading system coordinator
 * 
 * Orchestrates all components in proper sequence:
 * Market Data → Indicators → Strategy → Risk → Execution → PnL
 * 
 * Designed for single-threaded deterministic execution (critical for backtesting
 * and regulatory reproducibility).
 */
class TradingEngine {
private:
    MarketData market_data_;
    TwoCandelPatternStrategy strategy_;
    RiskManager risk_manager_;
    Position position_;
    std::vector<Trade> trade_log_;
    
    size_t current_candle_index_;
    bool session_active_;
    
    // Time-based controls
    const std::string MARKET_CLOSE_TIME = "15:00";
    
    /**
     * @brief Parse time string to minutes since market open
     */
    int parseTimeToMinutes(const std::string& time_str) const {
        int hours = std::stoi(time_str.substr(0, 2));
        int minutes = std::stoi(time_str.substr(3, 2));
        return hours * 60 + minutes;
    }
    
    bool isPastMarketClose(const std::string& current_time) const {
        return parseTimeToMinutes(current_time) >= parseTimeToMinutes(MARKET_CLOSE_TIME);
    }
    
    /**
     * @brief Execute sell order (strategy only generates SELL signals)
     */
    void executeSellOrder(const Candle& candle) {
        if (!risk_manager_.canTrade()) {
            logMessage("Trade limit reached for the day");
            return;
        }
        
        if (position_.is_open) {
            logMessage("Position already open - skipping signal");
            return;
        }
        
        double entry_price = candle.close;  // Assume execution at candle close
        int quantity = risk_manager_.calculatePositionSize(entry_price);
        
        if (quantity <= 0) {
            logMessage("Insufficient capital for position");
            return;
        }
        
        // Open position
        position_.open(Trade::Side::SELL, entry_price, quantity, candle.timestamp);
        risk_manager_.recordTrade();
        
        // Log trade
        Trade entry_trade(candle.timestamp, Trade::Side::SELL, Trade::Type::ENTRY, 
                         entry_price, quantity);
        trade_log_.push_back(entry_trade);
        
        logTrade(entry_trade);
    }
    
    /**
     * @brief Close current position
     */
    void closePosition(const Candle& candle, const std::string& reason) {
        if (!position_.is_open) return;
        
        double exit_price = candle.close;
        double pnl = position_.getUnrealizedPnL(exit_price);
        
        // Update capital
        risk_manager_.updateCapital(pnl);
        
        // Log exit trade
        Trade exit_trade(candle.timestamp, position_.side, Trade::Type::EXIT,
                        exit_price, position_.quantity, pnl);
        trade_log_.push_back(exit_trade);
        
        logExit(exit_trade, reason);
        
        // Close position
        position_.close();
    }
    
    /**
     * @brief Check all exit conditions (SL/TP/Time)
     */
    void checkExitConditions(const Candle& candle) {
        if (!position_.is_open) return;
        
        double unrealized_pnl = position_.getUnrealizedPnL(candle.close);
        
        // Check stop loss
        if (risk_manager_.isStopLossHit(unrealized_pnl)) {
            closePosition(candle, "Stop Loss Hit");
            return;
        }
        
        // Check take profit
        if (risk_manager_.isTakeProfitHit(unrealized_pnl)) {
            closePosition(candle, "Take Profit Hit");
            return;
        }
        
        // Check market close
        if (isPastMarketClose(candle.timestamp)) {
            closePosition(candle, "Market Close (15:00)");
            session_active_ = false;
            return;
        }
    }
    
    // Logging methods
    void logMessage(const std::string& msg) const {
        std::cout << "[INFO] " << msg << std::endl;
    }
    
    void logCandle(const Candle& candle, double ema3, double ema5) const {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "\n[" << candle.timestamp << "] "
                  << "O:" << candle.open << " "
                  << "H:" << candle.high << " "
                  << "L:" << candle.low << " "
                  << "C:" << candle.close << " | "
                  << "EMA3:" << ema3 << " "
                  << "EMA5:" << ema5 << std::endl;
    }
    
    void logTrade(const Trade& trade) const {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << ">>> [TRADE EXECUTED] " << trade.getTypeStr() << " | "
                  << trade.getSideStr() << " " << trade.quantity << " @ " 
                  << trade.price << " at " << trade.timestamp << std::endl;
    }
    
    void logExit(const Trade& trade, const std::string& reason) const {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "<<< [TRADE CLOSED] " << reason << " | "
                  << "P&L: ₹" << trade.pnl 
                  << " (" << (trade.pnl >= 0 ? "+" : "") 
                  << (trade.pnl / risk_manager_.getInitialCapital() * 100.0) << "%)"
                  << " at " << trade.timestamp << std::endl;
    }
    
public:
    explicit TradingEngine(const MarketData& data)
        : market_data_(data),
          risk_manager_(data.capital),
          current_candle_index_(0),
          session_active_(true) {}
    
    /**
     * @brief Main simulation loop - processes market data tick-by-tick
     */
    void run() {
        printHeader();
        
        // Initialize strategy with previous day close
        strategy_.initialize(market_data_.previous_day_close);
        
        std::cout << "\n════════════════════════════════════════════════════════════════\n";
        std::cout << "Starting Trading Session for " << market_data_.instrument << std::endl;
        std::cout << "Previous Day Close: ₹" << market_data_.previous_day_close << std::endl;
        std::cout << "Initial Capital: ₹" << risk_manager_.getInitialCapital() << std::endl;
        std::cout << "Stop Loss: ₹" << risk_manager_.getStopLossAmount() 
                  << " (2% of capital)" << std::endl;
        std::cout << "Take Profit: ₹" << risk_manager_.getTakeProfitAmount() 
                  << " (7% of capital)" << std::endl;
        std::cout << "════════════════════════════════════════════════════════════════\n";
        
        // Process each candle
        for (const auto& candle : market_data_.candles) {
            if (!session_active_) break;
            
            // Update strategy with new candle
            bool signal = strategy_.processCandle(candle);
            
            // Log candle data
            if (strategy_.isEMA5Ready()) {
                logCandle(candle, strategy_.getEMA3(), strategy_.getEMA5());
            } else {
                std::cout << "\n[" << candle.timestamp << "] "
                          << "Warming up indicators..." << std::endl;
            }
            
            // Check exit conditions first (if position open)
            checkExitConditions(candle);
            
            // Process entry signal (if any)
            if (signal && session_active_) {
                std::cout << "\n*** SIGNAL DETECTED: Two-Candle Pattern Breakdown ***\n";
                executeSellOrder(candle);
            }
            
            // Display current status
            if (position_.is_open) {
                double unrealized = position_.getUnrealizedPnL(candle.close);
                std::cout << "    [Position] OPEN | Unrealized P&L: ₹" 
                          << std::fixed << std::setprecision(2) << unrealized << std::endl;
            }
        }
        
        // Force close any open position at end of data
        if (position_.is_open && !market_data_.candles.empty()) {
            const Candle& last_candle = market_data_.candles.back();
            closePosition(last_candle, "End of Market Data");
        }
        
        printSummary();
    }
    
    void printHeader() const {
        std::cout << "\n";
        std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
        std::cout << "║          MINI ALGORITHMIC TRADING ENGINE v1.0                  ║\n";
        std::cout << "║          Intraday Mean-Reversion Strategy Simulator            ║\n";
        std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    }
    
    void printSummary() const {
        std::cout << "\n";
        std::cout << "════════════════════════════════════════════════════════════════\n";
        std::cout << "                    END OF DAY SUMMARY                          \n";
        std::cout << "════════════════════════════════════════════════════════════════\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Instrument:          " << market_data_.instrument << std::endl;
        std::cout << "Total Trades:        " << risk_manager_.getTradesCount() << std::endl;
        std::cout << "Initial Capital:     ₹" << risk_manager_.getInitialCapital() << std::endl;
        std::cout << "Final Capital:       ₹" << risk_manager_.getCurrentCapital() << std::endl;
        std::cout << "Total P&L:           ₹" << risk_manager_.getTotalPnL();
        
        if (risk_manager_.getTotalPnL() >= 0) {
            std::cout << " ✓" << std::endl;
        } else {
            std::cout << " ✗" << std::endl;
        }
        
        std::cout << "Return:              " << risk_manager_.getTotalPnLPercent() << "%" << std::endl;
        std::cout << "════════════════════════════════════════════════════════════════\n";
        
        // Trade log
        if (!trade_log_.empty()) {
            std::cout << "\nTrade Log:\n";
            std::cout << "------------------------------------------------------------\n";
            for (const auto& trade : trade_log_) {
                std::cout << trade.timestamp << " | " 
                          << trade.getTypeStr() << " | "
                          << trade.getSideStr() << " | "
                          << trade.quantity << " @ ₹" << trade.price;
                if (trade.type == Trade::Type::EXIT) {
                    std::cout << " | P&L: ₹" << trade.pnl;
                }
                std::cout << std::endl;
            }
            std::cout << "------------------------------------------------------------\n";
        }
    }
};

#endif // TRADING_ENGINE_HPP
