#pragma once
#include <sys/time.h>

// weather forecast
extern void TF_init();

// the forthcoming lowest temperature
extern int forecast_low_temp;
extern int historic_low_temp;
extern time_t temp_fetch_time;
