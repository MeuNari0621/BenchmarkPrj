//SPYモジュール用のインターフェース実装

#include "LightController.h"

//プロダクトコード汚染を防ぐため、ここにリテラル値を記述
enum {
    LIGHT_ID_UNKNOWN = -1,
    LIGHT_STATE_UNKNOWN = -1,
    LIGHT_OFF = 0,
    LIGHT_ON = 1
};

int LightControllerSpy_GetLastId(void);
int LightControllerSpy_GetLastState(void);
void LightControllerSpy_Create(void);
void LightController_On(int id);
void LightController_Off(int id);
void LightControllerSpy_Destroy(void);

