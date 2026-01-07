#ifndef INSTRUMENT_HPP
#define INSTRUMENT_HPP

#include <string>
#include <vector>

struct Instrument {
    std::string name;
    std::string symbol;
    int instrumentId;
    double marketPrice;

    Instrument(const std::string& n, const std::string& s, int id, double mPrice)
        : name(n), symbol(s), instrumentId(id), marketPrice(mPrice) {}
};

class InstrumentManager {
public:
    static InstrumentManager& getInstance() {
        static InstrumentManager instance;
        return instance;
    }

    const std::vector<Instrument>& getInstruments() const { return instruments_; }

    const Instrument* getInstrumentById(int id) const {
        for (const auto& instrument : instruments_) {
            if (instrument.instrumentId == id) {
                return &instrument;
            }
        }
        return nullptr;
    }

private:
    InstrumentManager() {
        // Initialize with instruments and their market prices
        instruments_ = {
            Instrument("Reliance Industries", "RELIANCE (NSE)", 1, 1577.0),
            Instrument("Tata Consultancy Services", "TCS (NSE)", 2, 3213.0),
            Instrument("Dixon Technologies", "DIXON (NSE)", 3, 12055.0),
            Instrument("HDFC Bank", "HDFCBANK (NSE)", 4, 987.5),
            Instrument("Tata Motors", "TATAMOTORS (NSE)", 5, 373.55),
            Instrument("Tata Power", "TATAPOWER (NSE)", 6, 388.0),
            Instrument("Adani Enterprises", "ADANIENT (NSE)", 7, 2279.0),
            Instrument("Adani Green Energy", "ADANIGREEN (NSE)", 8, 1028.8),
            Instrument("Adani Power", "ADANIPOWER (NSE)", 9, 146.0),
            Instrument("Tanla Platforms", "TANLA (NSE)", 10, 524.0),
            Instrument("Nifty 50 Index", "NIFTY 50", 11, 26250.3),
            Instrument("Bank Nifty Index", "BANKNIFTY", 12, 60044.2),
            Instrument("FinNifty", "FINNIFTY", 13, 27851.45),
            Instrument("Sensex", "SENSEX", 14, 84961.14), // No price provided
            Instrument("Nifty Next 50 Index", "NIFTY NEXT 50", 15, 70413.4)
        };
    }

    std::vector<Instrument> instruments_;
};

#endif // INSTRUMENT_HPP