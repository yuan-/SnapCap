#include "platform/win/WinInputWatch.h"

#include <QPoint>
#include <bitset>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

namespace {

// Process-global hooks, one recording at a time (see the header). Everything here runs
// on the GUI thread: a low-level hook is dispatched on the thread that installed it.
HHOOK s_mouse = nullptr;
HHOOK s_keyboard = nullptr;
platform::IInputWatch::Callbacks s_cb;
// Virtual keys currently down. A held key repeats WM_KEYDOWN with no WM_KEYUP between,
// and the contract is one `down` per real press.
std::bitset<256> s_down;

// Win32 virtual key -> Qt::Key; 0 for keys whose name depends on the layout (OEM
// punctuation) or that Qt::Key cannot name usefully (numpad) — those are shown by
// their key-cap label instead.
int qtKeyFromVk(DWORD vk)
{
    if (vk >= 'A' && vk <= 'Z') return Qt::Key_A + int(vk - 'A');
    if (vk >= '0' && vk <= '9') return Qt::Key_0 + int(vk - '0');
    if (vk >= VK_F1 && vk <= VK_F24) return Qt::Key_F1 + int(vk - VK_F1);
    switch (vk) {
        case VK_SHIFT:   case VK_LSHIFT:   case VK_RSHIFT:   return Qt::Key_Shift;
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return Qt::Key_Control;
        case VK_MENU:    case VK_LMENU:    case VK_RMENU:    return Qt::Key_Alt;
        case VK_LWIN:    case VK_RWIN:                       return Qt::Key_Meta;
        case VK_ESCAPE:  return Qt::Key_Escape;
        case VK_TAB:     return Qt::Key_Tab;
        case VK_SPACE:   return Qt::Key_Space;
        case VK_RETURN:  return Qt::Key_Return;
        case VK_BACK:    return Qt::Key_Backspace;
        case VK_DELETE:  return Qt::Key_Delete;
        case VK_INSERT:  return Qt::Key_Insert;
        case VK_HOME:    return Qt::Key_Home;
        case VK_END:     return Qt::Key_End;
        case VK_PRIOR:   return Qt::Key_PageUp;
        case VK_NEXT:    return Qt::Key_PageDown;
        case VK_UP:      return Qt::Key_Up;
        case VK_DOWN:    return Qt::Key_Down;
        case VK_LEFT:    return Qt::Key_Left;
        case VK_RIGHT:   return Qt::Key_Right;
        case VK_SNAPSHOT:return Qt::Key_Print;
        case VK_PAUSE:   return Qt::Key_Pause;
        case VK_CAPITAL: return Qt::Key_CapsLock;
    }
    return 0;
}

// What the key types under the foreground window's layout. The keyboard state is
// rebuilt from the async state, because this thread's own state (GetKeyboardState) is
// not the foreground app's. Flag 0x4 (Windows 10 1607+) keeps ToUnicodeEx from
// changing the kernel's keyboard state — without it, querying would eat a pending
// dead key and the user's next accented letter would come out wrong.
QString typedText(const KBDLLHOOKSTRUCT& k, HKL layout)
{
    BYTE state[256] = {};
    for (int vk : {VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL,
                   VK_MENU, VK_LMENU, VK_RMENU})
        if (GetAsyncKeyState(vk) & 0x8000)
            state[vk] = 0x80;
    if (GetKeyState(VK_CAPITAL) & 1)
        state[VK_CAPITAL] = 0x01;
    wchar_t buf[8];
    const int n = ToUnicodeEx(k.vkCode, k.scanCode, state, buf, 8, 0x4, layout);
    return n > 0 ? QString::fromWCharArray(buf, n) : QString();   // < 0: a dead key
}

// The key-cap name: its unshifted character, else what Windows calls the key.
QString keyLabel(const KBDLLHOOKSTRUCT& k, HKL layout)
{
    const UINT ch = MapVirtualKeyExW(k.vkCode, MAPVK_VK_TO_CHAR, layout) & 0xFFFF;
    if (ch >= 0x20)
        return QString(QChar(char16_t(ch)));
    const LONG lp = LONG((k.scanCode & 0xFF) << 16) | ((k.flags & LLKHF_EXTENDED) ? (1 << 24) : 0);
    wchar_t name[64];
    const int n = GetKeyNameTextW(lp, name, 64);
    return n > 0 ? QString::fromWCharArray(name, n) : QString();
}

LRESULT CALLBACK mouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION) {
        const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        const QPoint p(ms->pt.x, ms->pt.y);       // virtual-desktop physical px
        switch (wParam) {
            case WM_MOUSEMOVE:   if (s_cb.onMove) s_cb.onMove(p); break;
            case WM_LBUTTONDOWN: if (s_cb.onButton) s_cb.onButton(Qt::LeftButton, true, p); break;
            case WM_LBUTTONUP:   if (s_cb.onButton) s_cb.onButton(Qt::LeftButton, false, p); break;
            case WM_RBUTTONDOWN: if (s_cb.onButton) s_cb.onButton(Qt::RightButton, true, p); break;
            case WM_RBUTTONUP:   if (s_cb.onButton) s_cb.onButton(Qt::RightButton, false, p); break;
            case WM_MBUTTONDOWN: if (s_cb.onButton) s_cb.onButton(Qt::MiddleButton, true, p); break;
            case WM_MBUTTONUP:   if (s_cb.onButton) s_cb.onButton(Qt::MiddleButton, false, p); break;
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                if (s_cb.onWheel) {
                    const short delta = short(HIWORD(ms->mouseData));
                    // Vertical positive is up; horizontal positive is right.
                    s_cb.onWheel(delta > 0, wParam == WM_MOUSEHWHEEL, p);
                }
                break;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);     // never swallow
}

LRESULT CALLBACK keyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && s_cb.onKey) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const DWORD vk = k->vkCode & 0xFF;
        const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool fresh = down ? !s_down.test(vk) : true;
        s_down.set(vk, down);
        if (fresh && vk != VK_PACKET) {            // VK_PACKET: injected Unicode, no key
            const HKL layout =
                GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
            const QString text = down ? typedText(*k, layout) : QString();
            s_cb.onKey(qtKeyFromVk(vk), down, text, down ? keyLabel(*k, layout) : QString());
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);     // never swallow
}

} // namespace

WinInputWatch::~WinInputWatch() { disarm(); }

void WinInputWatch::arm(Callbacks cb)
{
    disarm();                                     // never stack hooks
    s_cb = std::move(cb);
    s_down.reset();
    const HINSTANCE mod = GetModuleHandleW(nullptr);
    s_mouse = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, mod, 0);
    s_keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, mod, 0);
    POINT pt;
    if (s_cb.onMove && GetCursorPos(&pt))
        s_cb.onMove(QPoint(pt.x, pt.y));
}

void WinInputWatch::disarm()
{
    if (s_mouse)    { UnhookWindowsHookEx(s_mouse);    s_mouse = nullptr; }
    if (s_keyboard) { UnhookWindowsHookEx(s_keyboard); s_keyboard = nullptr; }
    s_cb = Callbacks();
}
