#pragma once
#include "pocket/config.hpp"

/** ESP: durable DeviceConfig — always NVS; mirror to SD when mounted + space OK. */
class EspPersistentConfigStore : public pocket::ConfigStore {
 public:
  pocket::DeviceConfig load() override;
  void save(const pocket::DeviceConfig& cfg) override;
};
