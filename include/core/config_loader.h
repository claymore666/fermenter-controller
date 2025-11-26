#pragma once

#include "config.h"
#include "hal/interfaces.h"

namespace core {

/**
 * Configuration Loader
 * Loads system configuration from JSON and NVS
 */
class ConfigLoader {
public:
    /**
     * Load configuration from JSON string
     * @param json JSON configuration string
     * @param config Output configuration struct
     * @return true on success, false on parse error
     */
    static bool load_from_json(const char* json, SystemConfig& config);

    /**
     * Load configuration from storage (NVS)
     * @param storage Storage interface
     * @param config Output configuration struct
     * @return true on success, false if not found or invalid
     */
    static bool load_from_storage(hal::IStorageInterface* storage, SystemConfig& config);

    /**
     * Save configuration to storage (NVS)
     * @param storage Storage interface
     * @param config Configuration to save
     * @return true on success
     */
    static bool save_to_storage(hal::IStorageInterface* storage, const SystemConfig& config);

    /**
     * Load default configuration
     * @param config Output configuration struct
     */
    static void load_defaults(SystemConfig& config);

    /**
     * Serialize configuration to JSON
     * @param config Configuration to serialize
     * @param buffer Output buffer
     * @param buffer_size Buffer size
     * @return Number of bytes written, 0 on error
     */
    static size_t to_json(const SystemConfig& config, char* buffer, size_t buffer_size);
};

} // namespace core
