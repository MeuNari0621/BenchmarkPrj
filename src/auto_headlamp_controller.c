#include "auto_headlamp_controller.h"

#define AHC_DARK_ON_THRESHOLD_LUX 300
#define AHC_BRIGHT_OFF_THRESHOLD_LUX 500
#define AHC_DEBOUNCE_COUNT 3
#define AHC_MIN_SPEED_KPH 10
#define AHC_MANUAL_OVERRIDE_TIMEOUT_MS 120000u
#define AHC_MIN_VALID_LUX 0
#define AHC_MAX_VALID_LUX 200000

static bool is_sensor_valid(int lux)
{
    return lux >= AHC_MIN_VALID_LUX && lux <= AHC_MAX_VALID_LUX;
}

void AHC_Init(AHC_Controller* controller)
{
    controller->lamp_on = false;
    controller->has_valid_input = false;
    controller->fault_sensor_range = false;
    controller->active_mode = AHC_MODE_AUTO;
    controller->override_remaining_ms = 0u;
    controller->dark_count = 0u;
    controller->bright_count = 0u;
}

void AHC_Update(AHC_Controller* controller, const AHC_Input* input, uint32_t elapsed_ms)
{
    if (input->manual_override_request) {
        controller->active_mode = input->requested_override_mode;
        controller->override_remaining_ms =
            (input->requested_override_mode == AHC_MODE_AUTO) ? 0u : AHC_MANUAL_OVERRIDE_TIMEOUT_MS;
    } else if (controller->active_mode != AHC_MODE_AUTO) {
        if (elapsed_ms >= controller->override_remaining_ms) {
            controller->active_mode = AHC_MODE_AUTO;
            controller->override_remaining_ms = 0u;
        } else {
            controller->override_remaining_ms -= elapsed_ms;
        }
    }

    if (!is_sensor_valid(input->ambient_lux)) {
        controller->fault_sensor_range = true;
        return;
    }

    controller->fault_sensor_range = false;
    controller->has_valid_input = true;

    if (controller->active_mode == AHC_MODE_FORCE_ON) {
        controller->lamp_on = true;
        return;
    }

    if (controller->active_mode == AHC_MODE_FORCE_OFF) {
        controller->lamp_on = false;
        return;
    }

    if (input->tunnel_detected) {
        controller->lamp_on = true;
        controller->dark_count = 0u;
        controller->bright_count = 0u;
        return;
    }

    if (!controller->has_valid_input) {
        controller->lamp_on = false;
        return;
    }

    if (input->vehicle_speed_kph < AHC_MIN_SPEED_KPH) {
        controller->lamp_on = false;
        controller->dark_count = 0u;
        controller->bright_count = 0u;
        return;
    }

    if (input->ambient_lux <= AHC_DARK_ON_THRESHOLD_LUX) {
        controller->dark_count++;
        controller->bright_count = 0u;
        if (controller->dark_count >= AHC_DEBOUNCE_COUNT) {
            controller->lamp_on = true;
        }
        return;
    }

    if (input->ambient_lux >= AHC_BRIGHT_OFF_THRESHOLD_LUX) {
        controller->bright_count++;
        controller->dark_count = 0u;
        if (controller->bright_count >= AHC_DEBOUNCE_COUNT) {
            controller->lamp_on = false;
        }
        return;
    }

    controller->dark_count = 0u;
    controller->bright_count = 0u;
}

AHC_Output AHC_GetOutput(const AHC_Controller* controller)
{
    AHC_Output output;
    output.lamp_on = controller->lamp_on;
    output.fault_sensor_range = controller->fault_sensor_range;
    output.active_mode = controller->active_mode;
    output.override_remaining_ms = controller->override_remaining_ms;
    return output;
}
