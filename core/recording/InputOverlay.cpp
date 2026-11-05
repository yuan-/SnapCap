#include "core/recording/InputOverlay.h"

#include "core/action/KeySequence.h"
#include "core/action/KeySequenceCodec.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QVariant>
#include <algorithm>
#include <chrono>

namespace recording {

namespace {

constexpr int kRippleMs = 500;        // one click's ring, from press to gone
constexpr int kBadgeLingerMs = 600;   // the badge stays this long after the last action
constexpr int kWheelMs = 300;         // a wheel arrow's lifetime on the badge
constexpr int kFadeMs = 250;          // the tail over which the badge and caption fade
constexpr int kTypingMaxChars = 30;   // a longer typed run keeps only its newest end
constexpr int kBadgeW = 22;           // the mouse badge, video px
constexpr int kBadgeH = 34;
constexpr int kHaloAlpha = 100;       // the halo is drawn translucent over the cursor

// 1 → 0 over the last kFadeMs of a `lifeMs`-long display that started at `t`.
qreal fade(qint64 t, qint64 nowMs, int lifeMs)
{
    const qint64 left = t + lifeMs - nowMs;
    if (left <= 0) return 0;
    return left >= kFadeMs ? 1.0 : qreal(left) / kFadeMs;
}

QColor withAlpha(QColor c, qreal a)
{
    c.setAlphaF(qBound<qreal>(0, a, 1));
    return c;
}

Qt::KeyboardModifier modifierFor(int qtKey)
{
    switch (qtKey) {
        case Qt::Key_Control: return Qt::ControlModifier;
        case Qt::Key_Shift:   return Qt::ShiftModifier;
        case Qt::Key_Alt:     return Qt::AltModifier;
        case Qt::Key_Meta:    return Qt::MetaModifier;
    }
    return Qt::NoModifier;
}

bool printable(const QString& text)
{
    if (text.isEmpty()) return false;
    for (QChar c : text)
        if (!c.isPrint()) return false;
    return true;
}

QColor buttonColor(const InputStyle& st, Qt::MouseButton b)
{
    if (b == Qt::RightButton)  return st.rightColor;
    if (b == Qt::MiddleButton) return st.middleColor;
    return st.leftColor;
}

QColor readColor(const std::function<QVariant(const QString&, const QVariant&)>& get,
                 const QString& key, const QColor& def)
{
    const QColor c(get(key, def.name()).toString());
    return c.isValid() ? c : def;
}

} // namespace

QString keyFor(KeyMode m)
{
    return m == KeyMode::AllKeys ? QStringLiteral("all") : QStringLiteral("shortcuts");
}

QString keyFor(KeyPosition p)
{
    switch (p) {
        case KeyPosition::TopLeft:      return QStringLiteral("topLeft");
        case KeyPosition::TopCenter:    return QStringLiteral("topCenter");
        case KeyPosition::TopRight:     return QStringLiteral("topRight");
        case KeyPosition::BottomLeft:   return QStringLiteral("bottomLeft");
        case KeyPosition::BottomCenter: return QStringLiteral("bottomCenter");
        case KeyPosition::BottomRight:  return QStringLiteral("bottomRight");
    }
    return QStringLiteral("bottomCenter");
}

KeyMode keyModeFromKey(const QString& key, KeyMode fallback)
{
    for (KeyMode m : {KeyMode::Shortcuts, KeyMode::AllKeys})
        if (key.compare(keyFor(m), Qt::CaseInsensitive) == 0) return m;
    return fallback;
}

KeyPosition keyPositionFromKey(const QString& key, KeyPosition fallback)
{
    for (KeyPosition p : {KeyPosition::TopLeft, KeyPosition::TopCenter, KeyPosition::TopRight,
                          KeyPosition::BottomLeft, KeyPosition::BottomCenter,
                          KeyPosition::BottomRight})
        if (key.compare(keyFor(p), Qt::CaseInsensitive) == 0) return p;
    return fallback;
}

InputStyle readInputStyle(const std::function<QVariant(const QString&, const QVariant&)>& get)
{
    const InputStyle d;
    InputStyle s;
    s.halo         = get(QStringLiteral("mouse/halo"), d.halo).toBool();
    s.haloColor    = readColor(get, QStringLiteral("mouse/haloColor"), d.haloColor);
    s.haloRadius   = get(QStringLiteral("mouse/haloRadius"), d.haloRadius).toInt();
    s.ripple       = get(QStringLiteral("mouse/ripple"), d.ripple).toBool();
    s.leftColor    = readColor(get, QStringLiteral("mouse/leftColor"), d.leftColor);
    s.rightColor   = readColor(get, QStringLiteral("mouse/rightColor"), d.rightColor);
    s.middleColor  = readColor(get, QStringLiteral("mouse/middleColor"), d.middleColor);
    s.rippleRadius = get(QStringLiteral("mouse/rippleRadius"), d.rippleRadius).toInt();
    s.badge        = get(QStringLiteral("mouse/badge"), d.badge).toBool();
    s.keys         = get(QStringLiteral("keys/show"), d.keys).toBool();
    s.keyMode      = keyModeFromKey(get(QStringLiteral("keys/mode"), keyFor(d.keyMode)).toString());
    s.position     = keyPositionFromKey(
        get(QStringLiteral("keys/position"), keyFor(d.position)).toString());
    s.margin       = get(QStringLiteral("keys/margin"), d.margin).toInt();
    s.fontSize     = qMax(6, get(QStringLiteral("keys/fontSize"), d.fontSize).toInt());
    s.textColor    = readColor(get, QStringLiteral("keys/textColor"), d.textColor);
    s.bgColor      = readColor(get, QStringLiteral("keys/bgColor"), d.bgColor);
    s.bgOpacity    = qBound(0, get(QStringLiteral("keys/bgOpacity"), d.bgOpacity).toInt(), 100);
    s.durationMs   = qMax(kFadeMs, get(QStringLiteral("keys/durationMs"), d.durationMs).toInt());
    return s;
}

bool isSpecialKey(int qtKey)
{
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F35)
        return true;
    switch (qtKey) {
        case Qt::Key_Escape: case Qt::Key_Tab: case Qt::Key_Backtab:
        case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Backspace:
        case Qt::Key_Delete: case Qt::Key_Insert: case Qt::Key_Home: case Qt::Key_End:
        case Qt::Key_PageUp: case Qt::Key_PageDown:
        case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left: case Qt::Key_Right:
        case Qt::Key_Print: case Qt::Key_Pause:
            return true;
    }
    return false;
}

