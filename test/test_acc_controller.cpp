/**
 * @file test_acc_controller.cpp
 * @brief Unit tests for ACC Controller
 */

#include <gtest/gtest.h>
#include <cmath>

extern "C" {
#include "acc_controller.h"
}

namespace {

/*============================================================================
 * Test Fixtures and Helpers
 *============================================================================*/

class ACCControllerTest : public ::testing::Test {
protected:
    ACC_Controller ctrl;
    ACC_Input input;

    void SetUp() override {
        ACC_Init(&ctrl);
        resetInput();
    }

    void resetInput() {
        input = {};
        input.ego_speed_kph = 50.0f;
        input.target_distance_m = 100.0f;
        input.target_rel_speed_mps = 0.0f;
        input.target_detected = false;
        input.shift_position = ACC_SHIFT_D;
        input.speed_sensor_valid = true;
        input.radar_valid = true;
    }

    void activateACC() {
        input.acc_on_request = true;
        input.ego_speed_kph = 50.0f;
        input.shift_position = ACC_SHIFT_D;
        ACC_Update(&ctrl, &input, 20);
        input.acc_on_request = false;
    }

    void updateN(int count, uint32_t elapsed_ms = 20) {
        for (int i = 0; i < count; ++i) {
            ACC_Update(&ctrl, &input, elapsed_ms);
        }
    }
};

/*============================================================================
 * FR-001: State Management
 *============================================================================*/

TEST_F(ACCControllerTest, InitialStateIsStandby) {
    EXPECT_EQ(ctrl.state, ACC_STATE_STANDBY);
}

TEST_F(ACCControllerTest, StateEnumValues) {
    EXPECT_EQ(ACC_STATE_STANDBY, 0);
    EXPECT_EQ(ACC_STATE_ACTIVE, 1);
    EXPECT_EQ(ACC_STATE_OVERRIDE, 2);
    EXPECT_EQ(ACC_STATE_FAULT, 3);
}

/*============================================================================
 * FR-002: State Transitions
 *============================================================================*/

TEST_F(ACCControllerTest, StandbyToActiveOnAccOn) {
    input.acc_on_request = true;
    input.ego_speed_kph = 50.0f;
    input.shift_position = ACC_SHIFT_D;

    ACC_Update(&ctrl, &input, 20);

    EXPECT_EQ(ctrl.state, ACC_STATE_ACTIVE);
}

TEST_F(ACCControllerTest, StandbyRemainsWhenSpeedTooLow) {
    input.acc_on_request = true;
    input.ego_speed_kph = 25.0f;  // Below 30 km/h
    input.shift_position = ACC_SHIFT_D;

    ACC_Update(&ctrl, &input, 20);

    EXPECT_EQ(ctrl.state, ACC_STATE_STANDBY);
}

TEST_F(ACCControllerTest, StandbyRemainsWhenNotInDrive) {
    input.acc_on_request = true;
    input.ego_speed_kph = 50.0f;
    input.shift_position = ACC_SHIFT_N;

    ACC_Update(&ctrl, &input, 20);

    EXPECT_EQ(ctrl.state, ACC_STATE_STANDBY);
}

TEST_F(ACCControllerTest, ActiveToStandbyOnAccOff) {
    activateACC();
    EXPECT_EQ(ctrl.state, ACC_STATE_ACTIVE);

    input.acc_off_request = true;
    ACC_Update(&ctrl, &input, 20);

    EXPECT_EQ(ctrl.state, ACC_STATE_STANDBY);
}

TEST_F(ACCControllerTest, ActiveToStandbyOnShiftChange) {
    activateACC();

    input.shift_position = ACC_SHIFT_N;
    ACC_Update(&ctrl, &input, 20);

    EXPECT_EQ(ctrl.state, ACC_STATE_STANDBY);
}

TEST_F(ACCControllerTest, ActiveToOverrideOnBrake) {
    activateACC();

    input.brake_force_n = 15.0f;  // Above 10N threshold
    ACC_Update(&ctrl, &input, 20);

    EXPECT_EQ(ctrl.state, ACC_STATE_OVERRIDE);
}

TEST_F(ACCControllerTest, ActiveToOverrideOnAccelOverride) {
    activateACC();

    // Set speed at 50 km/h, accel override when > (50+5)/180 * 100 = ~30.5%
    ctrl.set_speed_kph = 50.0f;
    input.accel_pedal_pct = 35.0f;
    ACC_Update(&ctrl, &input, 20);

    EXPECT_EQ(ctrl.state, ACC_STATE_OVERRIDE);
}

TEST_F(ACCControllerTest, OverrideToActiveAfter2Seconds) {
    activateACC();

    // Trigger override
    input.brake_force_n = 15.0f;
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_OVERRIDE);

