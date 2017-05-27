#include <time.h>
#include <inttypes.h>
#include <string.h>
#include "alarm.h"

time_t next_alarm(time_t *t, struct alarm *alarms)
{
    struct tm lt;
    struct alarm *a;
    struct tm alarm;

    localtime_r(t, &lt);

    a = alarms + lt.tm_wday;
    memcpy(&alarm, &lt, sizeof (struct tm));

    if (lt.tm_hour >= a->a_hour
            && lt.tm_min > a->a_min) {
        /* Alarm have passed, fetch next alarm */
        if (lt.tm_wday == 6)
            a = alarms;
        else
            a++;

        /* Step current alarm struct to next day */
        alarm.tm_hour = 0;
        alarm.tm_min = 0;
        alarm.tm_sec = 0;
        time_t t = mktime(&alarm);
        t += 24 * 60 * 60;
        localtime_r(&t, &alarm);
    }

    alarm.tm_hour = a->a_hour;
    alarm.tm_min = a->a_min;
    alarm.tm_sec = 0;

    return mktime(&alarm) - mktime(&lt);
}

// Get time to compensate per sleep cycle
time_t sleep_comp(time_t expected, time_t real)
{
    time_t diff = expected - real;
    diff = diff / 3;
    diff = diff * 2;
    diff = diff / SYNC_CYCLES; 

    return diff;
}
