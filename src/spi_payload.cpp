#include "spi_payload.hpp"

void SPIPayload::init(const vector<string>& tx_vars, const vector<string>& rx_vars) {
  size_t max_vars = max(tx_vars.size(), rx_vars.size());

  // Basic Layout:
  // [Byte 0]     Start byte
  // [Byte 1-4]   msg_id
  size_t current_offset = 5;

  for(const auto& var : tx_vars) {
    _tx_offsets[var] = current_offset;
    current_offset += sizeof(float);
  }

  current_offset = 5;
  for(const auto& var : rx_vars) {
    _rx_offsets[var] = current_offset;
    current_offset += sizeof(float);
  }

  _check_idx = 5 + (max_vars * sizeof(float));
  size_t raw_size = _check_idx + 1;
  _total_bytes = ((raw_size + 31) / 32) * 32; // 32 Byte aligment for optimized memory-driven transmission

  _tx_buffer.resize(_total_bytes, 0);
  _rx_buffer.resize(_total_bytes, 0);
}

void SPIPayload::pack_tx(uint8_t start, uint32_t msg_id, const nlohmann::json& fmu_in) {
  std::fill(_tx_buffer.begin(), _tx_buffer.end(), 0); // all zeros at each iteration start
  
  _tx_buffer[0] = start;
  memcpy(&_tx_buffer[1], &msg_id, sizeof(msg_id));

  for(const auto& [var, offset] : _tx_offsets) {
    float val = fmu_in.value(var, 0.0f);
    memcpy(&_tx_buffer[offset], &val, sizeof(float)); 
  }

  uint8_t check = 0;
  for(size_t i = 0; i < _check_idx; i++) {
    check ^= _tx_buffer[i];
  }
  _tx_buffer[_check_idx] = check;
}

nlohmann::json SPIPayload::unpack_rx() {
  nlohmann::json out;
  
  if (_rx_buffer[0] != 0xBB) {
    cerr << "SPI RX starts with 0x" << _rx_buffer[0] << ". It must start with 0xBB" << endl;
    return out;
  }

  uint8_t calc_check = 0;
  for(size_t i = 0; i < _check_idx; i++) {
    calc_check ^= _rx_buffer[i];
  }
  
  if (calc_check != _rx_buffer[_check_idx]) {
    cerr << "SPIPayload Error: Invalid RX Checksum" << endl;
    return out;
  }

  uint32_t msg_id;
  memcpy(&msg_id, &_rx_buffer[1], sizeof(msg_id));
  out["msg_id"] = msg_id;

  for(const auto& [var, offset] : _rx_offsets) {
    float val;
    memcpy(&val, &_rx_buffer[offset], sizeof(float));
    out[var] = val;
  }

  return out;
}