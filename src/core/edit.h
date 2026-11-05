// Song editing operations with undo/redo (pure C++, no Windows). The UI calls these
// inside Engine::edit so that edits are safe while the song is playing.
#pragma once
#include <cstdint>
#include <vector>

#include "core/song.h"

// Cursor columns inside a cell.
enum : int {
    kColNote = 0,
    kColInstrHi = 1,
    kColInstrLo = 2,
    kColVolHi = 3,
    kColVolLo = 4,
    kColFx = 5,
    kColParamHi = 6,
    kColParamLo = 7,
    kColCount = 8
};

// Inclusive row/channel ranges inside one pattern (normalized on use).
struct Block {
    int pattern, row0, row1, ch0, ch1;
};

struct Clip {
    int rows = 0, channels = 0;
    std::vector<Cell> cells;  // rows * channels
};

class Editor {
public:
    // cells
    void setNote(Song& s, int pat, int row, int ch, uint8_t note, uint8_t instr);
    void setDigit(Song& s, int pat, int row, int ch, int column, int digit);  // digit 0..35 for kColFx
    void clearCell(Song& s, int pat, int row, int ch, int column);           // column -1 = whole cell
    // rows (ch = -1: all channels)
    void insertRow(Song& s, int pat, int row, int ch);
    void deleteRow(Song& s, int pat, int row, int ch);
    // blocks
    void clear(Song& s, const Block& b);
    Clip copy(const Song& s, const Block& b) const;
    void paste(Song& s, int pat, int row, int ch, const Clip& c);
    void exchange(Song& s, const Block& b, Clip& clip);  // swaps the block with the clipboard
    void transpose(Song& s, const Block& b, int semitones, int onlyInstr);
    void transposeSong(Song& s, int semitones, int onlyInstr);
    void rotate(Song& s, const Block& b, int dir);  // -1 up, +1 down (wraps)
    void exchangeInstrument(Song& s, int from, int to, bool swap);  // in all patterns
    void flip(Song& s, const Block& b);             // reverse row order
    // patterns and positions
    void setPatternRows(Song& s, int pat, int rows);
    int newPattern(Song& s, int rows);  // returns the new index or -1
    void insertOrder(Song& s, int pos, uint8_t pattern);
    void deleteOrder(Song& s, int pos);
    void setOrder(Song& s, int pos, uint8_t pattern);
    void clearSong(Song& s);  // all patterns empty, one position
    void setChannels(Song& s, int channels);  // undoable channel count change
    // history
    bool undo(Song& s);
    bool redo(Song& s);
    bool canUndo() const { return !undo_.empty(); }
    void reset() { undo_.clear(); redo_.clear(); }
    bool dirty() const { return dirty_; }
    void setClean() { dirty_ = false; }
    void markDirty() { dirty_ = true; }

    static Block normalize(const Song& s, Block b);

private:
    struct Snap {
        std::vector<int> idx;           // indices of saved patterns (-1 = all)
        std::vector<Pattern> pats;
        std::vector<uint8_t> order;
        size_t patternCount = 0;
        uint8_t numChannels = 4;
    };
    Snap capture(const Song& s, const std::vector<int>& pats) const;
    void restore(Song& s, const Snap& snap) const;
    void record(const Song& s, const std::vector<int>& pats);  // push undo, clear redo

    std::vector<Snap> undo_, redo_;
    bool dirty_ = false;
};
