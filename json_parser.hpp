#ifndef JSON_PARSER_HPP
#define JSON_PARSER_HPP

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "trading_engine.hpp"

/**
 * @class SimpleJSONParser
 * @brief Minimal JSON parser for market data loading
 * 
 * Production note: In real systems, use rapidjson or nlohmann/json.
 * This is a stripped-down parser for demonstration without external deps.
 * Handles only the specific JSON structure required for market data.
 */
class SimpleJSONParser {
private:
    std::string content_;
    size_t pos_;
    
    void skipWhitespace() {
        while (pos_ < content_.length() && 
               (content_[pos_] == ' ' || content_[pos_] == '\n' || 
                content_[pos_] == '\r' || content_[pos_] == '\t')) {
            pos_++;
        }
    }
    
    void expect(char c) {
        skipWhitespace();
        if (pos_ >= content_.length() || content_[pos_] != c) {
            throw std::runtime_error(std::string("Expected '") + c + "'");
        }
        pos_++;
    }
    
    std::string parseString() {
        skipWhitespace();
        if (content_[pos_] != '"') {
            throw std::runtime_error("Expected string");
        }
        pos_++; // Skip opening quote
        
        std::string result;
        while (pos_ < content_.length() && content_[pos_] != '"') {
            if (content_[pos_] == '\\') {
                pos_++;
                if (pos_ >= content_.length()) break;
            }
            result += content_[pos_];
            pos_++;
        }
        
        if (pos_ >= content_.length()) {
            throw std::runtime_error("Unterminated string");
        }
        pos_++; // Skip closing quote
        return result;
    }
    
    double parseNumber() {
        skipWhitespace();
        size_t start = pos_;
        
        if (content_[pos_] == '-') pos_++;
        
        while (pos_ < content_.length() && 
               (std::isdigit(content_[pos_]) || content_[pos_] == '.')) {
            pos_++;
        }
        
        std::string num_str = content_.substr(start, pos_ - start);
        return std::stod(num_str);
    }
    
    void skipValue() {
        skipWhitespace();
        char c = content_[pos_];
        
        if (c == '"') {
            parseString();
        } else if (c == '{') {
            pos_++;
            skipWhitespace();
            if (content_[pos_] != '}') {
                while (true) {
                    parseString(); // key
                    expect(':');
                    skipValue();   // value
                    skipWhitespace();
                    if (content_[pos_] == '}') break;
                    expect(',');
                }
            }
            expect('}');
        } else if (c == '[') {
            pos_++;
            skipWhitespace();
            if (content_[pos_] != ']') {
                while (true) {
                    skipValue();
                    skipWhitespace();
                    if (content_[pos_] == ']') break;
                    expect(',');
                }
            }
            expect(']');
        } else {
            // Number or boolean
            while (pos_ < content_.length() && 
                   content_[pos_] != ',' && 
                   content_[pos_] != '}' && 
                   content_[pos_] != ']') {
                pos_++;
            }
        }
    }
    
    Candle parseCandle() {
        Candle candle;
        expect('{');
        
        while (true) {
            skipWhitespace();
            if (content_[pos_] == '}') break;
            
            std::string key = parseString();
            expect(':');
            
            if (key == "timestamp") {
                candle.timestamp = parseString();
            } else if (key == "open") {
                candle.open = parseNumber();
            } else if (key == "high") {
                candle.high = parseNumber();
            } else if (key == "low") {
                candle.low = parseNumber();
            } else if (key == "close") {
                candle.close = parseNumber();
            } else {
                skipValue();
            }
            
            skipWhitespace();
            if (content_[pos_] == '}') break;
            expect(',');
        }
        
        expect('}');
        return candle;
    }
    
public:
    MarketData parse(const std::string& json_content) {
        content_ = json_content;
        pos_ = 0;
        MarketData data;
        
        expect('{');
        
        while (true) {
            skipWhitespace();
            if (content_[pos_] == '}') break;
            
            std::string key = parseString();
            expect(':');
            
            if (key == "instrument") {
                data.instrument = parseString();
            } else if (key == "previous_day_close") {
                data.previous_day_close = parseNumber();
            } else if (key == "capital") {
                data.capital = parseNumber();
            } else if (key == "candles") {
                expect('[');
                skipWhitespace();
                
                if (content_[pos_] != ']') {
                    while (true) {
                        data.candles.push_back(parseCandle());
                        skipWhitespace();
                        if (content_[pos_] == ']') break;
                        expect(',');
                    }
                }
                expect(']');
            } else {
                skipValue();
            }
            
            skipWhitespace();
            if (content_[pos_] == '}') break;
            expect(',');
        }
        
        expect('}');
        return data;
    }
    
    static MarketData loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + filename);
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        file.close();
        
        SimpleJSONParser parser;
        return parser.parse(content);
    }
};

#endif // JSON_PARSER_HPP
