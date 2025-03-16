// テストケース記述ファイル
#include "gtest/gtest.h"
#include "fff.h"
DEFINE_FFF_GLOBALS;

//テスト対象をinclude
extern "C" {
#include "../src/APP/SRC/LightScheduler.c"
}

//FFFによるFAKE定義
FAKE_VOID_FUNC(TimeService_SetDay, Day);
FAKE_VOID_FUNC(TimeService_SetMinute, MinuteOfDay);
FAKE_VOID_FUNC(TimeService_GetTime, Day*, MinuteOfDay*);

class LightScheduler : public ::testing::Test {
    protected:
        virtual void SetUp(){
        }
    };

TEST_F(LightScheduler, DISABLED_ScheduleOnEverydayNotTimeYet){
    //Arrange
    LightScheduler_ScheduleTurnON(LIGHT_ID_3, EVERYDAY, 1200);
    //月曜日、1199分にセット(TODO)

    //Act
    LightScheduler_Wakeup();

    //Assert
    //LightIdはUNKNOWN, LightStateはUNKNOWNであること

}