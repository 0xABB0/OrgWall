#pragma once

#include "lab.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Lab_Pump_Co Lab_Pump_Co;
typedef struct Lab_Race_Co Lab_Race_Co;

Lab_Pump_Co* lab_pump_begin(Lab_Job* job);
bool         lab_pump_step(Lab_Pump_Co* co);
void         lab_pump_end(Lab_Pump_Co* co);

Lab_Race_Co* lab_race_begin(Lab_Race* race);
bool         lab_race_step(Lab_Race_Co* co);
void         lab_race_end(Lab_Race_Co* co);

#ifdef __cplusplus
}
#endif