QRect captionRect(QSize size, const QRect& frame, KeyPosition pos, int margin)
{
    const bool top = pos == KeyPosition::TopLeft || pos == KeyPosition::TopCenter
                  || pos == KeyPosition::TopRight;
    int x;
    switch (pos) {
        case KeyPosition::TopLeft: case KeyPosition::BottomLeft:
            x = frame.left() + margin; break;
        case KeyPosition::TopRight: case KeyPosition::BottomRight:
            x = frame.left() + frame.width() - margin - size.width(); break;
        default:
            x = frame.left() + (frame.width() - size.width()) / 2; break;
    }
    int y = top ? frame.top() + margin
                : frame.top() + frame.height() - margin - size.height();
    x = qBound(frame.left(), x, qMax(frame.left(), frame.left() + frame.width() - size.width()));
    y = qBound(frame.top(), y, qMax(frame.top(), frame.top() + frame.height() - size.height()));
    return QRect(QPoint(x, y), size);
}

InputOverlay::InputOverlay(const InputStyle& style) : m_style(style) {}

void InputOverlay::setEnabled(bool on)
{
    QMutexLocker lock(&m_lock);
    m_s.enabled = on;
}

bool InputOverlay::enabled() const
{
    QMutexLocker lock(&m_lock);
    return m_s.enabled;
}

void InputOverlay::reset()
{
    QMutexLocker lock(&m_lock);
    const bool on = m_s.enabled;
    m_s = State();
    m_s.enabled = on;
}

void InputOverlay::mouseMoved(QPoint pos)
{
    QMutexLocker lock(&m_lock);
    m_s.cursor = pos;
    m_s.haveCursor = true;
}

void InputOverlay::mouseButton(Qt::MouseButton button, bool down, QPoint pos, qint64 nowMs)
{
    if (button != Qt::LeftButton && button != Qt::RightButton && button != Qt::MiddleButton)
        return;
    QMutexLocker lock(&m_lock);
    State& s = m_s;
    s.cursor = pos;
    s.haveCursor = true;
    s.buttons.setFlag(button, down);
    s.mouseActivity = nowMs;
    if (down && m_style.ripple) {
        s.ripples.erase(std::remove_if(s.ripples.begin(), s.ripples.end(),
                                       [nowMs](const Ripple& r) { return nowMs - r.t >= kRippleMs; }),
                        s.ripples.end());
        s.ripples.push_back({pos, button, nowMs});
    }
}

