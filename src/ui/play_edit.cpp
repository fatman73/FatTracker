// Pattern editing on the main screen: cursor, note/digit entry, blocks, side panel operations.
#include <cstdio>

#include "core/keymap.h"
#include "ui/play_screen.h"

namespace {
constexpr unsigned kVkNoteOff = '1';  // key-off in the note column (FT2 style)
}

void PlayScreen::ask(const char* text, std::initializer_list<const char*> buttons, std::function<void(int)> onAnswer) {
    dialog_ = Dialog{};
    dialog_.open = true;
    std::snprintf(dialog_.text, sizeof dialog_.text, "%s", text);
    for (const char* b : buttons)
        if (dialog_.count < 4) dialog_.buttons[dialog_.count++] = b;
    dialog_.onAnswer = static_cast<std::function<void(int)>&&>(onAnswer);
}

int PlayScreen::editRow() const { return (recordMode_ && last_.playing) ? last_.row : viewRow_; }

int PlayScreen::editPattern() const { return last_.playing ? last_.pattern : viewPattern_; }

Block PlayScreen::currentBlock(bool wholeTrackIfNone) const {
    const int pat = editPattern(), row = editRow();
    if (blockOn_)
        return {pat, blockRow_ < row ? blockRow_ : row, blockRow_ < row ? row : blockRow_,
                blockCh_ < curCh_ ? blockCh_ : curCh_, blockCh_ < curCh_ ? curCh_ : blockCh_};
    if (wholeTrackIfNone)
        return scopePattern_ ? Block{pat, 0, 255, 0, kMaxChannels - 1} : Block{pat, 0, 255, curCh_, curCh_};
    return {pat, row, row, curCh_, curCh_};
}

void PlayScreen::ensureCursorVisible(const Song& s) {
    if (curCh_ >= s.numChannels) curCh_ = s.numChannels - 1;
    if (curCh_ < 0) curCh_ = 0;
    if (curCh_ < firstChannel_) firstChannel_ = curCh_;
    if (curCh_ >= firstChannel_ + viewMode_) firstChannel_ = curCh_ - viewMode_ + 1;
    firstChannel_ = computePatternLayout(viewMode_, s.numChannels, firstChannel_, 1.f).firstChannel;
}

void PlayScreen::moveCursor(int rows, int cols, Engine& e) {
    const Song* s = e.song();
    if (!s || s->patterns.empty()) return;
    if (rows && !(recordMode_ && last_.playing)) {
        const int pat = viewPattern_ < (int)s->patterns.size() ? viewPattern_ : 0;
        const int n = s->patterns[pat].rows;
        viewRow_ = ((viewRow_ + rows) % n + n) % n;
    }
    if (cols) {
        curCol_ += cols;
        if (curCol_ < 0) {
            curCol_ = kColCount - 1;
            curCh_ = curCh_ > 0 ? curCh_ - 1 : s->numChannels - 1;
        } else if (curCol_ >= kColCount) {
            curCol_ = 0;
            curCh_ = curCh_ + 1 < s->numChannels ? curCh_ + 1 : 0;
        }
    }
    ensureCursorVisible(*s);
}

void PlayScreen::advance(Engine& e) {
    if (editSkip_ > 0) moveCursor(editSkip_, 0, e);
}

