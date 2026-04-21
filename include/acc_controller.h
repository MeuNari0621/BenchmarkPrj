/**
 * @file acc_controller.h
 * @brief Adaptive Cruise Control (ACC) Controller Interface
 *
 * This module implements the core ACC logic for automotive applications.
 * It handles state management, speed control, gap control, emergency braking,
 * driver override detection, and fault management.
 */

#ifndef ACC_CONTROLLER_H
#define ACC_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

#define ACC_MAX_ACCEL_MPS2          2.0f
#define ACC_MAX_DECEL_NORMAL_MPS2  (-3.5f)
#define ACC_MAX_DECEL_EMERG_MPS2   (-8.0f)
#define ACC_MIN_ACTIVE_SPEED_KPH   30.0f
#define ACC_MIN_SET_SPEED_KPH      30.0f
#define ACC_MAX_SET_SPEED_KPH     180.0f
#define ACC_OVERRIDE_TIMEOUT_MS   2000u
#define ACC_TTC_BRAKE_THRESHOLD    1.5f
#define ACC_TTC_RELEASE_THRESHOLD  3.0f
#define ACC_BRAKE_OVERRIDE_N      10.0f
#define ACC_SPEED_SENSOR_TIMEOUT  500u
#define ACC_RADAR_TIMEOUT         200u
#define ACC_FAULT_OK_DURATION    1000u
#define ACC_GAP_WARNING_50PCT     0.50f
#define ACC_GAP_WARNING_30PCT     0.30f
#define ACC_MIN_GAP_M             5.0f
#define ACC_ACCEL_OVERRIDE_MARGIN 5.0f

/*============================================================================
 * Type Definitions
 *============================================================================*/

/**
 * @brief ACC operating states
 */
typedef enum {
    ACC_STATE_STANDBY  = 0,  /**< ACC inactive, driver in full control */
    ACC_STATE_ACTIVE   = 1,  /**< ACC active, controlling speed/gap */
    ACC_STATE_OVERRIDE = 2,  /**< Driver override detected */
    ACC_STATE_FAULT    = 3   /**< Fault detected, ACC disabled */
} ACC_State;

/**
 * @brief Shift positions
 */
typedef enum {
    ACC_SHIFT_P = 0,  /**< Park */
    ACC_SHIFT_R = 1,  /**< Reverse */
    ACC_SHIFT_N = 2,  /**< Neutral */
    ACC_SHIFT_D = 3   /**< Drive */
} ACC_ShiftPosition;

/**
 * @brief Fault codes (bit flags)
 */
typedef enum {
    ACC_FAULT_NONE           = 0x0000,
    ACC_FAULT_SPEED_RANGE    = 0x0001,
    ACC_FAULT_SPEED_TIMEOUT  = 0x0002,
    ACC_FAULT_RADAR_RANGE    = 0x0004,
    ACC_FAULT_RADAR_TIMEOUT  = 0x0008,
    ACC_FAULT_CONTROL_ERROR  = 0x0010
} ACC_FaultCode;

/**
 * @brief Input structure containing all sensor data and driver commands
 */
typedef struct {
    /* Vehicle sensors */
    float   ego_speed_kph;           /**< Own vehicle speed [km/h] */
    float   target_distance_m;       /**< Distance to lead vehicle [m] */
    float   target_rel_speed_mps;    /**< Relative speed to lead [m/s], negative=approaching */
    bool    target_detected;         /**< Lead vehicle detected flag */
    float   longitudinal_accel_mps2; /**< Longitudinal acceleration [m/s²] */
    float   lateral_accel_mps2;      /**< Lateral acceleration [m/s²] */
    float   steering_angle_deg;      /**< Steering wheel angle [deg] */

    /* Driver interface */
    bool    acc_on_request;          /**< ACC ON switch pressed */
    bool    acc_off_request;         /**< ACC OFF switch pressed */
    bool    set_plus_request;        /**< SET+ switch pressed */
    bool    set_minus_request;       /**< SET- switch pressed */
    bool    resume_request;          /**< RESUME switch pressed */
    bool    gap_adjust_request;      /**< Gap time adjust switch pressed */

    /* Pedals and shift */
    bool    brake_pressed;           /**< Brake pedal pressed */
    float   brake_force_n;           /**< Brake pedal force [N] */
    float   accel_pedal_pct;         /**< Accelerator pedal position [%] */
    ACC_ShiftPosition shift_position; /**< Current shift position */

    /* System flags */
    bool    ignition_cycle_flag;     /**< Ignition OFF->ON detected (for fault reset) */
    bool    speed_sensor_valid;      /**< Speed sensor data valid this cycle */
    bool    radar_valid;             /**< Radar data valid this cycle */
} ACC_Input;

/**
 * @brief Output structure containing control commands and status
 */
typedef struct {
    float       accel_request_mps2;  /**< Acceleration request [m/s²] */
    ACC_State   state;               /**< Current ACC state */
    float       set_speed_kph;       /**< Set speed [km/h] */
    uint8_t     gap_setting;         /**< Gap time setting (0-3) */
    bool        warning_visual;      /**< Visual warning active */
    bool        warning_audio;       /**< Audio warning active */
    uint16_t    fault_code;          /**< Active fault codes (bitmask) */
    bool        emergency_brake;     /**< Emergency braking active */
} ACC_Output;

/**
 * @brief Internal controller state structure
 */
typedef struct {
    /* State management */
    ACC_State   state;
    uint16_t    fault_code;

    /* Speed control */
    float       set_speed_kph;
    float       previous_set_speed_kph;
    bool        set_plus_held;
    bool        set_minus_held;
    uint32_t    set_hold_timer_ms;

    /* Gap control */
    uint8_t     gap_setting;         /* 0=1.0s, 1=1.5s, 2=2.0s, 3=2.5s */
    bool        gap_button_prev;

    /* Override detection */
    bool        override_active;
    uint32_t    override_release_timer_ms;

    /* Emergency braking */
    bool        emergency_brake_active;

    /* Fault management */
    uint32_t    speed_sensor_timer_ms;
    uint32_t    radar_timer_ms;
    uint32_t    fault_ok_timer_ms;
    bool        fault_sensors_ok;

    /* Warnings */
    bool        warning_visual;
    bool        warning_audio;

    /* Control outputs */
    float       accel_speed_mps2;
    float       accel_gap_mps2;
    float       accel_final_mps2;
} ACC_Controller;

/*============================================================================
 * Public API
 *============================================================================*/

/**
 * @brief Initialize the ACC controller
 * @param ctrl Pointer to controller structure
 */
void ACC_Init(ACC_Controller* ctrl);

/**
 * @brief Update ACC state and calculate control outputs
 * @param ctrl Pointer to controller structure
 * @param input Pointer to input data
 * @param elapsed_ms Time elapsed since last update [ms]
 */
void ACC_Update(ACC_Controller* ctrl, const ACC_Input* input, uint32_t elapsed_ms);

/**
 * @brief Get current output values
 * @param ctrl Pointer to controller structure
 * @return Output structure with current values
 */
ACC_Output ACC_GetOutput(const ACC_Controller* ctrl);

#ifdef __cplusplus
}
#endif

#endif /* ACC_CONTROLLER_H */