void InputOverlay::mouseWheel(bool up, bool horizontal, QPoint pos, qint64 nowMs)
{
    QMutexLocker lock(&m_lock);
    State& s = m_s;
    s.cursor = pos;
    s.haveCursor = true;
    s.wheelDir = (horizontal ? 2 : 1) * (up ? 1 : -1);
    s.wheelT = nowMs;
    s.mouseActivity = nowMs;
}

void InputOverlay::key(int qtKey, bool down, const QString& text, const QString& label,
                       qint64 nowMs)
{
    QMutexLocker lock(&m_lock);
    State& s = m_s;
    if (const Qt::KeyboardModifier mod = modifierFor(qtKey); mod != Qt::NoModifier) {
        s.mods.setFlag(mod, down);
        return;                     // a modifier alone shows nothing; it joins the combo
    }
    if (!down || !m_style.keys)
        return;

    const bool ctrl = s.mods.testFlag(Qt::ControlModifier);
    const bool alt  = s.mods.testFlag(Qt::AltModifier);
    const bool win  = s.mods.testFlag(Qt::MetaModifier);
    // Ctrl+Alt together is AltGr, which TYPES on many layouts (German AltGr+Q = '@'),
    // so a printable result under it is text, not a shortcut.
    const bool typing = printable(text) && !win && (ctrl == alt);
    if (typing) {
        if (m_style.keyMode != KeyMode::AllKeys)
            return;
        Caption& c = s.caption;
        if (c.typing && c.t >= 0 && nowMs - c.t < m_style.durationMs)
            c.text += text;
        else
            c = Caption{text, true, 1, nowMs};
        c.t = nowMs;
        return;
    }
    if (!ctrl && !alt && !win && !isSpecialKey(qtKey))
        return;                     // Shift + a non-typing key, CapsLock, media keys…

    QString name;
    if (qtKey)
        name = KeySequenceCodec::toString(KeySequence{Qt::NoModifier, qtKey, true});
    if (name.isEmpty())
        name = label.size() == 1 ? label.toUpper() : label;
    if (name.isEmpty())
        return;
    QStringList parts;
    if (ctrl)                                  parts << QStringLiteral("Ctrl");
    if (s.mods.testFlag(Qt::ShiftModifier))    parts << QStringLiteral("Shift");
    if (alt)                                   parts << QStringLiteral("Alt");
    if (win)                                   parts << QStringLiteral("Win");
    parts << name;
    showCombo(s, parts.join(QStringLiteral(" + ")), nowMs);
}

void InputOverlay::showCombo(State& s, const QString& text, qint64 nowMs)
{
    Caption& c = s.caption;
    if (!c.typing && c.text == text && c.t >= 0 && nowMs - c.t < m_style.durationMs)
        ++c.count;
    else
        c = Caption{text, false, 1, nowMs};
    c.t = nowMs;
}

QString InputOverlay::captionText(const State& s, qint64 nowMs) const
{
    const Caption& c = s.caption;
    if (c.t < 0 || nowMs - c.t >= m_style.durationMs)
        return QString();
    if (c.typing)
        return c.text.size() > kTypingMaxChars
            ? QStringLiteral("…") + c.text.right(kTypingMaxChars - 1)
            : c.text;
    return c.count > 1 ? QStringLiteral("%1 ×%2").arg(c.text).arg(c.count) : c.text;
}

QString InputOverlay::caption(qint64 nowMs) const
{
    QMutexLocker lock(&m_lock);
    return captionText(m_s, nowMs);
}

