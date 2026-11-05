#pragma once

#include <QColor>
#include <QMutex>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>
#include <functional>

class QPainter;
class QVariant;

// Mouse and keyboard actions drawn INTO a recording: a halo that follows the cursor,
// a ripple per click, a small mouse badge showing the pressed button / wheel, and a
// caption box with the keys pressed ("Ctrl + Shift + S").
//
// The overlay windows are excluded from capture, so none of this can be drawn on
// screen and recorded: the recorder calls paint() on every frame, from its encode
// thread, while the global input hooks feed events in on the GUI thread — hence the
// mutex. Pure Qt-Gui painting with no widgets or Win32, so the rules (which keys
// count as a shortcut, how captions merge and expire, where the box sits) are
// headless unit-testable (tst_input_overlay).
//
// Everything is in capture (physical) px: event positions are virtual-desktop px and
// the style's sizes are pixels of the VIDEO, not of the screen at its DPI.
namespace recording {

// Which keys reach the caption.
//   Shortcuts — a key pressed with Ctrl / Alt / Win, or a special key (F-keys, Enter,
//               Esc, Tab, Backspace, arrows…). Plain typing never shows, so a typed
//               password cannot end up in the video. The default.
//   AllKeys   — that, plus typed text, joined into one running caption ("hello").
enum class KeyMode { Shortcuts, AllKeys };
// Where the key caption sits inside the recorded region.
enum class KeyPosition { TopLeft, TopCenter, TopRight, BottomLeft, BottomCenter, BottomRight };

QString keyFor(KeyMode m);
QString keyFor(KeyPosition p);
KeyMode keyModeFromKey(const QString& key, KeyMode fallback = KeyMode::Shortcuts);
KeyPosition keyPositionFromKey(const QString& key,
                               KeyPosition fallback = KeyPosition::BottomCenter);

// Preferences → Recording → Mouse and keyboard. Defaults are the shipping defaults;
// each field is the `recording/input/<name>` key noted beside it.
struct InputStyle {
    bool   halo = false;                       // mouse/halo
    QColor haloColor{0xff, 0xd4, 0x00};        // mouse/haloColor (drawn translucent)
    int    haloRadius = 28;                    // mouse/haloRadius
    bool   ripple = true;                      // mouse/ripple
    QColor leftColor{0x2d, 0x7d, 0xff};        // mouse/leftColor
    QColor rightColor{0xe5, 0x48, 0x4d};       // mouse/rightColor
    QColor middleColor{0x30, 0xa4, 0x6c};      // mouse/middleColor
    int    rippleRadius = 32;                  // mouse/rippleRadius
    bool   badge = false;                      // mouse/badge

    bool        keys = true;                   // keys/show
    KeyMode     keyMode = KeyMode::Shortcuts;  // keys/mode
    KeyPosition position = KeyPosition::BottomCenter;   // keys/position
    int    margin = 40;                        // keys/margin
    int    fontSize = 28;                      // keys/fontSize
    QColor textColor{Qt::white};               // keys/textColor
    QColor bgColor{0x20, 0x20, 0x20};          // keys/bgColor
    int    bgOpacity = 75;                     // keys/bgOpacity, percent
    int    durationMs = 1500;                  // keys/durationMs
};

// Reads the style through `get(key, default)`, where key is relative to
// `recording/input/` (e.g. "mouse/halo"). A function rather than a Settings& so the
// key names are tested without touching a config file.
InputStyle readInputStyle(const std::function<QVariant(const QString&, const QVariant&)>& get);

// True for keys that show even without Ctrl/Alt/Win: F-keys, Enter, Esc, Tab,
// Backspace, Delete, Insert, Home/End, PageUp/Down, arrows, Print, Pause. Not Space —
// that is typing.
bool isSpecialKey(int qtKey);
// Where a caption of `size` sits in `frame` for `pos`, `margin` px in from the edges.
// Clamped so it never leaves the frame, even when the frame is smaller than the box.
QRect captionRect(QSize size, const QRect& frame, KeyPosition pos, int margin);

class InputOverlay {
public:
    explicit InputOverlay(const InputStyle& style = {});

    // Off draws nothing. The recording bar's toggle flips this mid-recording.
    void setEnabled(bool on);
    bool enabled() const;
    // Forget held keys, held buttons and pending effects. Called whenever the hooks
    // (re)arm: while they were off, releases went unseen and would leave a modifier
    // stuck down.
    void reset();

    // --- events (GUI thread). Positions in capture px; `nowMs` from clockMs().
    void mouseMoved(QPoint pos);
    void mouseButton(Qt::MouseButton button, bool down, QPoint pos, qint64 nowMs);
    // `up` is the wheel's direction: up / right when true.
    void mouseWheel(bool up, bool horizontal, QPoint pos, qint64 nowMs);
    // `qtKey` is a Qt::Key (0 if the platform had none for it); `text` is what the key
    // typed under the current layout (empty for none), `label` its key-cap name for
    // keys Qt::Key cannot name (OEM punctuation, numpad…). Auto-repeat must already be
    // filtered out: every `down` is taken as a fresh press.
    void key(int qtKey, bool down, const QString& text, const QString& label, qint64 nowMs);

    // --- rendering (encode thread). `frame` is the video frame's rect in capture px —
    // its top-left is the recorded region's — and `p` paints in frame pixels.
    void paint(QPainter& p, const QRect& frame, qint64 nowMs) const;

    // The caption the key box would show at `nowMs`, empty when none. For tests.
    QString caption(qint64 nowMs) const;

    // The monotonic clock shared by the hooks and the encode loop.
    static qint64 clockMs();

private:
    struct Ripple { QPoint pos; Qt::MouseButton button; qint64 t; };
    struct Caption {
        QString text;
        bool    typing = false;     // typed text: later typed keys append to it
        int     count = 1;          // the same shortcut pressed again: "Ctrl + Z ×3"
        qint64  t = -1;             // last update; -1 = none
    };
    struct State {
        bool   enabled = false;
        bool   haveCursor = false;
        QPoint cursor;
        Qt::MouseButtons buttons;
        qint64 mouseActivity = -1;  // last press / release / wheel — the badge's fade
        int    wheelDir = 0;        // 0 none; ±1 vertical (up +), ±2 horizontal (right +)
        qint64 wheelT = -1;
        QVector<Ripple> ripples;
        Qt::KeyboardModifiers mods;
        Caption caption;
    };

    QString captionText(const State& s, qint64 nowMs) const;
    void showCombo(State& s, const QString& text, qint64 nowMs);

    const InputStyle m_style;
    mutable QMutex m_lock;
    State m_s;
};

} // namespace recording