    // Release brake
    input.brake_force_n = 0.0f;

    // Wait 1.9 seconds - still override
    updateN(95, 20);  // 95 * 20ms = 1900ms
    EXPECT_EQ(ctrl.state, ACC_STATE_OVERRIDE);

    // Wait 0.2 more seconds - should transition
    updateN(10, 20);  // 200ms more, total 2100ms
    EXPECT_EQ(ctrl.state, ACC_STATE_ACTIVE);
}

TEST_F(ACCControllerTest, OverrideToStandbyOnLowSpeed) {
    activateACC();

    // Trigger override
    input.brake_force_n = 15.0f;
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_OVERRIDE);

    // Speed drops below 25 km/h
    input.ego_speed_kph = 20.0f;
    input.brake_force_n = 0.0f;
    ACC_Update(&ctrl, &input, 20);

    EXPECT_EQ(ctrl.state, ACC_STATE_STANDBY);
}

TEST_F(ACCControllerTest, AnyStateToFaultOnSensorError) {
    activateACC();

    input.ego_speed_kph = -5.0f;  // Invalid speed
    ACC_Update(&ctrl, &input, 20);

    EXPECT_EQ(ctrl.state, ACC_STATE_FAULT);
}

TEST_F(ACCControllerTest, FaultToStandbyOnReset) {
    activateACC();

    // Trigger fault
    input.ego_speed_kph = -5.0f;
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_FAULT);

    // Restore valid input
    input.ego_speed_kph = 50.0f;

    // Wait for fault_ok_timer (1 second)
    for (int i = 0; i < 50; ++i) {  // 50 * 20ms = 1000ms
        ACC_Update(&ctrl, &input, 20);
    }

    // Ignition cycle flag
    input.ignition_cycle_flag = true;
    ACC_Update(&ctrl, &input, 20);

    EXPECT_EQ(ctrl.state, ACC_STATE_STANDBY);
}

/*============================================================================
 * FR-003: Set Speed Range
 *============================================================================*/

TEST_F(ACCControllerTest, SetSpeedInitializedToCurrentSpeedRounded) {
    input.ego_speed_kph = 52.0f;  // Should round to 50
    activateACC();

    EXPECT_FLOAT_EQ(ctrl.set_speed_kph, 50.0f);
}

TEST_F(ACCControllerTest, SetSpeedClampsAtMinimum) {
    activateACC();
    ctrl.set_speed_kph = 25.0f;  // Below minimum

    input.set_minus_request = true;
    ACC_Update(&ctrl, &input, 20);

    EXPECT_GE(ctrl.set_speed_kph, ACC_MIN_SET_SPEED_KPH);
}

TEST_F(ACCControllerTest, SetSpeedClampsAtMaximum) {
    activateACC();
    ctrl.set_speed_kph = 175.0f;

    input.set_plus_request = true;
    for (int i = 0; i < 50; ++i) {
        ACC_Update(&ctrl, &input, 20);
    }

    EXPECT_LE(ctrl.set_speed_kph, ACC_MAX_SET_SPEED_KPH);
}

/*============================================================================
 * FR-004: Set Speed Change
 *============================================================================*/

TEST_F(ACCControllerTest, SetPlusIncrementsBy1) {
    activateACC();
    float initial = ctrl.set_speed_kph;

    input.set_plus_request = true;
    ACC_Update(&ctrl, &input, 20);

    EXPECT_FLOAT_EQ(ctrl.set_speed_kph, initial + 1.0f);
}