qint64 InputOverlay::clockMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void InputOverlay::paint(QPainter& p, const QRect& frame, qint64 nowMs) const
{
    State s;
    {
        QMutexLocker lock(&m_lock);
        if (!m_s.enabled)
            return;
        s = m_s;        // paint from a copy so the hooks never wait on the painter
    }
    const InputStyle& st = m_style;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(-frame.topLeft());      // capture px → frame px

    if (st.halo && s.haveCursor) {
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(st.haloColor, kHaloAlpha / 255.0));
        p.drawEllipse(QPointF(s.cursor), st.haloRadius, st.haloRadius);
    }

    for (const Ripple& r : s.ripples) {
        const qint64 age = nowMs - r.t;
        if (age < 0 || age >= kRippleMs)
            continue;
        const qreal t = qreal(age) / kRippleMs;
        const qreal radius = st.rippleRadius * (0.35 + 0.65 * t);
        const QColor c = buttonColor(st, r.button);
        p.setPen(QPen(withAlpha(c, 1 - t), qMax(2, st.rippleRadius / 7)));
        p.setBrush(withAlpha(c, 0.25 * (1 - t)));
        p.drawEllipse(QPointF(r.pos), radius, radius);
    }

    if (st.badge && s.haveCursor) {
        const qreal a = s.buttons ? 1.0 : (s.mouseActivity >= 0
                                               ? fade(s.mouseActivity, nowMs, kBadgeLingerMs)
                                               : 0.0);
        if (a > 0) {
            // Below-right of the hotspot, clear of the arrow cursor itself.
            const QRectF body(s.cursor.x() + 16, s.cursor.y() + 20, kBadgeW, kBadgeH);
            QPainterPath shape;
            shape.addRoundedRect(body, kBadgeW / 2.0, kBadgeW / 2.0);
            p.setPen(Qt::NoPen);
            p.setBrush(withAlpha(Qt::white, 0.92 * a));
            p.drawPath(shape);

            const qreal split = body.top() + body.height() * 0.45;
            p.save();
            p.setClipPath(shape);
            if (s.buttons.testFlag(Qt::LeftButton))
                p.fillRect(QRectF(body.left(), body.top(), body.width() / 2, split - body.top()),
                           withAlpha(st.leftColor, a));
            if (s.buttons.testFlag(Qt::RightButton))
                p.fillRect(QRectF(body.center().x(), body.top(), body.width() / 2,
                                  split - body.top()),
                           withAlpha(st.rightColor, a));
            p.restore();

            const QColor ink = withAlpha(QColor(0x33, 0x33, 0x33), a);
            p.setPen(QPen(ink, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawPath(shape);
            p.drawLine(QPointF(body.left(), split), QPointF(body.right(), split));
            p.drawLine(QPointF(body.center().x(), body.top()), QPointF(body.center().x(), split));

            const bool wheelOn = s.wheelT >= 0 && nowMs - s.wheelT < kWheelMs;
            const QRectF wheel(body.center().x() - 3, body.top() + 5, 6, 10);
            p.setBrush(s.buttons.testFlag(Qt::MiddleButton) || wheelOn
                           ? withAlpha(st.middleColor, a) : withAlpha(Qt::white, a));
            p.drawRoundedRect(wheel, 3, 3);

            if (wheelOn) {
                // A small arrow just outside the body, pointing the way the wheel turned.
                const QPointF c = body.center();
                const qreal hw = body.width() / 2 + 7, hh = body.height() / 2 + 7;
                QPolygonF tri;
                switch (s.wheelDir) {
                    case 1:  tri << QPointF(c.x(), c.y() - hh - 6) << QPointF(c.x() - 6, c.y() - hh)
                                 << QPointF(c.x() + 6, c.y() - hh); break;
                    case -1: tri << QPointF(c.x(), c.y() + hh + 6) << QPointF(c.x() - 6, c.y() + hh)
                                 << QPointF(c.x() + 6, c.y() + hh); break;
                    case 2:  tri << QPointF(c.x() + hw + 6, c.y()) << QPointF(c.x() + hw, c.y() - 6)
                                 << QPointF(c.x() + hw, c.y() + 6); break;
                    default: tri << QPointF(c.x() - hw - 6, c.y()) << QPointF(c.x() - hw, c.y() - 6)
                                 << QPointF(c.x() - hw, c.y() + 6); break;
                }
                p.setPen(Qt::NoPen);
                p.setBrush(withAlpha(st.middleColor, a));
                p.drawPolygon(tri);
            }
        }
    }
    p.restore();

    const QString text = captionText(s, nowMs);
    if (st.keys && !text.isEmpty()) {
        const qreal a = fade(s.caption.t, nowMs, st.durationMs);
        QFont f;
        f.setPixelSize(st.fontSize);
        f.setBold(true);
        const QFontMetrics fm(f);
        const int padX = st.fontSize * 6 / 10, padY = st.fontSize * 35 / 100;
        const QSize box(fm.horizontalAdvance(text) + 2 * padX, fm.height() + 2 * padY);
        const QRect r = captionRect(box, QRect(QPoint(0, 0), frame.size()), st.position,
                                    st.margin);
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(st.bgColor, st.bgOpacity / 100.0 * a));
        p.drawRoundedRect(r, st.fontSize * 0.3, st.fontSize * 0.3);
        p.setFont(f);
        p.setPen(withAlpha(st.textColor, a));
        p.drawText(r, Qt::AlignCenter, text);
        p.restore();
    }
}

} // namespace recording
