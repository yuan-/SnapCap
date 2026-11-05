#pragma once

#include "platform/platform.h"

// Windows IInputWatch backend: a WH_MOUSE_LL and a WH_KEYBOARD_LL hook that report
// what they see and pass every event straight on (never return non-zero) — the app
// being recorded must keep receiving all of its input.
//
// Unlike WinWheelWatch, events are delivered SYNCHRONOUSLY from the hook proc: the
// one consumer (recording::InputOverlay) only stores them under a short lock, and
// queuing a call per WM_MOUSEMOVE would flood the GUI thread's queue for nothing.
// Low-level hooks are process-global and only one recording runs at a time, so a
// single set of file-static state backs the C-style hook procs.
// (windows.h is kept out of this header.)
class WinInputWatch : public platform::IInputWatch {
public:
    WinInputWatch() = default;
    ~WinInputWatch() override;

    void arm(Callbacks cb) override;
    void disarm() override;
};
