//include
#include <stdint.h>

//type
typedef enum{
    SUNDAY,
    MONDAY,
    TUESDAY,
    WEDNESDAY,
    THURSDAY,
    FRIDAY,
    SATURDAY,
    EVERYDAY,
    WEEKDAY,
    WEEKEND,
    DAY_NEVER
}Day;

//一日の中での経過分数を2byte型で表現
typedef uint16_t MinuteOfDay;

//extern
extern void TimeService_SetDay(Day);
extern void TimeService_SetMinute(MinuteOfDay);
extern void TimeService_GetTime(Day*, MinuteOfDay*);
