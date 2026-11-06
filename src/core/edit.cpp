#include "core/edit.h"

#include <algorithm>

namespace {
constexpr size_t kUndoLimit = 100;
constexpr int kAll = -1;

bool validCell(const Song& s, int pat, int row, int ch) {
    return pat >= 0 && pat < (int)s.patterns.size() && row >= 0 && row < s.patterns[pat].rows && ch >= 0 &&
           ch < s.patterns[pat].channels;
}
}  // namespace

// ---------------------------------------------------------------- history

Editor::Snap Editor::capture(const Song& s, const std::vector<int>& pats, bool instruments) const {
    Snap snap;
    if (instruments) {
        snap.hasInstruments = true;
        snap.instruments = s.instruments;
    }
    snap.order = s.order;
    snap.patternCount = s.patterns.size();
    snap.numChannels = s.numChannels;
    if (!pats.empty() && pats[0] == kAll) {
        snap.idx = {kAll};
        snap.pats = s.patterns;
    } else {
        for (int p : pats)
            if (p >= 0 && p < (int)s.patterns.size()) {
                snap.idx.push_back(p);
                snap.pats.push_back(s.patterns[p]);
            }
    }
    return snap;
}

void Editor::restore(Song& s, const Snap& snap) const {
    if (!snap.idx.empty() && snap.idx[0] == kAll) {
        s.patterns = snap.pats;
        s.numChannels = snap.numChannels;  // whole-song snapshots carry the channel count
    } else {
        if (s.patterns.size() > snap.patternCount) s.patterns.resize(snap.patternCount);
        for (size_t i = 0; i < snap.idx.size(); ++i) {
            if (snap.idx[i] >= (int)s.patterns.size()) continue;
            Pattern p = snap.pats[i];
            if (p.channels != s.numChannels) {  // snapshot taken at another channel count: adapt it
                Pattern q = makePattern(p.rows, s.numChannels);
                const int keep = p.channels < s.numChannels ? p.channels : s.numChannels;
                for (int r = 0; r < p.rows; ++r)
                    for (int c = 0; c < keep; ++c) q.at(r, c) = p.at(r, c);
                p = static_cast<Pattern&&>(q);
            }
            s.patterns[snap.idx[i]] = static_cast<Pattern&&>(p);
        }
    }
    s.order = snap.order;
    if (snap.hasInstruments) s.instruments = snap.instruments;
}

void Editor::record(const Song& s, const std::vector<int>& pats) {
    undo_.push_back(capture(s, pats));
    if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
    redo_.clear();
    dirty_ = true;
}

bool Editor::undo(Song& s) {
    if (undo_.empty()) return false;
    Snap top = static_cast<Snap&&>(undo_.back());
    undo_.pop_back();
    redo_.push_back(capture(s, top.idx.empty() ? std::vector<int>{} : top.idx, top.hasInstruments));
    restore(s, top);
    dirty_ = true;
    return true;
}

bool Editor::redo(Song& s) {
    if (redo_.empty()) return false;
    Snap top = static_cast<Snap&&>(redo_.back());
    redo_.pop_back();
    undo_.push_back(capture(s, top.idx, top.hasInstruments));
    restore(s, top);
    dirty_ = true;
    return true;
}

void Editor::recordInstruments(const Song& s) {
    undo_.push_back(capture(s, {}, true));
    if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
    redo_.clear();
    dirty_ = true;
}

// ---------------------------------------------------------------- cells

void Editor::setNote(Song& s, int pat, int row, int ch, uint8_t note, uint8_t instr) {
    if (!validCell(s, pat, row, ch)) return;
    record(s, {pat});
    Cell& c = s.patterns[pat].at(row, ch);
    c.note = note;
    if (instr) c.instr = instr;
}

void Editor::setDigit(Song& s, int pat, int row, int ch, int column, int d) {
    if (!validCell(s, pat, row, ch) || d < 0) return;
    if (column != kColFx && d > 15) return;
    if (column == kColFx && d > 35) return;
    record(s, {pat});
    Cell& c = s.patterns[pat].at(row, ch);
    switch (column) {
        case kColInstrHi:
            c.instr = (uint8_t)((d << 4) | (c.instr & 15));
            break;
        case kColInstrLo:
            c.instr = (uint8_t)((c.instr & 0xF0) | d);
            break;
        case kColVolHi:
            if (d <= 4) {
                int value = (c.vol >= 0x10 && c.vol <= 0x50) ? c.vol - 0x10 : 0;
                value = (d << 4) | (value & 15);
                c.vol = (uint8_t)(0x10 + (value > 0x40 ? 0x40 : value));
            } else if (d >= 6) {
                c.vol = (uint8_t)((d << 4) | (c.vol >= 0x60 ? (c.vol & 15) : 0));
            }
            break;
        case kColVolLo:
            if (c.vol >= 0x60) {
                c.vol = (uint8_t)((c.vol & 0xF0) | d);
            } else {
                int value = (c.vol >= 0x10 && c.vol <= 0x50) ? c.vol - 0x10 : 0;
                value = (value & 0xF0) | d;
                c.vol = (uint8_t)(0x10 + (value > 0x40 ? 0x40 : value));
            }
            break;
        case kColFx:
            c.fx = (uint8_t)d;
            break;
        case kColParamHi:
            c.param = (uint8_t)((d << 4) | (c.param & 15));
            break;
        case kColParamLo:
            c.param = (uint8_t)((c.param & 0xF0) | d);
            break;
    }
}

