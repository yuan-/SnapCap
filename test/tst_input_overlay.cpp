#include <QtTest>

#include <QImage>
#include <QPainter>

#include "core/recording/InputOverlay.h"

using namespace recording;

// The mouse/keyboard display burned into recordings: which keys reach the caption,
// how captions merge and expire, where the caption box sits, and the settings keys.
// Text rendering needs a GUI application, so the caption is checked through
// caption() and only the (text-free) mouse layer is painted.
class TstInputOverlay : public QObject
{
    Q_OBJECT
private slots:
    void plainTypingHiddenInShortcutsMode();
    void shortcutsAreShown();
    void specialKeysShowWithoutModifiers();
    void shiftAloneIsNotAShortcut();
    void repeatedShortcutCounts();
    void allKeysJoinsTyping();
    void typingIsTrimmedToItsNewestEnd();
    void altGrTypesRatherThanShortcuts();
    void labelNamesUnknownKeys();
    void captionExpires();
    void resetDropsStuckModifiers();
    void keysOffShowsNothing();
    void captionRectPlacesAndClamps();
    void settingsKeysRoundTrip();
    void rippleIsPaintedAtTheClick();
    void disabledPaintsNothing();
};

void TstInputOverlay::plainTypingHiddenInShortcutsMode()
{
    InputOverlay o;
    o.key(Qt::Key_H, true, QStringLiteral("h"), QStringLiteral("H"), 0);
    o.key(Qt::Key_Space, true, QStringLiteral(" "), QString(), 10);
    QCOMPARE(o.caption(20), QString());
}

void TstInputOverlay::shortcutsAreShown()
{
    InputOverlay o;
    o.key(Qt::Key_Control, true, {}, {}, 0);
    o.key(Qt::Key_Shift, true, {}, {}, 0);
    // With Ctrl held the key "types" a control character, which is not text.
    o.key(Qt::Key_S, true, QString(QChar(0x13)), QStringLiteral("S"), 10);
    QCOMPARE(o.caption(20), QStringLiteral("Ctrl + Shift + S"));

    InputOverlay w;
    w.key(Qt::Key_Meta, true, {}, {}, 0);
    w.key(Qt::Key_D, true, QStringLiteral("d"), QStringLiteral("D"), 10);
    QCOMPARE(w.caption(20), QStringLiteral("Win + D"));
}

void TstInputOverlay::specialKeysShowWithoutModifiers()
{
    InputOverlay o;
    o.key(Qt::Key_Return, true, QStringLiteral("\r"), {}, 0);
    QCOMPARE(o.caption(10), QStringLiteral("Enter"));
    o.key(Qt::Key_F5, true, {}, {}, 20);
    QCOMPARE(o.caption(30), QStringLiteral("F5"));
    o.key(Qt::Key_Shift, true, {}, {}, 40);
    o.key(Qt::Key_Tab, true, QStringLiteral("\t"), {}, 50);
    QCOMPARE(o.caption(60), QStringLiteral("Shift + Tab"));
}

void TstInputOverlay::shiftAloneIsNotAShortcut()
{
    InputOverlay o;
    o.key(Qt::Key_Shift, true, {}, {}, 0);
    o.key(Qt::Key_A, true, QStringLiteral("A"), QStringLiteral("A"), 10);
    QCOMPARE(o.caption(20), QString());
    // Modifiers on their own never show.
    o.key(Qt::Key_Control, true, {}, {}, 30);
    QCOMPARE(o.caption(40), QString());
}

void TstInputOverlay::repeatedShortcutCounts()
{
    InputOverlay o;
    o.key(Qt::Key_Control, true, {}, {}, 0);
    for (int i = 0; i < 3; ++i)
        o.key(Qt::Key_Z, true, QString(QChar(0x1a)), QStringLiteral("Z"), 100 * i);
    QCOMPARE(o.caption(250), QStringLiteral("Ctrl + Z ×3"));
    // A different combo starts over.
    o.key(Qt::Key_Y, true, QString(QChar(0x19)), QStringLiteral("Y"), 300);
    QCOMPARE(o.caption(310), QStringLiteral("Ctrl + Y"));
}

