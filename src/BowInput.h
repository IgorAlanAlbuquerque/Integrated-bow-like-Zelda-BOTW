#pragma once

namespace BowInput {
    void RegisterInputHandler();

    void SetHoldMode(bool hold);
    void SetKeyScanCodes(int k1, int k2, int k3);
    void SetGamepadButtons(int b1, int b2, int b3);
}