void Editor::clearCell(Song& s, int pat, int row, int ch, int column) {
    if (!validCell(s, pat, row, ch)) return;
    record(s, {pat});
    Cell& c = s.patterns[pat].at(row, ch);
    if (column < 0) {
        c = Cell{};
    } else if (column == kColNote) {
        c.note = 0;
        c.instr = 0;
    } else if (column <= kColInstrLo) {
        c.instr = 0;
    } else if (column <= kColVolLo) {
        c.vol = 0;
    } else {
        c.fx = c.param = 0;
    }
}

// ---------------------------------------------------------------- rows

void Editor::insertRow(Song& s, int pat, int row, int ch) {
    if (!validCell(s, pat, row, ch < 0 ? 0 : ch)) return;
    record(s, {pat});
    Pattern& p = s.patterns[pat];
    const int c0 = ch < 0 ? 0 : ch, c1 = ch < 0 ? p.channels - 1 : ch;
    for (int c = c0; c <= c1; ++c) {
        for (int r = p.rows - 1; r > row; --r) p.at(r, c) = p.at(r - 1, c);
        p.at(row, c) = Cell{};
    }
}

void Editor::deleteRow(Song& s, int pat, int row, int ch) {
    if (!validCell(s, pat, row, ch < 0 ? 0 : ch)) return;
    record(s, {pat});
    Pattern& p = s.patterns[pat];
    const int c0 = ch < 0 ? 0 : ch, c1 = ch < 0 ? p.channels - 1 : ch;
    for (int c = c0; c <= c1; ++c) {
        for (int r = row; r < p.rows - 1; ++r) p.at(r, c) = p.at(r + 1, c);
        p.at(p.rows - 1, c) = Cell{};
    }
}

// ---------------------------------------------------------------- blocks

Block Editor::normalize(const Song& s, Block b) {
    if (b.row0 > b.row1) std::swap(b.row0, b.row1);
    if (b.ch0 > b.ch1) std::swap(b.ch0, b.ch1);
    if (b.pattern < 0 || b.pattern >= (int)s.patterns.size()) return {b.pattern, 0, -1, 0, -1};
    const Pattern& p = s.patterns[b.pattern];
    b.row0 = std::max(0, b.row0);
    b.ch0 = std::max(0, b.ch0);
    b.row1 = std::min(p.rows - 1, b.row1);
    b.ch1 = std::min(p.channels - 1, b.ch1);
    return b;
}

void Editor::clear(Song& s, const Block& in) {
    Block b = normalize(s, in);
    if (b.row1 < b.row0 || b.ch1 < b.ch0) return;
    record(s, {b.pattern});
    for (int r = b.row0; r <= b.row1; ++r)
        for (int c = b.ch0; c <= b.ch1; ++c) s.patterns[b.pattern].at(r, c) = Cell{};
}

Clip Editor::copy(const Song& s, const Block& in) const {
    Clip clip;
    Block b = normalize(s, in);
    if (b.row1 < b.row0 || b.ch1 < b.ch0) return clip;
    clip.rows = b.row1 - b.row0 + 1;
    clip.channels = b.ch1 - b.ch0 + 1;
    for (int r = b.row0; r <= b.row1; ++r)
        for (int c = b.ch0; c <= b.ch1; ++c) clip.cells.push_back(s.patterns[b.pattern].at(r, c));
    return clip;
}

void Editor::paste(Song& s, int pat, int row, int ch, const Clip& clip) {
    if (!validCell(s, pat, row, ch) || clip.rows <= 0) return;
    record(s, {pat});
    Pattern& p = s.patterns[pat];
    for (int r = 0; r < clip.rows && row + r < p.rows; ++r)
        for (int c = 0; c < clip.channels && ch + c < p.channels; ++c)
            p.at(row + r, ch + c) = clip.cells[(size_t)r * clip.channels + c];
}

void Editor::exchange(Song& s, const Block& in, Clip& clip) {
    Block b = normalize(s, in);
    if (b.row1 < b.row0 || b.ch1 < b.ch0) return;
    Clip old = copy(s, b);
    if (clip.rows > 0) paste(s, b.pattern, b.row0, b.ch0, clip);
    clip = static_cast<Clip&&>(old);
}