UiAction PlayScreen::editKey(unsigned vk, bool shift, bool ctrl, bool alt, bool repeat, Engine& e) {
    const Song* s = e.song();
    if (!s || s->patterns.empty()) return UiAction::None;
    const int pat = editPattern(), row = editRow();
    auto extend = [&]() {
        if (!blockOn_) {
            blockOn_ = true;
            blockRow_ = viewRow_;
            blockCh_ = curCh_;
        }
    };
    // ---- history and clipboard
    if (ctrl && !alt) {
        switch (vk) {
            case 'Z':
                e.edit([&](Song& song) { editor_.undo(song); });
                setStatus(editor_.canUndo() ? "UNDO" : "NOTHING MORE TO UNDO");
                return UiAction::Redraw;
            case 'Y':
                e.edit([&](Song& song) { editor_.redo(song); });
                setStatus("REDO");
                return UiAction::Redraw;
            case 'C':
            case 'X': {
                const Block b = currentBlock(false);
                clip_ = editor_.copy(*s, b);
                if (vk == 'X') e.edit([&](Song& song) { editor_.clear(song, b); });
                setStatus(vk == 'X' ? "BLOCK CUT" : "BLOCK COPIED");
                return UiAction::Redraw;
            }
            case 'V':
                e.edit([&](Song& song) { editor_.paste(song, pat, row, curCh_, clip_); });
                setStatus("BLOCK PASTED");
                return UiAction::Redraw;
            case 'A':  // select the whole track
                blockOn_ = true;
                blockRow_ = 0;
                blockCh_ = curCh_;
                viewRow_ = s->patterns[pat].rows - 1;
                return UiAction::Redraw;
        }
    }
    // ---- track / pattern cut-copy-paste (Shift / Alt + F3 F4 F5, as in ProTracker)
    if ((shift || alt) && vk >= VK_F3 && vk <= VK_F5) {
        const Block b = alt ? Block{pat, 0, 255, 0, kMaxChannels - 1} : Block{pat, 0, 255, curCh_, curCh_};
        if (vk != VK_F5) clip_ = editor_.copy(*s, b);
        if (vk == VK_F3) e.edit([&](Song& song) { editor_.clear(song, b); });
        if (vk == VK_F5) e.edit([&](Song& song) { editor_.paste(song, pat, 0, alt ? 0 : curCh_, clip_); });
        setStatus(vk == VK_F3 ? "CUT" : vk == VK_F4 ? "COPIED" : "PASTED");
        return UiAction::Redraw;
    }
    if (alt) return UiAction::None;
    // ---- navigation
    switch (vk) {
        case VK_UP:
        case VK_DOWN:
            if (shift) extend(); else if (!ctrl) blockOn_ = false;
            moveCursor(vk == VK_UP ? -1 : 1, 0, e);
            return UiAction::Redraw;
        case VK_PRIOR:
        case VK_NEXT:
            if (shift) extend(); else blockOn_ = false;
            moveCursor(vk == VK_PRIOR ? -16 : 16, 0, e);
            return UiAction::Redraw;
        case VK_HOME:
            viewRow_ = 0;
            return UiAction::Redraw;
        case VK_END:
            viewRow_ = s->patterns[pat].rows - 1;
            return UiAction::Redraw;
        case VK_LEFT:
        case VK_RIGHT:
            if (ctrl) return UiAction::None;  // handled by the caller (pattern change)
            if (shift) {
                extend();
                curCh_ += vk == VK_RIGHT ? 1 : -1;
                ensureCursorVisible(*s);
            } else {
                blockOn_ = false;
                moveCursor(0, vk == VK_RIGHT ? 1 : -1, e);
            }
            return UiAction::Redraw;
        case VK_TAB:
            curCol_ = 0;
            curCh_ += shift ? -1 : 1;
            if (curCh_ < 0) curCh_ = s->numChannels - 1;
            if (curCh_ >= s->numChannels) curCh_ = 0;
            ensureCursorVisible(*s);
            return UiAction::Redraw;
        case VK_ESCAPE:
            blockOn_ = false;
            return UiAction::Redraw;
        case VK_F1:
        case VK_F2:
            octave_ += vk == VK_F1 ? -1 : 1;
            octave_ = octave_ < 0 ? 0 : (octave_ > 6 ? 6 : octave_);
            {
                char m[32];
                std::snprintf(m, sizeof m, "OCTAVE %d", octave_);
                setStatus(m);
            }
            return UiAction::Redraw;
    }
    // ---- editing keys (edit mode only)
    if (editMode_) {
        switch (vk) {
            case VK_DELETE:
                if (blockOn_) {
                    const Block b = currentBlock(false);
                    e.edit([&](Song& song) { editor_.clear(song, b); });
                } else {
                    e.edit([&](Song& song) { editor_.clearCell(song, pat, row, curCh_, shift ? -1 : curCol_); });
                    advance(e);
                }
                return UiAction::Redraw;
            case VK_BACK:
                if (row > 0) {
                    e.edit([&](Song& song) { editor_.deleteRow(song, pat, row - 1, shift ? -1 : curCh_); });
                    moveCursor(-1, 0, e);
                }
                return UiAction::Redraw;
            case VK_INSERT:
                e.edit([&](Song& song) { editor_.insertRow(song, pat, row, shift ? -1 : curCh_); });
                return UiAction::Redraw;
        }
    }
    if (ctrl) return UiAction::None;
    // ---- note column: piano keys (always jam, write in edit mode)
    if (curCol_ == kColNote) {
        int note = vk == kVkNoteOff ? NOTE_OFF : noteForKey(vk, octave_);
        if (note > 0) {
            if (repeat) return UiAction::None;
            if (note != NOTE_OFF) {
                e.previewNote(sample_, note);
                jamKey_ = vk;
            }
            if (editMode_) {
                const uint8_t instr = note == NOTE_OFF ? 0 : (uint8_t)(sample_ + 1);
                e.edit([&](Song& song) { editor_.setNote(song, pat, row, curCh_, (uint8_t)note, instr); });
                advance(e);
            }
            return UiAction::Redraw;
        }
        return UiAction::None;
    }
    // ---- digit columns
    if (editMode_) {
        const int d = curCol_ == kColFx ? fxDigit(vk) : hexDigit(vk);
        if (d >= 0) {
            e.edit([&](Song& song) { editor_.setDigit(song, pat, row, curCh_, curCol_, d); });
            advance(e);
            return UiAction::Redraw;
        }
    }
    return UiAction::None;
}

