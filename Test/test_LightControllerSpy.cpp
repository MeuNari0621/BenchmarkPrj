// テストケース記述ファイル
#include "gtest/gtest.h"

//テスト対象をinclude
extern "C" {
#include "LightControllerSpy.h"
}


class LightControllerSpy : public ::testing::Test {
    protected:
        virtual void SetUp(){
            LightControllerSpy_Create();
        }
        virtual void TearDown(){
            LightControllerSpy_Destroy();
        }
    };

TEST_F(LightControllerSpy, Create){
    //Arrange

    //Act

    //Assert
    EXPECT_EQ(LIGHT_ID_UNKNOWN, LightControllerSpy_GetLastId());
    EXPECT_EQ(LIGHT_STATE_UNKNOWN, LightControllerSpy_GetLastState());
}

TEST_F(LightControllerSpy, RememberTheLastLightIdControlled){
    //Arrange

    //Act
    LightController_On(10);

    //Assert
    EXPECT_EQ(10, LightControllerSpy_GetLastId());
    EXPECT_EQ(LIGHT_ON, LightControllerSpy_GetLastState());
}
