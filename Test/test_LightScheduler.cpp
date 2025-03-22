// テストケース記述ファイル
#include "gtest/gtest.h"
#include "fff.h"
DEFINE_FFF_GLOBALS;

//テスト対象をinclude
extern "C" {
#include "LightScheduler.h"
#include "LightControllerSpy.h"
#include "TimeService.h"
}

//FFFによるFAKE定義
FAKE_VOID_FUNC(TimeService_SetDay, Day);
FAKE_VOID_FUNC(TimeService_SetMinute, MinuteOfDay);
FAKE_VOID_FUNC(TimeService_GetTime, Day*, MinuteOfDay*);

class LightScheduler : public ::testing::Test {
    protected:
    virtual void SetUp(){
        LightControllerSpy_Create();
    }
    virtual void TearDown(){
        LightControllerSpy_Destroy();
    }
    };

#if 0
TEST_F(LightScheduler, ScheduleOnEverydayNotTimeYet){
    //Arrange
    LightScheduler_ScheduleTurnON(LIGHT_ID_3, EVERYDAY, 1200);
    //月曜日、1199分にセット(TODO)

    //Act
    LightScheduler_Wakeup();

    //Assert
    //LightIdはUNKNOWN, LightStateはUNKNOWNであること

}
#endif

TEST_F(LightScheduler, NoChangeToLightsDuringInitialization){
    //Arrange

    //Act

    //Assert
    //LightIdはUNKNOWN, LightStateはUNKNOWNであること
    EXPECT_EQ(LIGHT_ID_UNKNOWN, LightControllerSpy_GetLastId());
    EXPECT_EQ(LIGHT_STATE_UNKNOWN, LightControllerSpy_GetLastState());
}

