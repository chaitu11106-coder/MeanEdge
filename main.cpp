#include <iostream>
#include <thread>
#include <chrono>
#include "json_parser.hpp"
#include "trading_engine.hpp"

/**
 * @file main.cpp
 * @brief Trading engine entry point and simulation orchestrator
 * 
 * PRODUCTION DEPLOYMENT NOTES:
 * - In live systems, replace file I/O with FIX/WebSocket market data feed
 * - Add proper error handling and circuit breakers
 * - Implement order management system (OMS) integration
 * - Add telemetry and monitoring hooks
 * - Replace sleep() with event-driven architecture
 */

/**
 * @brief Simulate live market data feed with artificial delay
 * 
 * In production: This would be replaced by market data adapter
 * consuming from exchange feed handler (e.g., 0MQ, gRPC streaming).
 */
void simulateLiveDataFeed(const MarketData& data) {
    const int CANDLE_DELAY_MS = 500;  // 500ms between candles (compressed time)
    
    std::cout << "\n[SIMULATION] Processing candles with " 
              << CANDLE_DELAY_MS << "ms delay to simulate live feed...\n" << std::endl;
    
    TradingEngine engine(data);
    engine.run();
}

/**
 * @brief Application entry point
 */
int main(int argc, char* argv[]) {
    try {
        // Parse command-line arguments
        std::string input_file = "market_data.json";
        if (argc > 1) {
            input_file = argv[1];
        }
        
        std::cout << "Loading market data from: " << input_file << std::endl;
        
        // Load market data
        MarketData market_data = SimpleJSONParser::loadFromFile(input_file);
        
        // Validate data
        if (market_data.candles.empty()) {
            std::cerr << "ERROR: No candle data found in file" << std::endl;
            return 1;
        }
        
        if (market_data.capital <= 0) {
            std::cerr << "ERROR: Invalid capital amount" << std::endl;
            return 1;
        }
        
        std::cout << "Loaded " << market_data.candles.size() 
                  << " candles for " << market_data.instrument << std::endl;
        
        // Run trading simulation
        simulateLiveDataFeed(market_data);
        
        std::cout << "\n[SIMULATION COMPLETE]" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