void Editor::transpose(Song& s, const Block& in, int semis, int onlyInstr) {
    Block b = normalize(s, in);
    if (b.row1 < b.row0 || b.ch1 < b.ch0) return;
    record(s, {b.pattern});
    for (int r = b.row0; r <= b.row1; ++r)
        for (int c = b.ch0; c <= b.ch1; ++c) {
            Cell& cell = s.patterns[b.pattern].at(r, c);
            if (cell.note == 0 || cell.note == NOTE_OFF) continue;
            if (onlyInstr && cell.instr != onlyInstr) continue;
            const int n = std::clamp((int)cell.note + semis, 1, 96);
            cell.note = (uint8_t)n;
        }
}

void Editor::transposeSong(Song& s, int semis, int onlyInstr) {
    record(s, {kAll});
    for (Pattern& p : s.patterns)
        for (Cell& cell : p.cells) {
            if (cell.note == 0 || cell.note == NOTE_OFF) continue;
            if (onlyInstr && cell.instr != onlyInstr) continue;
            cell.note = (uint8_t)std::clamp((int)cell.note + semis, 1, 96);
        }
}

void Editor::rotate(Song& s, const Block& in, int dir) {
    Block b = normalize(s, in);
    const int n = b.row1 - b.row0 + 1;
    if (n < 2 || b.ch1 < b.ch0) return;
    record(s, {b.pattern});
    Pattern& p = s.patterns[b.pattern];
    for (int c = b.ch0; c <= b.ch1; ++c) {
        std::vector<Cell> col(n);
        for (int i = 0; i < n; ++i) col[i] = p.at(b.row0 + i, c);
        for (int i = 0; i < n; ++i) p.at(b.row0 + ((i + dir) % n + n) % n, c) = col[i];
    }
}

void Editor::exchangeInstrument(Song& s, int from, int to, bool swap) {
    if (from < 1 || to < 1 || from > 255 || to > 255 || from == to) return;
    record(s, {kAll});
    for (Pattern& p : s.patterns)
        for (Cell& c : p.cells) {
            if (c.instr == from)
                c.instr = (uint8_t)to;
            else if (swap && c.instr == to)
                c.instr = (uint8_t)from;
        }
}

void Editor::flip(Song& s, const Block& in) {
    Block b = normalize(s, in);
    if (b.row1 <= b.row0 || b.ch1 < b.ch0) return;
    record(s, {b.pattern});
    Pattern& p = s.patterns[b.pattern];
    for (int c = b.ch0; c <= b.ch1; ++c)
        for (int i = 0; i < (b.row1 - b.row0 + 1) / 2; ++i) std::swap(p.at(b.row0 + i, c), p.at(b.row1 - i, c));
}

// ---------------------------------------------------------------- patterns and positions

void Editor::setPatternRows(Song& s, int pat, int rows) {
    if (pat < 0 || pat >= (int)s.patterns.size()) return;
    rows = std::clamp(rows, 1, 256);
    Pattern& old = s.patterns[pat];
    if (rows == old.rows) return;
    record(s, {pat});
    Pattern np = makePattern(rows, old.channels);
    const int keep = std::min<int>(rows, old.rows);
    for (int r = 0; r < keep; ++r)
        for (int c = 0; c < old.channels; ++c) np.at(r, c) = old.at(r, c);
    s.patterns[pat] = static_cast<Pattern&&>(np);
}

int Editor::newPattern(Song& s, int rows) {
    if (s.patterns.size() >= 256) return -1;
    record(s, {kAll});
    s.patterns.push_back(makePattern(std::clamp(rows, 1, 256), s.numChannels));
    return (int)s.patterns.size() - 1;
}

void Editor::insertOrder(Song& s, int pos, uint8_t pattern) {
    if (s.order.size() >= 256 || pattern >= s.patterns.size()) return;
    pos = std::clamp(pos, 0, (int)s.order.size());
    record(s, {});
    s.order.insert(s.order.begin() + pos, pattern);
}

void Editor::deleteOrder(Song& s, int pos) {
    if (s.order.size() <= 1 || pos < 0 || pos >= (int)s.order.size()) return;
    record(s, {});
    s.order.erase(s.order.begin() + pos);
    if (s.restartPos >= s.order.size()) s.restartPos = 0;
}

void Editor::setOrder(Song& s, int pos, uint8_t pattern) {
    if (pos < 0 || pos >= (int)s.order.size() || pattern >= s.patterns.size()) return;
    record(s, {});
    s.order[pos] = pattern;
}

void Editor::setChannels(Song& s, int channels) {
    record(s, {kAll});
    setChannelCount(s, channels);
}

void Editor::clearSong(Song& s) {
    record(s, {kAll});
    s.patterns.clear();
    s.patterns.push_back(makePattern(64, s.numChannels));
    s.order = {0};
    s.restartPos = 0;
}
