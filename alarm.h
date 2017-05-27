struct alarm {
    uint8_t a_hour;
    uint8_t a_min;
};

time_t next_alarm(time_t *, struct alarm *);
