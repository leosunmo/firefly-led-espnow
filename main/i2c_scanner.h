#pragma once

#include "driver/i2c_master.h"

/**
 * @brief Scan the I2C bus for connected devices
 * 
 * This function will scan all possible I2C addresses (1-127) and report
 * which devices respond with an ACK.
 * 
 * @param i2c_bus The I2C bus handle to scan
 */
void i2c_scan_bus(i2c_master_bus_handle_t i2c_bus);
