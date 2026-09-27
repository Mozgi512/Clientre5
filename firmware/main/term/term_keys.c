#include "term_keys.h"
#include <string.h>
#include <stdio.h>

static size_t put(char *out, size_t cap, const char *s) { size_t n = strlen(s); if (n >= cap) n = cap - 1; memcpy(out, s, n); out[n] = 0; return n; }

size_t term_keys_encode(const key_event_t *ev, const vt_t *vt, char *out, size_t cap)
{
    if (cap < 8) return 0;
    bool app = vt && vt_app_cursor(vt);
    uint8_t m = ev->mods;
    int mod = 1 + ((m & MOD_SHIFT) ? 1 : 0) + ((m & MOD_ALT) ? 2 : 0) + ((m & MOD_CTRL) ? 4 : 0);
    if (ev->type == KEY_EV_CHAR) {
        uint32_t c = ev->cp;
        size_t n = 0;
        if (m & MOD_ALT) out[n++] = 0x1b;
        if (m & MOD_CTRL) {
            if (c >= 'a' && c <= 'z') c = c - 'a' + 1;
            else if (c >= 'A' && c <= 'Z') c = c - 'A' + 1;
            else if (c == ' ' || c == '@' || c == '2') c = 0;
            else if (c == '[' || c == '3') c = 0x1b;
            else if (c == '\\' || c == '4') c = 0x1c;
            else if (c == ']' || c == '5') c = 0x1d;
            else if (c == '^' || c == '6') c = 0x1e;
            else if (c == '_' || c == '7' || c == '-') c = 0x1f;
            else if (c == '?' || c == '8') c = 0x7f;
            out[n++] = (char)c; out[n] = 0; return n;
        }
        if (c < 0x80) out[n++] = c;
        else if (c < 0x800) { out[n++] = 0xC0 | (c >> 6); out[n++] = 0x80 | (c & 0x3F); }
        else if (c < 0x10000) { out[n++] = 0xE0 | (c >> 12); out[n++] = 0x80 | ((c >> 6) & 0x3F); out[n++] = 0x80 | (c & 0x3F); }
        else { out[n++] = 0xF0 | (c >> 18); out[n++] = 0x80 | ((c >> 12) & 0x3F); out[n++] = 0x80 | ((c >> 6) & 0x3F); out[n++] = 0x80 | (c & 0x3F); }
        out[n] = 0; return n;
    }
    char buf[16];
    const char *seq = NULL;
    switch (ev->key) {
    case SK_ENTER: return put(out, cap, (m & MOD_ALT) ? "\x1b\r" : "\r");
    case SK_ESC: return put(out, cap, "\x1b");
    case SK_TAB: return put(out, cap, (m & MOD_SHIFT) ? "\x1b[Z" : "\t");
    case SK_BACKSPACE: return put(out, cap, (m & MOD_ALT) ? "\x1b\x7f" : ((m & MOD_CTRL) ? "\x08" : "\x7f"));
    case SK_UP: case SK_DOWN: case SK_RIGHT: case SK_LEFT: case SK_HOME: case SK_END: {
        char letter = ev->key == SK_UP ? 'A' : ev->key == SK_DOWN ? 'B' : ev->key == SK_RIGHT ? 'C' : ev->key == SK_LEFT ? 'D' : ev->key == SK_HOME ? 'H' : 'F';
        if (mod > 1) snprintf(buf, sizeof(buf), "\x1b[1;%d%c", mod, letter);
        else snprintf(buf, sizeof(buf), "\x1b%c%c", app ? 'O' : '[', letter);
        return put(out, cap, buf);
    }
    case SK_INSERT: seq = "2"; break;
    case SK_DELETE: seq = "3"; break;
    case SK_PGUP: seq = "5"; break;
    case SK_PGDN: seq = "6"; break;
    case SK_F1: return put(out, cap, mod > 1 ? (snprintf(buf, sizeof(buf), "\x1b[1;%dP", mod), buf) : "\x1bOP");
    case SK_F2: return put(out, cap, mod > 1 ? (snprintf(buf, sizeof(buf), "\x1b[1;%dQ", mod), buf) : "\x1bOQ");
    case SK_F3: return put(out, cap, mod > 1 ? (snprintf(buf, sizeof(buf), "\x1b[1;%dR", mod), buf) : "\x1bOR");
    case SK_F4: return put(out, cap, mod > 1 ? (snprintf(buf, sizeof(buf), "\x1b[1;%dS", mod), buf) : "\x1bOS");
    case SK_F5: seq = "15"; break;
    case SK_F6: seq = "17"; break;
    case SK_F7: seq = "18"; break;
    case SK_F8: seq = "19"; break;
    case SK_F9: seq = "20"; break;
    case SK_F10: seq = "21"; break;
    case SK_F11: seq = "23"; break;
    case SK_F12: seq = "24"; break;
    default: return 0;
    }
    if (mod > 1) snprintf(buf, sizeof(buf), "\x1b[%s;%d~", seq, mod);
    else snprintf(buf, sizeof(buf), "\x1b[%s~", seq);
    return put(out, cap, buf);
}
