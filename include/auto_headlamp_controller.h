#ifndef AUTO_HEADLAMP_CONTROLLER_H
#define AUTO_HEADLAMP_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    AHC_MODE_AUTO = 0,
    AHC_MODE_FORCE_ON = 1,
    AHC_MODE_FORCE_OFF = 2
} AHC_OverrideMode;

typedef struct {
    int ambient_lux;
    int vehicle_speed_kph;
    bool tunnel_detected;
    bool manual_override_request;
    AHC_OverrideMode requested_override_mode;
} AHC_Input;

typedef struct {
    bool lamp_on;
    bool fault_sensor_range;
    AHC_OverrideMode active_mode;
    uint32_t override_remaining_ms;
} AHC_Output;

typedef struct {
    bool lamp_on;
    bool has_valid_input;
    bool fault_sensor_range;
    AHC_OverrideMode active_mode;
    uint32_t override_remaining_ms;
    uint8_t dark_count;
    uint8_t bright_count;
} AHC_Controller;

void AHC_Init(AHC_Controller* controller);
void AHC_Update(AHC_Controller* controller, const AHC_Input* input, uint32_t elapsed_ms);
AHC_Output AHC_GetOutput(const AHC_Controller* controller);

#endif
