#pragma once

#include "fighter.h"

bool boxes_overlap(Fighter_Box a, Fighter_Box b);

void combat_check_hits(Fighter* attacker, Fighter* defender);

void combat_check_projectiles(Fighter* shooter, Fighter* target);