UiAction PlayScreen::onKeyUp(unsigned vk, Engine& e) {
    if (jamKey_ && vk == jamKey_) {
        e.previewStop();
        jamKey_ = 0;
    }
    return UiAction::None;
}

void PlayScreen::sideAction(int i, Engine& e) {
    const Song* s = e.song();
    if (!s || s->patterns.empty()) return;
    const int pat = editPattern(), row = editRow();
    const Block b = currentBlock(true);
    const int ch = scopePattern_ ? -1 : curCh_;
    const char* msg = nullptr;
    switch (i) {
        case 0:
            scopePattern_ = !scopePattern_;
            msg = scopePattern_ ? "OPERATIONS: WHOLE PATTERN" : "OPERATIONS: TRACK";
            break;
        case 1:
            e.edit([&](Song& song) { editor_.insertRow(song, pat, row, ch); });
            msg = "ROW INSERTED";
            break;
        case 2:
            e.edit([&](Song& song) { editor_.deleteRow(song, pat, row, ch); });
            msg = "ROW DELETED";
            break;
        case 3:
            clip_ = editor_.copy(*s, b);
            e.edit([&](Song& song) { editor_.clear(song, b); });
            msg = "CUT";
            break;
        case 4:
            clip_ = editor_.copy(*s, b);
            msg = "COPIED";
            break;
        case 5:
            e.edit([&](Song& song) {
                editor_.paste(song, pat, blockOn_ ? b.row0 : 0, scopePattern_ ? 0 : (blockOn_ ? b.ch0 : curCh_), clip_);
            });
            msg = "PASTED";
            break;
        case 6:
            e.edit([&](Song& song) { editor_.clear(song, b); });
            msg = "CLEARED";
            break;
        case 7:
        case 8:
        case 9:
        case 10: {
            const int semis = i == 7 ? 12 : i == 8 ? -12 : i == 9 ? 1 : -1;
            e.edit([&](Song& song) { editor_.transpose(song, b, semis, 0); });
            msg = i <= 8 ? "OCTAVE TRANSPOSED" : "NOTES TRANSPOSED";
            break;
        }
        case 11:
        case 12:
            e.edit([&](Song& song) { editor_.rotate(song, b, i == 11 ? -1 : 1); });
            msg = "ROTATED";
            break;
        case 13:
            e.edit([&](Song& song) { editor_.flip(song, b); });
            msg = "FLIPPED";
            break;
        case 14:
            e.edit([&](Song& song) { editor_.exchange(song, b, clip_); });
            msg = "EXCHANGED WITH CLIPBOARD";
            break;
        case 15:
            e.edit([&](Song& song) { editor_.undo(song); });
            msg = "UNDO";
            break;
    }
    if (msg) setStatus(msg);
}
