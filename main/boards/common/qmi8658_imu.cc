#include "qmi8658_imu.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Qmi8658Imu"
#define QMI8658_I2C_ADDR 0x6B

#define QMI8658_REG_WHO_AM_I 0x00
#define QMI8658_REG_CTRL1    0x02
#define QMI8658_REG_CTRL2    0x03
#define QMI8658_REG_CTRL3    0x04
#define QMI8658_REG_CTRL7    0x08
#define QMI8658_REG_AX_L     0x35

i2c_master_dev_handle_t Qmi8658Imu::i2c_dev_ = nullptr;

esp_err_t Qmi8658Imu::WriteReg(uint8_t reg, uint8_t data) {
    if (!i2c_dev_) return ESP_FAIL;
    uint8_t buf[2] = {reg, data};
    return i2c_master_transmit(i2c_dev_, buf, sizeof(buf), -1);
}

esp_err_t Qmi8658Imu::ReadReg(uint8_t reg, uint8_t* data, size_t len) {
    if (!i2c_dev_) return ESP_FAIL;
    return i2c_master_transmit_receive(i2c_dev_, &reg, 1, data, len, -1);
}

esp_err_t Qmi8658Imu::Initialize(i2c_master_bus_handle_t i2c_bus) {
    if (i2c_dev_ != nullptr) {
        return ESP_OK;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = QMI8658_I2C_ADDR,
        .scl_speed_hz = 400000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &i2c_dev_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add QMI8658 to I2C bus");
        return ret;
    }

    uint8_t who_am_i = 0;
    ret = ReadReg(QMI8658_REG_WHO_AM_I, &who_am_i, 1);
    if (ret != ESP_OK || who_am_i != 0x05) {
        ESP_LOGE(TAG, "QMI8658 not found, WHO_AM_I=0x%02x", who_am_i);
        i2c_master_bus_rm_device(i2c_dev_);
        i2c_dev_ = nullptr;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "QMI8658 found, WHO_AM_I=0x%02x", who_am_i);

    // Reset
    WriteReg(0x60, 0xb0);
    vTaskDelay(pdMS_TO_TICKS(20));

    // CTRL1: SPI/I2C config (Auto increment, Address increment)
    WriteReg(QMI8658_REG_CTRL1, 0x40); // Serial Interface and Sensor Enable
    // CTRL2: Accel config (±4g, 125Hz)
    WriteReg(QMI8658_REG_CTRL2, 0x14); // aODR=125Hz, aFS=4g
    // CTRL3: Gyro config (±512dps, 125Hz)
    WriteReg(QMI8658_REG_CTRL3, 0x54); // gODR=125Hz, gFS=512dps
    // CTRL7: Enable Accel and Gyro
    WriteReg(QMI8658_REG_CTRL7, 0x03); // aEN=1, gEN=1

    return ESP_OK;
}

bool Qmi8658Imu::ReadAccelRaw(qmi8658_sens_data& data) {
    if (!i2c_dev_) return false;

    uint8_t buf[12];
    esp_err_t ret = ReadReg(QMI8658_REG_AX_L, buf, 12);
    if (ret != ESP_OK) return false;

    int16_t ax = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t ay = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t az = (int16_t)((buf[5] << 8) | buf[4]);

    int16_t gx = (int16_t)((buf[7] << 8) | buf[6]);
    int16_t gy = (int16_t)((buf[9] << 8) | buf[8]);
    int16_t gz = (int16_t)((buf[11] << 8) | buf[10]);

    data.acc.x = ax;
    data.acc.y = ay;
    data.acc.z = az;
    data.gyro.x = gx;
    data.gyro.y = gy;
    data.gyro.z = gz;

    return true;
}
