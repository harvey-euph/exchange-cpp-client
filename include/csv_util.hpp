#pragma once
#include <cstdint>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include "fbs/order_generated.h"

namespace Exchange {

class CSVDataReader {
public:
    CSVDataReader() = default;
    ~CSVDataReader();

    bool loadFromCSV(const std::string& csv_filename);
    const std::vector<const OrderRequest*>& getRequests() const;
    void clear();
    size_t size() const;

private:
    std::vector<std::unique_ptr<flatbuffers::FlatBufferBuilder>> builders_;
    std::vector<const OrderRequest*> requests_;
};

} // namespace Exchange