void TstInputOverlay::allKeysJoinsTyping()
{
    InputStyle st;
    st.keyMode = KeyMode::AllKeys;
    InputOverlay o(st);
    const QString word = QStringLiteral("hi you");
    for (int i = 0; i < word.size(); ++i)
        o.key(0, true, word.mid(i, 1), {}, 100 * i);
    QCOMPARE(o.caption(600), QStringLiteral("hi you"));
    // A special key replaces the typed run, and typing afterwards starts a new one.
    o.key(Qt::Key_Return, true, QStringLiteral("\r"), {}, 700);
    QCOMPARE(o.caption(710), QStringLiteral("Enter"));
    o.key(Qt::Key_X, true, QStringLiteral("x"), QStringLiteral("X"), 800);
    QCOMPARE(o.caption(810), QStringLiteral("x"));
    // A pause longer than the display time also starts afresh.
    o.key(Qt::Key_Y, true, QStringLiteral("y"), QStringLiteral("Y"), 800 + st.durationMs + 1);
    QCOMPARE(o.caption(800 + st.durationMs + 2), QStringLiteral("y"));
}

void TstInputOverlay::typingIsTrimmedToItsNewestEnd()
{
    InputStyle st;
    st.keyMode = KeyMode::AllKeys;
    InputOverlay o(st);
    for (int i = 0; i < 40; ++i)
        o.key(0, true, QString(QChar('a' + i % 26)), {}, i);
    const QString c = o.caption(50);
    QCOMPARE(c.size(), 30);
    QVERIFY(c.startsWith(QStringLiteral("…")));
    QVERIFY(c.endsWith(QStringLiteral("n")));      // the 40th letter, i = 39 → 'n'
}

void TstInputOverlay::altGrTypesRatherThanShortcuts()
{
    InputOverlay o;              // Shortcuts mode: typed text must stay hidden
    o.key(Qt::Key_Control, true, {}, {}, 0);
    o.key(Qt::Key_Alt, true, {}, {}, 0);
    o.key(Qt::Key_Q, true, QStringLiteral("@"), QStringLiteral("Q"), 10);
    QCOMPARE(o.caption(20), QString());
    // Ctrl+Alt with a key that types nothing is a real shortcut.
    o.key(Qt::Key_Delete, true, {}, {}, 30);
    QCOMPARE(o.caption(40), QStringLiteral("Ctrl + Alt + Delete"));
}

void TstInputOverlay::labelNamesUnknownKeys()
{
    InputOverlay o;
    o.key(Qt::Key_Control, true, {}, {}, 0);
    o.key(0, true, {}, QStringLiteral("="), 10);       // OEM key: no Qt::Key
    QCOMPARE(o.caption(20), QStringLiteral("Ctrl + ="));
    o.key(0, true, {}, QStringLiteral("Num 5"), 30);
    QCOMPARE(o.caption(40), QStringLiteral("Ctrl + Num 5"));
    o.key(0, true, {}, QString(), 50);                 // nothing to call it: ignored
    QCOMPARE(o.caption(60), QStringLiteral("Ctrl + Num 5"));
}

void TstInputOverlay::captionExpires()
{
    InputStyle st;
    st.durationMs = 1000;
    InputOverlay o(st);
    o.key(Qt::Key_Escape, true, {}, {}, 0);
    QCOMPARE(o.caption(999), QStringLiteral("Esc"));
    QCOMPARE(o.caption(1000), QString());
}

void TstInputOverlay::resetDropsStuckModifiers()
{
    InputOverlay o;
    o.key(Qt::Key_Control, true, {}, {}, 0);     // its release is never seen
    o.reset();
    o.key(Qt::Key_C, true, QStringLiteral("c"), QStringLiteral("C"), 10);
    QCOMPARE(o.caption(20), QString());           // plain typing, not "Ctrl + C"
}

void TstInputOverlay::keysOffShowsNothing()
{
    InputStyle st;
    st.keys = false;
    InputOverlay o(st);
    o.key(Qt::Key_F1, true, {}, {}, 0);
    QCOMPARE(o.caption(10), QString());
}

