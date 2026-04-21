/**
 * @file acc_controller.c
 * @brief Adaptive Cruise Control (ACC) Controller Implementation
 */

#include "acc_controller.h"
#include <math.h>
#include <float.h>
#include <stddef.h>

/*============================================================================
 * Private Constants
 *============================================================================*/

static const float GAP_TIME_TABLE[4] = {1.0f, 1.5f, 2.0f, 2.5f};

/* Control gains */
#define KP_SPEED    0.5f
#define KP_GAP      0.3f
#define KD_GAP      0.5f

/* Speed conversion */
#define KPH_TO_MPS  (1.0f / 3.6f)
#define MPS_TO_KPH  3.6f

/*============================================================================
 * Private Helper Functions
 *============================================================================*/

static float clamp_f(float value, float min_val, float max_val)
{
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

static float min_f(float a, float b)
{
    return (a < b) ? a : b;
}

/**
 * @brief Calculate Time To Collision (TTC)
 * @param distance_m Distance to lead vehicle [m]
 * @param rel_speed_mps Relative speed [m/s], negative means approaching
 * @return TTC in seconds, FLT_MAX if not approaching
 */
static float calc_ttc(float distance_m, float rel_speed_mps)
{
    if (rel_speed_mps >= 0.0f) {
        return FLT_MAX;  /* Not approaching, no collision */
    }
    if (distance_m <= 0.0f) {
        return 0.0f;
    }
    return distance_m / (-rel_speed_mps);
}

/**
 * @brief Check if speed sensor value is within valid range
 */
static bool is_speed_valid(float speed_kph)
{
    return (speed_kph >= -1.0f && speed_kph <= 251.0f);
}

/**
 * @brief Check if radar value is within valid range
 */
static bool is_radar_distance_valid(float distance_m)
{
    return (distance_m >= 0.0f && distance_m <= 201.0f);
}

/*============================================================================
 * Fault Management
 *============================================================================*/

static void fault_mgr_update(ACC_Controller* ctrl, const ACC_Input* input, uint32_t elapsed_ms)
{
    uint16_t new_fault = ACC_FAULT_NONE;

    /* Speed sensor check */
    if (!is_speed_valid(input->ego_speed_kph)) {
        new_fault |= ACC_FAULT_SPEED_RANGE;
    }

    if (input->speed_sensor_valid) {
        ctrl->speed_sensor_timer_ms = 0;
    } else {
        ctrl->speed_sensor_timer_ms += elapsed_ms;
        if (ctrl->speed_sensor_timer_ms > ACC_SPEED_SENSOR_TIMEOUT) {
            new_fault |= ACC_FAULT_SPEED_TIMEOUT;
        }
    }

    /* Radar sensor check */
    if (input->target_detected && !is_radar_distance_valid(input->target_distance_m)) {
        new_fault |= ACC_FAULT_RADAR_RANGE;
    }

    if (input->radar_valid) {
        ctrl->radar_timer_ms = 0;
    } else {
        ctrl->radar_timer_ms += elapsed_ms;
        if (ctrl->radar_timer_ms > ACC_RADAR_TIMEOUT) {
            new_fault |= ACC_FAULT_RADAR_TIMEOUT;
        }
    }

    ctrl->fault_code = new_fault;

    /* Track fault-free duration for reset */
    if (new_fault == ACC_FAULT_NONE) {
        ctrl->fault_ok_timer_ms += elapsed_ms;
        ctrl->fault_sensors_ok = (ctrl->fault_ok_timer_ms >= ACC_FAULT_OK_DURATION);
    } else {
        ctrl->fault_ok_timer_ms = 0;
        ctrl->fault_sensors_ok = false;
    }
}

/*============================================================================
 * State Management
 *============================================================================*/

static void state_mgr_update(ACC_Controller* ctrl, const ACC_Input* input, uint32_t elapsed_ms)
{
    ACC_State current = ctrl->state;
    ACC_State next = current;

    bool can_activate = (input->ego_speed_kph >= ACC_MIN_ACTIVE_SPEED_KPH) &&
                        (input->shift_position == ACC_SHIFT_D) &&
                        (ctrl->fault_code == ACC_FAULT_NONE);

    bool brake_override = (input->brake_force_n > ACC_BRAKE_OVERRIDE_N);

    /* Calculate accel override threshold (set_speed + 5 km/h equivalent) */
    /* Simplified: assume 100% accel = 180 km/h target, linear mapping */
    float set_plus_margin_pct = ((ctrl->set_speed_kph + ACC_ACCEL_OVERRIDE_MARGIN) / 180.0f) * 100.0f;
    bool accel_override = (input->accel_pedal_pct > set_plus_margin_pct);

    bool override_detected = brake_override || accel_override;

    switch (current) {
        case ACC_STATE_STANDBY:
            if (input->acc_on_request && can_activate) {
                next = ACC_STATE_ACTIVE;
                /* Initialize set speed to current speed rounded to nearest 5 */
                ctrl->set_speed_kph = roundf(input->ego_speed_kph / 5.0f) * 5.0f;
                ctrl->set_speed_kph = clamp_f(ctrl->set_speed_kph,
                                             ACC_MIN_SET_SPEED_KPH,
                                             ACC_MAX_SET_SPEED_KPH);
                /* Note: Do NOT overwrite previous_set_speed here - it's preserved for RESUME */
            }
            break;

        case ACC_STATE_ACTIVE:
            if (input->acc_off_request) {
                next = ACC_STATE_STANDBY;
            } else if (input->shift_position != ACC_SHIFT_D) {
                next = ACC_STATE_STANDBY;
            } else if (ctrl->fault_code != ACC_FAULT_NONE) {
                next = ACC_STATE_FAULT;
            } else if (override_detected) {
                next = ACC_STATE_OVERRIDE;
                ctrl->override_active = true;
                ctrl->override_release_timer_ms = 0;
            }
            break;

        case ACC_STATE_OVERRIDE:
            /* Check for low speed exit */
            if (input->ego_speed_kph < (ACC_MIN_ACTIVE_SPEED_KPH - 5.0f)) {  /* 25 km/h */
                next = ACC_STATE_STANDBY;
            } else if (ctrl->fault_code != ACC_FAULT_NONE) {
                next = ACC_STATE_FAULT;
            } else if (!override_detected) {
                ctrl->override_release_timer_ms += elapsed_ms;
                if (ctrl->override_release_timer_ms >= ACC_OVERRIDE_TIMEOUT_MS) {
                    if (input->ego_speed_kph >= (ACC_MIN_ACTIVE_SPEED_KPH - 5.0f)) {
                        next = ACC_STATE_ACTIVE;
                        ctrl->override_active = false;
                    } else {
                        next = ACC_STATE_STANDBY;
                    }
                }
            } else {
                ctrl->override_release_timer_ms = 0;
            }
            break;

        case ACC_STATE_FAULT:
            if (ctrl->fault_sensors_ok && input->ignition_cycle_flag) {
                next = ACC_STATE_STANDBY;
                ctrl->fault_code = ACC_FAULT_NONE;
            }
            break;

        default:
            next = ACC_STATE_FAULT;
            break;
    }

    ctrl->state = next;
}

/*============================================================================
 * Speed Control
 *============================================================================*/

static void speed_ctrl_update(ACC_Controller* ctrl, const ACC_Input* input, uint32_t elapsed_ms)
{
    bool speed_changed = false;
    
    /* SET+ processing */
    if (input->set_plus_request) {
        if (!ctrl->set_plus_held) {
            /* Initial press: +1 km/h */
            ctrl->set_speed_kph += 1.0f;
            ctrl->set_plus_held = true;
            ctrl->set_hold_timer_ms = 0;
            speed_changed = true;
        } else {
            ctrl->set_hold_timer_ms += elapsed_ms;
            if (ctrl->set_hold_timer_ms >= 500) {
                /* Long press: +5 km/h per second */
                ctrl->set_speed_kph += 5.0f * (elapsed_ms / 1000.0f);
                speed_changed = true;
            }
        }
    } else {
        ctrl->set_plus_held = false;
    }

    /* SET- processing */
    if (input->set_minus_request) {
        if (!ctrl->set_minus_held) {
            /* Initial press: -1 km/h */
            ctrl->set_speed_kph -= 1.0f;
            ctrl->set_minus_held = true;
            ctrl->set_hold_timer_ms = 0;
            speed_changed = true;
        } else {
            ctrl->set_hold_timer_ms += elapsed_ms;
            if (ctrl->set_hold_timer_ms >= 500) {
                /* Long press: -5 km/h per second */
                ctrl->set_speed_kph -= 5.0f * (elapsed_ms / 1000.0f);
                speed_changed = true;
            }
        }
    } else {
        ctrl->set_minus_held = false;
    }

    /* RESUME processing */
    if (input->resume_request && ctrl->previous_set_speed_kph >= ACC_MIN_SET_SPEED_KPH) {
        ctrl->set_speed_kph = ctrl->previous_set_speed_kph;
    }

    /* Clamp set speed */
    ctrl->set_speed_kph = clamp_f(ctrl->set_speed_kph,
                                  ACC_MIN_SET_SPEED_KPH,
                                  ACC_MAX_SET_SPEED_KPH);

    /* Store for RESUME - update when SET+/SET- changes the speed */
    if (speed_changed) {
        ctrl->previous_set_speed_kph = ctrl->set_speed_kph;
    }

    /* Calculate acceleration for speed maintenance */
    float ego_speed_mps = input->ego_speed_kph * KPH_TO_MPS;
    float set_speed_mps = ctrl->set_speed_kph * KPH_TO_MPS;
    float speed_error = set_speed_mps - ego_speed_mps;

    ctrl->accel_speed_mps2 = KP_SPEED * speed_error;

    /* Speed control only accelerates, does not decelerate */
    if (ctrl->accel_speed_mps2 < 0.0f) {
        ctrl->accel_speed_mps2 = 0.0f;
    }
    ctrl->accel_speed_mps2 = clamp_f(ctrl->accel_speed_mps2, 0.0f, ACC_MAX_ACCEL_MPS2);
}

/*============================================================================
 * Gap Control
 *============================================================================*/

static void gap_ctrl_update(ACC_Controller* ctrl, const ACC_Input* input)
{
    /* Gap button toggle */
    if (input->gap_adjust_request && !ctrl->gap_button_prev) {
        ctrl->gap_setting = (ctrl->gap_setting + 1) % 4;
    }
    ctrl->gap_button_prev = input->gap_adjust_request;

    /* Clear warnings */
    ctrl->warning_visual = false;
    ctrl->warning_audio = false;

    /* No target: no gap control */
    if (!input->target_detected) {
        ctrl->accel_gap_mps2 = ACC_MAX_ACCEL_MPS2;  /* No constraint */
        return;
    }

    float ego_speed_mps = input->ego_speed_kph * KPH_TO_MPS;

    /* Calculate target gap distance */
    float gap_time = GAP_TIME_TABLE[ctrl->gap_setting];
    float target_gap_m = gap_time * ego_speed_mps;
    if (target_gap_m < ACC_MIN_GAP_M) {
        target_gap_m = ACC_MIN_GAP_M;
    }

    /* Calculate gap error */
    float gap_error = input->target_distance_m - target_gap_m;
    float closing_rate = -input->target_rel_speed_mps;  /* positive = approaching */

    /* PD control for gap */
    ctrl->accel_gap_mps2 = KP_GAP * gap_error - KD_GAP * closing_rate;

    /* Apply limits */
    ctrl->accel_gap_mps2 = clamp_f(ctrl->accel_gap_mps2,
                                   ACC_MAX_DECEL_NORMAL_MPS2,
                                   ACC_MAX_ACCEL_MPS2);

    /* Gap warnings */
    if (ego_speed_mps > 0.1f) {
        float actual_gap_time = input->target_distance_m / ego_speed_mps;

        if (actual_gap_time < gap_time * ACC_GAP_WARNING_30PCT) {
            ctrl->warning_visual = true;
            ctrl->warning_audio = true;
        } else if (actual_gap_time < gap_time * ACC_GAP_WARNING_50PCT) {
            ctrl->warning_visual = true;
        }
    }
}

/*============================================================================
 * Emergency Braking
 *============================================================================*/

static void emergency_ctrl_update(ACC_Controller* ctrl, const ACC_Input* input)
{
    if (!input->target_detected) {
        ctrl->emergency_brake_active = false;
        return;
    }

    float ttc = calc_ttc(input->target_distance_m, input->target_rel_speed_mps);

    /* Activation: TTC < 1.5s AND approaching */
    if (ttc < ACC_TTC_BRAKE_THRESHOLD && input->target_rel_speed_mps < 0.0f) {
        ctrl->emergency_brake_active = true;
    }

    /* Release: TTC > 3.0s OR target lost */
    if (ttc > ACC_TTC_RELEASE_THRESHOLD) {
        ctrl->emergency_brake_active = false;
    }
}

/*============================================================================
 * Acceleration Arbitration
 *============================================================================*/

static void arbitrate_accel(ACC_Controller* ctrl)
{
    float accel = ctrl->accel_speed_mps2;

    /* Select minimum (most conservative) */
    accel = min_f(accel, ctrl->accel_gap_mps2);

    /* Apply emergency braking if active */
    if (ctrl->emergency_brake_active) {
        accel = ACC_MAX_DECEL_EMERG_MPS2;
    }

    /* Final limits */
    ctrl->accel_final_mps2 = clamp_f(accel, ACC_MAX_DECEL_EMERG_MPS2, ACC_MAX_ACCEL_MPS2);
}

/*============================================================================
 * Public API Implementation
 *============================================================================*/

void ACC_Init(ACC_Controller* ctrl)
{
    if (ctrl == NULL) return;

    ctrl->state = ACC_STATE_STANDBY;
    ctrl->fault_code = ACC_FAULT_NONE;

    ctrl->set_speed_kph = 0.0f;
    ctrl->previous_set_speed_kph = 0.0f;
    ctrl->set_plus_held = false;
    ctrl->set_minus_held = false;
    ctrl->set_hold_timer_ms = 0;

    ctrl->gap_setting = 1;  /* Default 1.5 seconds */
    ctrl->gap_button_prev = false;

    ctrl->override_active = false;
    ctrl->override_release_timer_ms = 0;

    ctrl->emergency_brake_active = false;

    ctrl->speed_sensor_timer_ms = 0;
    ctrl->radar_timer_ms = 0;
    ctrl->fault_ok_timer_ms = 0;
    ctrl->fault_sensors_ok = false;

    ctrl->warning_visual = false;
    ctrl->warning_audio = false;

    ctrl->accel_speed_mps2 = 0.0f;
    ctrl->accel_gap_mps2 = 0.0f;
    ctrl->accel_final_mps2 = 0.0f;
}

void ACC_Update(ACC_Controller* ctrl, const ACC_Input* input, uint32_t elapsed_ms)
{
    if (ctrl == NULL || input == NULL) return;

    /* 1. Fault management (sensor diagnosis) */
    fault_mgr_update(ctrl, input, elapsed_ms);

    /* 2. State management */
    state_mgr_update(ctrl, input, elapsed_ms);

    /* 3. Control calculations (only when active) */
    if (ctrl->state == ACC_STATE_ACTIVE) {
        speed_ctrl_update(ctrl, input, elapsed_ms);
        gap_ctrl_update(ctrl, input);
        emergency_ctrl_update(ctrl, input);
        arbitrate_accel(ctrl);
    } else if (ctrl->state == ACC_STATE_OVERRIDE) {
        /* Keep tracking but output 0 */
        ctrl->accel_final_mps2 = 0.0f;
        ctrl->warning_visual = false;
        ctrl->warning_audio = false;
    } else {
        /* STANDBY or FAULT: no control */
        ctrl->accel_final_mps2 = 0.0f;
        ctrl->warning_visual = false;
        ctrl->warning_audio = false;
        ctrl->emergency_brake_active = false;
    }
}

ACC_Output ACC_GetOutput(const ACC_Controller* ctrl)
{
    ACC_Output output;

    if (ctrl == NULL) {
        output.accel_request_mps2 = 0.0f;
        output.state = ACC_STATE_FAULT;
        output.set_speed_kph = 0.0f;
        output.gap_setting = 1;
        output.warning_visual = false;
        output.warning_audio = false;
        output.fault_code = ACC_FAULT_CONTROL_ERROR;
        output.emergency_brake = false;
        return output;
    }

    output.accel_request_mps2 = ctrl->accel_final_mps2;
    output.state = ctrl->state;
    output.set_speed_kph = ctrl->set_speed_kph;
    output.gap_setting = ctrl->gap_setting;
    output.warning_visual = ctrl->warning_visual;
    output.warning_audio = ctrl->warning_audio;
    output.fault_code = ctrl->fault_code;
    output.emergency_brake = ctrl->emergency_brake_active;

    return output;
}
