#ifndef SPI_PAYLOAD_HPP
#define SPI_PAYLOAD_HPP

#include <vector>
#include <string>
#include <map>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace std;

class SPIPayload {
private:
  map<string, size_t> _tx_offsets;
  map<string, size_t> _rx_offsets;
  size_t _total_bytes;
  size_t _check_idx;
    
  vector<uint8_t> _tx_buffer;
  vector<uint8_t> _rx_buffer;

public:
  SPIPayload() = default;
  ~SPIPayload() = default;

  /**
   * 
   * @brief Memory initialization according to `.ini` section
   * @param tx_vars MADS -> SPI variables
   * @param rx_vars SPI --> MADS variables
  */
  void init(const vector<string>& tx_vars, const vector<string>& rx_vars);
    
  // Converte il JSON MADS nel buffer binario da spedire
  void pack_tx(uint8_t start, uint32_t msg_id, const nlohmann::json& fmu_in);
    
  nlohmann::json unpack_rx();

  uint8_t* SPIPayload::get_tx_data() { return _tx_buffer.data(); }
  uint8_t* SPIPayload::get_rx_data() { return _rx_buffer.data(); }
  size_t SPIPayload::get_total_bytes() const { return _total_bytes; }
};

#endif