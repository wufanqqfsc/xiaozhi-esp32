#ifndef QMI8658_IMU_H
#define QMI8658_IMU_H

#include <driver/i2c_master.h>
#include <esp_err.h>

struct qmi8658_sens_data {
    struct {
        float x;
        float y;
        float z;
    } acc;
    struct {
        float x;
        float y;
        float z;
    } gyro;
};

class Qmi8658Imu {
public:
    static esp_err_t Initialize(i2c_master_bus_handle_t i2c_bus);
    static bool ReadAccelRaw(qmi8658_sens_data& data);

private:
    static i2c_master_dev_handle_t i2c_dev_;
    static esp_err_t WriteReg(uint8_t reg, uint8_t data);
    static esp_err_t ReadReg(uint8_t reg, uint8_t* data, size_t len);
};

#endif // QMI8658_IMU_H
