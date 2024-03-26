#ifndef __TIME_H
#define __TIME_H

typedef long time_t;

struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};

time_t time(time_t *tloc);
struct tm *localtime(const time_t *timer);

char *ctime_r(const time_t *timep, char *buf);

#endif
