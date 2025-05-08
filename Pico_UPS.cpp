#include "Pico_UPS.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

class INA219 {
public:
    INA219(uint8_t addr = INA219_ADDRESS);
    void begin();
    void setCalibration_32V_2A();
    float getBusVoltage_V();
    float getShuntVoltage_mV();
    float getCurrent_mA();
    float getPower_mW();
    void powerSave(bool on);
    void wireWriteRegister(uint8_t reg, uint16_t value);
    void wireReadRegister(uint8_t reg, uint16_t *value);
private:
    uint8_t ina219_i2caddr;
    uint32_t ina219_calValue;
    uint32_t ina219_currentDivider_mA;
    float ina219_powerMultiplier_mW;
};

INA219::INA219(uint8_t addr) {
    ina219_i2caddr = addr;
    ina219_currentDivider_mA = 0;
    ina219_powerMultiplier_mW = 0.0f;
}

void INA219::wireWriteRegister(uint8_t reg, uint16_t value) {
    uint8_t tmpi[3];
    tmpi[0] = reg;
    tmpi[1] = (value >> 8) & 0xFF;
    tmpi[2] = value & 0xFF;
    i2c_write_blocking(i2c1, ina219_i2caddr, tmpi, 3, false);
}

void INA219::wireReadRegister(uint8_t reg, uint16_t *value) {
    uint8_t tmpi[2];
    i2c_write_blocking(i2c1, ina219_i2caddr, &reg, 1, true);
    i2c_read_blocking(i2c1, ina219_i2caddr, tmpi, 2, false);
    *value = (((uint16_t)tmpi[0] << 8) | (uint16_t)tmpi[1]);
}

void INA219::setCalibration_32V_2A() {
    ina219_calValue = 4096;
    ina219_currentDivider_mA = 1.0;
    ina219_powerMultiplier_mW = 20;
    wireWriteRegister(INA219_REG_CALIBRATION, ina219_calValue);
    uint16_t config = INA219_CONFIG_BVOLTAGERANGE_32V |
                      INA219_CONFIG_GAIN_8_320MV | INA219_CONFIG_BADCRES_12BIT |
                      INA219_CONFIG_SADCRES_12BIT_32S_17MS |
                      INA219_CONFIG_MODE_SANDBVOLT_CONTINUOUS;
    wireWriteRegister(INA219_REG_CONFIG, config);
}

void INA219::powerSave(bool on) {
    uint16_t current;
    wireReadRegister(INA219_REG_CONFIG, &current);
    uint16_t next = on ? (current | INA219_CONFIG_MODE_POWERDOWN) : 
                         (current & ~INA219_CONFIG_MODE_POWERDOWN);
    wireWriteRegister(INA219_REG_CONFIG, next);
}

void INA219::begin() {
    i2c_init(i2c1, 400 * 1000);
    gpio_set_function(6, GPIO_FUNC_I2C);
    gpio_set_function(7, GPIO_FUNC_I2C);
    gpio_pull_up(6);
    gpio_pull_up(7);
    setCalibration_32V_2A();
}

float INA219::getShuntVoltage_mV() {
    uint16_t value;
    wireReadRegister(INA219_REG_SHUNTVOLTAGE, &value);
    return (int16_t)value * 0.01;
}

float INA219::getBusVoltage_V() {
    uint16_t value;
    wireReadRegister(INA219_REG_BUSVOLTAGE, &value);
    return (int16_t)((value >> 3) * 4) * 0.001;
}

float INA219::getCurrent_mA() {
    uint16_t value;
    wireWriteRegister(INA219_REG_CALIBRATION, ina219_calValue);
    wireReadRegister(INA219_REG_CURRENT, &value);
    float valueDec = (int16_t)value;
    valueDec /= ina219_currentDivider_mA;
    return valueDec;
}

float INA219::getPower_mW() {
    uint16_t value;
    wireWriteRegister(INA219_REG_CALIBRATION, ina219_calValue);
    wireReadRegister(INA219_REG_POWER, &value);
    float valueDec = (int16_t)value;
    valueDec *= ina219_powerMultiplier_mW;
    return valueDec;
}

// C interface implementations
extern "C" {
    INA219Handle INA219_create(uint8_t addr) {
        return new INA219(addr);
    }

    void INA219_destroy(INA219Handle handle) {
        delete static_cast<INA219*>(handle);
    }

    void INA219_begin(INA219Handle handle) {
        static_cast<INA219*>(handle)->begin();
    }

    void INA219_setCalibration_32V_2A(INA219Handle handle) {
        static_cast<INA219*>(handle)->setCalibration_32V_2A();
    }

    float INA219_getBusVoltage_V(INA219Handle handle) {
        return static_cast<INA219*>(handle)->getBusVoltage_V();
    }

    float INA219_getShuntVoltage_mV(INA219Handle handle) {
        return static_cast<INA219*>(handle)->getShuntVoltage_mV();
    }

    float INA219_getCurrent_mA(INA219Handle handle) {
        return static_cast<INA219*>(handle)->getCurrent_mA();
    }

    float INA219_getPower_mW(INA219Handle handle) {
        return static_cast<INA219*>(handle)->getPower_mW();
    }

    void INA219_powerSave(INA219Handle handle, int on) {
        static_cast<INA219*>(handle)->powerSave(on != 0);
    }
}