TEST_F(ACCControllerTest, SetMinusDecrementsBy1) {
    activateACC();
    float initial = ctrl.set_speed_kph;

    input.set_minus_request = true;
    ACC_Update(&ctrl, &input, 20);

    EXPECT_FLOAT_EQ(ctrl.set_speed_kph, initial - 1.0f);
}

TEST_F(ACCControllerTest, SetPlusLongPressIncrementsFaster) {
    activateACC();
    float initial = ctrl.set_speed_kph;

    input.set_plus_request = true;

    // Hold for 1 second (after 500ms starts fast increment)
    for (int i = 0; i < 50; ++i) {  // 50 * 20ms = 1000ms
        ACC_Update(&ctrl, &input, 20);
    }

    // Should be initial + 1 (initial press) + ~2.5 (500ms * 5/s)
    EXPECT_GT(ctrl.set_speed_kph, initial + 2.0f);
}

TEST_F(ACCControllerTest, ResumeRestoresPreviousSetSpeed) {
    // First activation at 80 km/h
    input.ego_speed_kph = 80.0f;
    input.acc_on_request = true;
    ACC_Update(&ctrl, &input, 20);
    input.acc_on_request = false;
    EXPECT_EQ(ctrl.state, ACC_STATE_ACTIVE);
    EXPECT_FLOAT_EQ(ctrl.set_speed_kph, 80.0f);
    
    // Change set speed using SET+ to 85 km/h - this stores the value
    input.set_plus_request = true;
    for (int i = 0; i < 5; ++i) {
        ACC_Update(&ctrl, &input, 20);
    }
    input.set_plus_request = false;
    ACC_Update(&ctrl, &input, 20);
    float saved_speed = ctrl.set_speed_kph;
    EXPECT_GT(saved_speed, 80.0f);

    // Deactivate
    input.acc_off_request = true;
    ACC_Update(&ctrl, &input, 20);
    input.acc_off_request = false;
    EXPECT_EQ(ctrl.state, ACC_STATE_STANDBY);

    // Reactivate at lower speed 50 km/h (will set to current speed)
    input.ego_speed_kph = 50.0f;
    input.acc_on_request = true;
    ACC_Update(&ctrl, &input, 20);
    input.acc_on_request = false;
    EXPECT_EQ(ctrl.state, ACC_STATE_ACTIVE);
    EXPECT_FLOAT_EQ(ctrl.set_speed_kph, 50.0f);  // Reset to current speed

    // RESUME should restore the saved speed
    input.resume_request = true;
    ACC_Update(&ctrl, &input, 20);
    input.resume_request = false;

    EXPECT_FLOAT_EQ(ctrl.set_speed_kph, saved_speed);
}

/*============================================================================
 * FR-005: Speed Maintenance Control
 *============================================================================*/

TEST_F(ACCControllerTest, AcceleratesToSetSpeedWhenNoTarget) {
    activateACC();
    ctrl.set_speed_kph = 60.0f;
    input.ego_speed_kph = 50.0f;
    input.target_detected = false;

    ACC_Update(&ctrl, &input, 20);

    ACC_Output output = ACC_GetOutput(&ctrl);
    EXPECT_GT(output.accel_request_mps2, 0.0f);
    EXPECT_LE(output.accel_request_mps2, ACC_MAX_ACCEL_MPS2);
}

TEST_F(ACCControllerTest, SpeedControlDoesNotDecelerate) {
    activateACC();
    ctrl.set_speed_kph = 50.0f;
    input.ego_speed_kph = 60.0f;  // Above set speed
    input.target_detected = false;

    ACC_Update(&ctrl, &input, 20);

    // Without target, no gap control, so accel should be 0 (not negative)
    EXPECT_GE(ctrl.accel_speed_mps2, 0.0f);
}

/*============================================================================
 * FR-006: Gap Time Setting
 *============================================================================*/

TEST_F(ACCControllerTest, DefaultGapSettingIs1Point5Seconds) {
    EXPECT_EQ(ctrl.gap_setting, 1);  // Index 1 = 1.5 seconds
}

