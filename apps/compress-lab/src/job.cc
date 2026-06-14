#include "job.h"

#include <cppcoro/recursive_generator.hpp>

namespace
{

cppcoro::recursive_generator<i64> pump_gen(Lab_Job* job)
{
    while (!job->finished && !job->failed)
    {
        lab_job_chunk(job);
        co_yield (i64)job->in_consumed;
    }
}

cppcoro::recursive_generator<i64> race_gen(Lab_Race* race)
{
    for (i32 i = 0; i < (i32)race->count; i++)
    {
        lab_race_arm(race, i);
        co_yield pump_gen(&race->job);
        lab_race_settle(race, i);
    }
}

}

struct Lab_Pump_Co
{
    cppcoro::recursive_generator<i64>           gen;
    cppcoro::recursive_generator<i64>::iterator it;
    bool                                        started;
};

struct Lab_Race_Co
{
    cppcoro::recursive_generator<i64>           gen;
    cppcoro::recursive_generator<i64>::iterator it;
    bool                                        started;
};

Lab_Pump_Co* lab_pump_begin(Lab_Job* job) { return new Lab_Pump_Co{ pump_gen(job), {}, false }; }

bool lab_pump_step(Lab_Pump_Co* co)
{
    if (!co->started)
    {
        co->it = co->gen.begin();
        co->started = true;
    }
    else
    {
        ++co->it;
    }
    return co->it != co->gen.end();
}

void lab_pump_end(Lab_Pump_Co* co) { delete co; }

Lab_Race_Co* lab_race_begin(Lab_Race* race) { return new Lab_Race_Co{ race_gen(race), {}, false }; }

bool lab_race_step(Lab_Race_Co* co)
{
    if (!co->started)
    {
        co->it = co->gen.begin();
        co->started = true;
    }
    else
    {
        ++co->it;
    }
    return co->it != co->gen.end();
}

void lab_race_end(Lab_Race_Co* co) { delete co; }