void TstInputOverlay::captionRectPlacesAndClamps()
{
    const QRect frame(0, 0, 400, 300);
    const QSize box(100, 40);
    QCOMPARE(captionRect(box, frame, KeyPosition::BottomCenter, 20), QRect(150, 240, 100, 40));
    QCOMPARE(captionRect(box, frame, KeyPosition::TopLeft, 20),      QRect(20, 20, 100, 40));
    QCOMPARE(captionRect(box, frame, KeyPosition::TopRight, 20),     QRect(280, 20, 100, 40));
    QCOMPARE(captionRect(box, frame, KeyPosition::BottomLeft, 0),    QRect(0, 260, 100, 40));
    // A margin larger than the room pulls the box back inside the frame.
    QCOMPARE(captionRect(box, frame, KeyPosition::BottomRight, 500), QRect(0, 0, 100, 40));
    // A box wider than the frame sits at the frame's left/top edge.
    QCOMPARE(captionRect(QSize(500, 40), frame, KeyPosition::TopCenter, 10).topLeft(),
             QPoint(0, 10));
}

void TstInputOverlay::settingsKeysRoundTrip()
{
    for (KeyMode m : {KeyMode::Shortcuts, KeyMode::AllKeys})
        QCOMPARE(keyModeFromKey(keyFor(m)), m);
    for (KeyPosition p : {KeyPosition::TopLeft, KeyPosition::TopCenter, KeyPosition::TopRight,
                          KeyPosition::BottomLeft, KeyPosition::BottomCenter,
                          KeyPosition::BottomRight})
        QCOMPARE(keyPositionFromKey(keyFor(p)), p);
    QCOMPARE(keyPositionFromKey(QStringLiteral("nonsense")), KeyPosition::BottomCenter);

    // An empty store yields the struct defaults; stored values are read back.
    const InputStyle d;
    auto empty = [](const QString&, const QVariant& def) { return def; };
    const InputStyle e = readInputStyle(empty);
    QCOMPARE(e.ripple, d.ripple);
    QCOMPARE(e.leftColor, d.leftColor);
    QCOMPARE(e.position, d.position);

    QMap<QString, QVariant> store{
        {QStringLiteral("mouse/halo"), true},
        {QStringLiteral("mouse/haloColor"), QStringLiteral("#123456")},
        {QStringLiteral("keys/mode"), QStringLiteral("all")},
        {QStringLiteral("keys/position"), QStringLiteral("topRight")},
        {QStringLiteral("keys/bgOpacity"), 250},          // clamped to 100
        {QStringLiteral("keys/textColor"), QStringLiteral("not a colour")},
    };
    const InputStyle s = readInputStyle([&](const QString& k, const QVariant& def) {
        return store.value(k, def);
    });
    QVERIFY(s.halo);
    QCOMPARE(s.haloColor, QColor(0x12, 0x34, 0x56));
    QCOMPARE(s.keyMode, KeyMode::AllKeys);
    QCOMPARE(s.position, KeyPosition::TopRight);
    QCOMPARE(s.bgOpacity, 100);
    QCOMPARE(s.textColor, d.textColor);                    // invalid → default
}

void TstInputOverlay::rippleIsPaintedAtTheClick()
{
    InputStyle st;
    st.keys = false;
    InputOverlay o(st);
    o.setEnabled(true);
    // Region at (1000, 500) in capture px; the click lands at frame (60, 40).
    const QRect frame(1000, 500, 120, 80);
    o.mouseButton(Qt::LeftButton, true, QPoint(1060, 540), 0);

    QImage img(frame.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    {
        QPainter p(&img);
        o.paint(p, frame, 0);
    }
    QVERIFY(img.pixelColor(60, 40) != QColor(Qt::black));   // the ring's translucent fill
    QCOMPARE(img.pixelColor(2, 2), QColor(Qt::black));      // nothing far away

    // Once the ripple's time is up it is gone.
    img.fill(Qt::black);
    {
        QPainter p(&img);
        o.paint(p, frame, 10000);
    }
    QCOMPARE(img.pixelColor(60, 40), QColor(Qt::black));
}

void TstInputOverlay::disabledPaintsNothing()
{
    InputStyle st;
    st.keys = false;
    InputOverlay o(st);                         // starts disabled
    o.mouseButton(Qt::LeftButton, true, QPoint(10, 10), 0);
    QImage img(40, 40, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    {
        QPainter p(&img);
        o.paint(p, QRect(0, 0, 40, 40), 0);
    }
    QCOMPARE(img.pixelColor(10, 10), QColor(Qt::black));
}

QTEST_GUILESS_MAIN(TstInputOverlay)
#include "tst_input_overlay.moc"
