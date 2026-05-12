// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 Thomas Basler and others
 */
#include "EnergyHistory.h"

EnergyHistoryClass EnergyHistory;

EnergyHistoryClass::EnergyHistoryClass()
    : _loopTask(5 * 60 * TASK_SECOND, TASK_FOREVER, std::bind(&EnergyHistoryClass::loop, this))
{
}

void EnergyHistoryClass::init(Scheduler& scheduler)
{
    scheduler.addTask(_loopTask);
    _loopTask.enable();
}

void EnergyHistoryClass::loop()
{
    // Persistence is intentionally not wired yet. See docs/EnergyHistory.md
    // for the on-flash format and retention rules.
}
