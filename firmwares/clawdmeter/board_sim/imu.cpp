#include "hal/imu_hal.h"
#include "sim_input.h"

void    imu_hal_init(void) {}
void    imu_hal_tick(void) {}
uint8_t imu_hal_rotation_quadrant(void) { return (uint8_t)sim_input().quadrant; }