TEST_F(ACCControllerTest, GapButtonCyclesThroughSettings) {
    activateACC();

    EXPECT_EQ(ctrl.gap_setting, 1);

    input.gap_adjust_request = true;
    ACC_Update(&ctrl, &input, 20);
    input.gap_adjust_request = false;
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.gap_setting, 2);  // 2.0s

    input.gap_adjust_request = true;
    ACC_Update(&ctrl, &input, 20);
    input.gap_adjust_request = false;
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.gap_setting, 3);  // 2.5s

    input.gap_adjust_request = true;
    ACC_Update(&ctrl, &input, 20);
    input.gap_adjust_request = false;
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.gap_setting, 0);  // 1.0s (wraps)
}

/*============================================================================
 * FR-007: Lead Vehicle Following
 *============================================================================*/

TEST_F(ACCControllerTest, DeceleratesWhenTargetTooClose) {
    activateACC();
    ctrl.set_speed_kph = 80.0f;
    input.ego_speed_kph = 80.0f;
    input.target_detected = true;
    input.target_distance_m = 10.0f;  // Very close
    input.target_rel_speed_mps = -5.0f;  // Approaching

    ACC_Update(&ctrl, &input, 20);

    ACC_Output output = ACC_GetOutput(&ctrl);
    EXPECT_LT(output.accel_request_mps2, 0.0f);
}

TEST_F(ACCControllerTest, AcceleratesToSetSpeedWhenTargetFar) {
    activateACC();
    ctrl.set_speed_kph = 80.0f;
    input.ego_speed_kph = 60.0f;
    input.target_detected = true;
    input.target_distance_m = 150.0f;  // Far
    input.target_rel_speed_mps = 5.0f;   // Moving away

    ACC_Update(&ctrl, &input, 20);

    ACC_Output output = ACC_GetOutput(&ctrl);
    EXPECT_GT(output.accel_request_mps2, 0.0f);
}

TEST_F(ACCControllerTest, DecelerationLimitedToNormalBraking) {
    activateACC();
    input.target_detected = true;
    input.target_distance_m = 5.0f;
    input.target_rel_speed_mps = -10.0f;

    ACC_Update(&ctrl, &input, 20);

    // Without emergency condition, should be limited to normal decel
    if (!ctrl.emergency_brake_active) {
        EXPECT_GE(ctrl.accel_gap_mps2, ACC_MAX_DECEL_NORMAL_MPS2);
    }
}

/*============================================================================
 * FR-008: Gap Distance Warning
 *============================================================================*/

TEST_F(ACCControllerTest, VisualWarningWhenGapBelow50Percent) {
    activateACC();
    ctrl.gap_setting = 1;  // 1.5 seconds
    input.ego_speed_kph = 100.0f;  // ~27.8 m/s
    input.target_detected = true;
    // Target gap = 1.5 * 27.8 = 41.7m
    // 50% = 20.8m, put target at 18m
    input.target_distance_m = 18.0f;
    input.target_rel_speed_mps = 0.0f;

    ACC_Update(&ctrl, &input, 20);

    ACC_Output output = ACC_GetOutput(&ctrl);
    EXPECT_TRUE(output.warning_visual);
    EXPECT_FALSE(output.warning_audio);  // Above 30%
}

TEST_F(ACCControllerTest, AudioWarningWhenGapBelow30Percent) {
    activateACC();
    ctrl.gap_setting = 1;  // 1.5 seconds
    input.ego_speed_kph = 100.0f;
    input.target_detected = true;
    // 30% = 12.5m
    input.target_distance_m = 10.0f;
    input.target_rel_speed_mps = 0.0f;

    ACC_Update(&ctrl, &input, 20);

    ACC_Output output = ACC_GetOutput(&ctrl);
    EXPECT_TRUE(output.warning_visual);
    EXPECT_TRUE(output.warning_audio);
}

/*============================================================================
 * FR-009: Emergency Braking Activation
 *============================================================================*/

