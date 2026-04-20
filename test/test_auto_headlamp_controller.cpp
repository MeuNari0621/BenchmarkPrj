#include <gtest/gtest.h>

extern "C" {
#include "auto_headlamp_controller.h"
}

namespace {

AHC_Input BaseInput() {
    AHC_Input input{};
    input.ambient_lux = 1000;
    input.vehicle_speed_kph = 50;
    input.tunnel_detected = false;
    input.manual_override_request = false;
    input.requested_override_mode = AHC_MODE_AUTO;
    return input;
}

void UpdateNTimes(AHC_Controller* controller, const AHC_Input& input, int times, uint32_t elapsed_ms = 100) {
    for (int i = 0; i < times; ++i) {
        AHC_Update(controller, &input, elapsed_ms);
    }
}

}  // namespace

TEST(AutoHeadlampController, StartupKeepsLampOff) {
    AHC_Controller controller;
    AHC_Init(&controller);

    auto output = AHC_GetOutput(&controller);
    EXPECT_FALSE(output.lamp_on);
    EXPECT_EQ(output.active_mode, AHC_MODE_AUTO);
}

TEST(AutoHeadlampController, TurnsOnAfterThreeConsecutiveDarkSamples) {
    AHC_Controller controller;
    AHC_Init(&controller);

    auto input = BaseInput();
    input.ambient_lux = 250;

    UpdateNTimes(&controller, input, 2);
    EXPECT_FALSE(AHC_GetOutput(&controller).lamp_on);

    AHC_Update(&controller, &input, 100);
    EXPECT_TRUE(AHC_GetOutput(&controller).lamp_on);
}

TEST(AutoHeadlampController, TurnsOffAfterThreeConsecutiveBrightSamples) {
    AHC_Controller controller;
    AHC_Init(&controller);

    auto dark = BaseInput();
    dark.ambient_lux = 200;
    UpdateNTimes(&controller, dark, 3);
    EXPECT_TRUE(AHC_GetOutput(&controller).lamp_on);

    auto bright = BaseInput();
    bright.ambient_lux = 700;
    UpdateNTimes(&controller, bright, 2);
    EXPECT_TRUE(AHC_GetOutput(&controller).lamp_on);

    AHC_Update(&controller, &bright, 100);
    EXPECT_FALSE(AHC_GetOutput(&controller).lamp_on);
}

TEST(AutoHeadlampController, DoesNotTurnOnWhenVehicleSpeedIsLow) {
    AHC_Controller controller;
    AHC_Init(&controller);

    auto input = BaseInput();
    input.vehicle_speed_kph = 5;
    input.ambient_lux = 100;

    UpdateNTimes(&controller, input, 5);
    EXPECT_FALSE(AHC_GetOutput(&controller).lamp_on);
}

TEST(AutoHeadlampController, TunnelDetectionForcesImmediateOnInAutoMode) {
    AHC_Controller controller;
    AHC_Init(&controller);

    auto input = BaseInput();
    input.tunnel_detected = true;
    input.ambient_lux = 1000;

    AHC_Update(&controller, &input, 100);
    EXPECT_TRUE(AHC_GetOutput(&controller).lamp_on);
}

TEST(AutoHeadlampController, ManualForceOnOverridesAutoDecision) {
    AHC_Controller controller;
    AHC_Init(&controller);

    auto request = BaseInput();
    request.manual_override_request = true;
    request.requested_override_mode = AHC_MODE_FORCE_ON;
    request.ambient_lux = 1000;

    AHC_Update(&controller, &request, 100);
    auto output = AHC_GetOutput(&controller);
    EXPECT_TRUE(output.lamp_on);
    EXPECT_EQ(output.active_mode, AHC_MODE_FORCE_ON);
}

TEST(AutoHeadlampController, ManualOverrideExpiresAfter120Seconds) {
    AHC_Controller controller;
    AHC_Init(&controller);

    auto request = BaseInput();
    request.manual_override_request = true;
    request.requested_override_mode = AHC_MODE_FORCE_OFF;
    request.ambient_lux = 100;

    AHC_Update(&controller, &request, 100);
    EXPECT_EQ(AHC_GetOutput(&controller).active_mode, AHC_MODE_FORCE_OFF);

    auto input = BaseInput();
    input.ambient_lux = 100;
    AHC_Update(&controller, &input, 119900);
    EXPECT_EQ(AHC_GetOutput(&controller).active_mode, AHC_MODE_FORCE_OFF);

    AHC_Update(&controller, &input, 100);
    EXPECT_EQ(AHC_GetOutput(&controller).active_mode, AHC_MODE_AUTO);

    UpdateNTimes(&controller, input, 3);
    EXPECT_TRUE(AHC_GetOutput(&controller).lamp_on);
}

TEST(AutoHeadlampController, InvalidSensorInputSetsFaultAndKeepsLastOutput) {
    AHC_Controller controller;
    AHC_Init(&controller);

    auto dark = BaseInput();
    dark.ambient_lux = 100;
    UpdateNTimes(&controller, dark, 3);
    EXPECT_TRUE(AHC_GetOutput(&controller).lamp_on);

    auto invalid = BaseInput();
    invalid.ambient_lux = -1;
    AHC_Update(&controller, &invalid, 100);

    auto output = AHC_GetOutput(&controller);
    EXPECT_TRUE(output.fault_sensor_range);
    EXPECT_TRUE(output.lamp_on);
}

TEST(AutoHeadlampController, HysteresisBandDoesNotToggleLamp) {
    AHC_Controller controller;
    AHC_Init(&controller);

    auto dark = BaseInput();
    dark.ambient_lux = 100;
    UpdateNTimes(&controller, dark, 3);
    EXPECT_TRUE(AHC_GetOutput(&controller).lamp_on);

    auto mid = BaseInput();
    mid.ambient_lux = 400;
    UpdateNTimes(&controller, mid, 10);

    EXPECT_TRUE(AHC_GetOutput(&controller).lamp_on);
}
