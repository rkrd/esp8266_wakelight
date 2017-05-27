#include <stdio.h>
#include <time.h>
#include <string.h>
#include <inttypes.h>

#include "helpers.h"
#include "../alarm.h"

struct alarm alarms[7];

main() {
    time_t tnow = 1495917325;
    struct tm *t = localtime(&tnow);
    time_t res;

    puts("tnow:");
    print_tm(t);

    for (int i = 0; i < 7; i++) {
        alarms[i].a_hour = 6;
        alarms[i].a_min = 25;
    }

    res = next_alarm(&tnow, alarms);
    printf("Time till next alarm %d secs %f hours\n", res, (float)res/3600);

    return 0;
}
