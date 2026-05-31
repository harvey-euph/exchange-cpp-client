#include "csv_util.hpp"
#include <iostream>
#include <sstream>
#include <vector>

namespace Exchange {

static int64_t safe_stoll(const std::string& str) {
    if (str.empty()) return 0;
    try { return std::stoll(str); } catch (...) { return 0; }
}

static uint64_t safe_stoull(const std::string& str) {
    if (str.empty()) return 0;
    try { return std::stoull(str); } catch (...) { return 0; }
}

static uint32_t safe_stoul(const std::string& str) {
    if (str.empty()) return 0;
    try { return std::stoul(str); } catch (...) { return 0; }
}

CSVDataReader::~CSVDataReader() {
    clear();
}

bool CSVDataReader::loadFromCSV(const std::string& csv_filename) {
    std::ifstream file(csv_filename);
    if (!file.is_open()) {
        std::cerr << "[Error] Cannot open CSV file: " << csv_filename << std::endl;
        return false;
    }

    clear();
    std::string line;
    size_t line_num = 0;
    std::getline(file, line); // header
    line_num++;

    while (std::getline(file, line)) {
        line_num++;
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        while (std::getline(ss, field, ',')) {
            fields.push_back(field);
        }

        if (fields.size() < 10) continue;

        try {
            auto builder = std::make_unique<flatbuffers::FlatBufferBuilder>(512);
            OrderAction action = OrderAction_New;
            if (fields[3] == "Cancel") action = OrderAction_Cancel;
            else if (fields[3] == "Modify") action = OrderAction_Modify;

            Side side = (fields[4] == "Buy" || fields[4] == "buy") ? Side_Buy : Side_Sell;
            OrderType type = OrderType_Limit;
            if (fields[5] == "Market") type = OrderType_Market;

            uint64_t req_id       = safe_stoull(fields[0]);
            uint64_t order_id     = safe_stoull(fields[1]);
            uint32_t client_id    = safe_stoul(fields[2]);
            uint32_t symbol_id    = (fields.size() > 10) ? safe_stoul(fields[10]) : 1;
            int64_t  price        = safe_stoll(fields[6]);
            uint64_t quantity     = safe_stoull(fields[7]);
            uint64_t visible_qty  = (fields.size() > 8) ? safe_stoull(fields[8]) : 0;
            uint64_t timestamp    = safe_stoull(fields[9]);

            auto req_offset = CreateOrderRequest(*builder, action, req_id, order_id, client_id, symbol_id, side, type, price, quantity, visible_qty, timestamp);
            builder->Finish(req_offset);
            builders_.push_back(std::move(builder));
            requests_.push_back(flatbuffers::GetRoot<OrderRequest>(builders_.back()->GetBufferPointer()));
        } catch (...) {}
    }
    return true;
}

const std::vector<const OrderRequest*>& CSVDataReader::getRequests() const { return requests_; }
void CSVDataReader::clear() { builders_.clear(); requests_.clear(); }
size_t CSVDataReader::size() const { return requests_.size(); }

} // namespace Exchange