TEST_F(ACCControllerTest, EmergencyBrakeWhenTTCBelow1Point5Seconds) {
    activateACC();
    input.target_detected = true;
    input.target_distance_m = 10.0f;
    input.target_rel_speed_mps = -10.0f;  // TTC = 10/10 = 1.0s

    ACC_Update(&ctrl, &input, 20);

    ACC_Output output = ACC_GetOutput(&ctrl);
    EXPECT_TRUE(output.emergency_brake);
    EXPECT_LE(output.accel_request_mps2, ACC_MAX_DECEL_EMERG_MPS2 + 0.1f);
}

TEST_F(ACCControllerTest, NoEmergencyBrakeWhenTargetMovingAway) {
    activateACC();
    input.target_detected = true;
    input.target_distance_m = 10.0f;
    input.target_rel_speed_mps = 5.0f;  // Moving away

    ACC_Update(&ctrl, &input, 20);

    EXPECT_FALSE(ctrl.emergency_brake_active);
}

/*============================================================================
 * FR-010: Emergency Braking Release
 *============================================================================*/

TEST_F(ACCControllerTest, EmergencyBrakeReleasesWhenTTCAbove3Seconds) {
    activateACC();

    // Trigger emergency
    input.target_detected = true;
    input.target_distance_m = 10.0f;
    input.target_rel_speed_mps = -10.0f;  // TTC = 1.0s
    ACC_Update(&ctrl, &input, 20);
    EXPECT_TRUE(ctrl.emergency_brake_active);

    // Increase TTC above 3.0s
    input.target_distance_m = 40.0f;
    input.target_rel_speed_mps = -10.0f;  // TTC = 4.0s
    ACC_Update(&ctrl, &input, 20);

    EXPECT_FALSE(ctrl.emergency_brake_active);
}

TEST_F(ACCControllerTest, EmergencyBrakeReleasesWhenTargetLost) {
    activateACC();

    // Trigger emergency
    input.target_detected = true;
    input.target_distance_m = 10.0f;
    input.target_rel_speed_mps = -10.0f;
    ACC_Update(&ctrl, &input, 20);
    EXPECT_TRUE(ctrl.emergency_brake_active);

    // Lose target
    input.target_detected = false;
    ACC_Update(&ctrl, &input, 20);

    EXPECT_FALSE(ctrl.emergency_brake_active);
}

/*============================================================================
 * FR-011: Override Detection
 *============================================================================*/

TEST_F(ACCControllerTest, BrakeOverrideAt10NThreshold) {
    activateACC();

    input.brake_force_n = 10.0f;  // At threshold
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_ACTIVE);  // 10N is not > 10N

    input.brake_force_n = 10.1f;  // Just above
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_OVERRIDE);
}

/*============================================================================
 * FR-012: Override Return
 *============================================================================*/

TEST_F(ACCControllerTest, Override2SecondTimerResets) {
    activateACC();

    // Trigger override
    input.brake_force_n = 15.0f;
    ACC_Update(&ctrl, &input, 20);
    input.brake_force_n = 0.0f;

    // Wait 1.5 seconds
    updateN(75, 20);  // 1500ms
    EXPECT_EQ(ctrl.state, ACC_STATE_OVERRIDE);

    // Press brake again (resets timer)
    input.brake_force_n = 15.0f;
    ACC_Update(&ctrl, &input, 20);
    input.brake_force_n = 0.0f;

    // Wait another 1.5 seconds
    updateN(75, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_OVERRIDE);  // Still override (timer reset)

    // Wait 0.6 more seconds = total 2.1s from last reset
    updateN(30, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_ACTIVE);
}

/*============================================================================
 * FR-013: Sensor Fault Detection
 *============================================================================*/

TEST_F(ACCControllerTest, SpeedSensorRangeFault) {
    input.ego_speed_kph = 260.0f;  // Above 251
    ACC_Update(&ctrl, &input, 20);

    EXPECT_TRUE(ctrl.fault_code & ACC_FAULT_SPEED_RANGE);
}

TEST_F(ACCControllerTest, SpeedSensorTimeoutFault) {
    input.speed_sensor_valid = false;

    // Wait for timeout (500ms)
    for (int i = 0; i < 30; ++i) {  // 30 * 20ms = 600ms
        ACC_Update(&ctrl, &input, 20);
    }

    EXPECT_TRUE(ctrl.fault_code & ACC_FAULT_SPEED_TIMEOUT);
}

