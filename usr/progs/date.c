// date.c prints current date/time from RTC
#include "../syscall.h"
#include "../string.h"
#include "../shell.h"
#include "../error.h"
#include "../io.h"

// helper for leap year check, gotta handle the 400 year edge case
#define LEAP(y) (((y)%4==0 && (y)%100!=0) || (y)%400==0)
#define NSEC_PER_SEC 1000000000ULL

void main(int argc, char** argv) {
    // open rtc device --x-- it returns nanoseconds as a uint64
    int fd = _open(-1, "/dev/rtc");
    if (fd < 0) {
        dprintf(STDOUT, "date: cant open /dev/rtc (%d)\n", fd);
        return;
    }

    unsigned long long ns = 0;
    long r = _read(fd, &ns, sizeof(ns));
    _close(fd);
    if (r != (long)sizeof(ns)) {
        dprintf(STDOUT, "date: rtc read failed\n");
        return;
    }

    // ns to seconds, then break out time of day first since its easy
    unsigned long long sec = ns / NSEC_PER_SEC;
    int hour = (sec % 86400) / 3600;
    int min  = (sec % 3600) / 60;
    int s    = sec % 60;

    // now figure out the date by walking forward year by year from epoch
    unsigned long long days = sec / 86400;
    int year = 1970;
    while (1) {
        int diy = LEAP(year) ? 366 : 365;
        if (days < (unsigned long long)diy) break;
        days -= diy;
        year++;
    }

    // walk months for whats left
    static const int dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    static const char * mon[12] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    int month = 0;
    for (int i = 0; i < 12; i++) {
        int d = (i == 1 && LEAP(year)) ? 29 : dim[i];
        if (days < (unsigned long long)d) { month = i; break; }
        days -= d;
    }
    int day = (int)days + 1;

    // spec format is "DD month YYYY HH:MM:SS" --x-- no UTC suffix, day zero padded
    dprintf(STDOUT, "%02d %s %04d %02d:%02d:%02d\n",
            day, mon[month], year, hour, min, s);
}