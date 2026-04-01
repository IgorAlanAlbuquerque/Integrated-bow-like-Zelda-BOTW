#pragma once
#include "PCH.h"

namespace BowInput {
    void DrainBowDeferredEvents();
    void UpdateBowInputState(RE::InputEvent** a_evns);
    void FilterBowEvents(RE::InputEvent** a_evns);
}