TEST_F(ACCControllerTest, RadarTimeoutFault) {
    input.radar_valid = false;

    // Wait for timeout (200ms)
    for (int i = 0; i < 15; ++i) {  // 15 * 20ms = 300ms
        ACC_Update(&ctrl, &input, 20);
    }

    EXPECT_TRUE(ctrl.fault_code & ACC_FAULT_RADAR_TIMEOUT);
}

/*============================================================================
 * FR-014: Fault Behavior
 *============================================================================*/

TEST_F(ACCControllerTest, FaultSetsAccelToZero) {
    activateACC();

    input.ego_speed_kph = -5.0f;  // Invalid
    ACC_Update(&ctrl, &input, 20);

    ACC_Output output = ACC_GetOutput(&ctrl);
    EXPECT_EQ(output.state, ACC_STATE_FAULT);
    EXPECT_FLOAT_EQ(output.accel_request_mps2, 0.0f);
}

TEST_F(ACCControllerTest, FaultCodeRecorded) {
    input.ego_speed_kph = 260.0f;
    ACC_Update(&ctrl, &input, 20);

    ACC_Output output = ACC_GetOutput(&ctrl);
    EXPECT_NE(output.fault_code, ACC_FAULT_NONE);
}

/*============================================================================
 * FR-015: Fault Reset
 *============================================================================*/

TEST_F(ACCControllerTest, FaultResetRequiresBothConditions) {
    activateACC();

    // Trigger fault
    input.ego_speed_kph = -5.0f;
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_FAULT);

    // Valid input for 1 second, but no ignition cycle
    input.ego_speed_kph = 50.0f;
    for (int i = 0; i < 60; ++i) {
        ACC_Update(&ctrl, &input, 20);
    }
    EXPECT_EQ(ctrl.state, ACC_STATE_FAULT);  // Still fault

    // Ignition cycle without valid duration
    input.ego_speed_kph = -5.0f;
    ACC_Update(&ctrl, &input, 20);
    input.ignition_cycle_flag = true;
    input.ego_speed_kph = 50.0f;
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_FAULT);  // Still fault

    // Both conditions
    for (int i = 0; i < 60; ++i) {
        ACC_Update(&ctrl, &input, 20);
    }
    EXPECT_EQ(ctrl.state, ACC_STATE_STANDBY);
}

/*============================================================================
 * Boundary Tests
 *============================================================================*/

TEST_F(ACCControllerTest, SpeedBoundary30KphActivation) {
    input.acc_on_request = true;
    input.ego_speed_kph = 29.9f;
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_STANDBY);

    input.ego_speed_kph = 30.0f;
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_ACTIVE);
}

TEST_F(ACCControllerTest, SpeedBoundary25KphOverride) {
    activateACC();

    input.brake_force_n = 15.0f;
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_OVERRIDE);

    input.brake_force_n = 0.0f;
    input.ego_speed_kph = 25.0f;
    updateN(100, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_ACTIVE);  // 25 >= 25

    input.brake_force_n = 15.0f;
    ACC_Update(&ctrl, &input, 20);
    input.brake_force_n = 0.0f;

    input.ego_speed_kph = 24.9f;
    ACC_Update(&ctrl, &input, 20);
    EXPECT_EQ(ctrl.state, ACC_STATE_STANDBY);  // Below 25
}

/*============================================================================
 * Null Pointer Safety
 *============================================================================*/

TEST_F(ACCControllerTest, NullControllerInit) {
    ACC_Init(nullptr);  // Should not crash
}

TEST_F(ACCControllerTest, NullControllerUpdate) {
    ACC_Update(nullptr, &input, 20);  // Should not crash
}

TEST_F(ACCControllerTest, NullInputUpdate) {
    ACC_Update(&ctrl, nullptr, 20);  // Should not crash
}

TEST_F(ACCControllerTest, NullControllerGetOutput) {
    ACC_Output output = ACC_GetOutput(nullptr);
    EXPECT_EQ(output.state, ACC_STATE_FAULT);
    EXPECT_TRUE(output.fault_code & ACC_FAULT_CONTROL_ERROR);
}

}  // namespace
