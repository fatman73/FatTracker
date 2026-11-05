// TFMX audio decoder library by Michael Schwendt

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, see <https://www.gnu.org/licenses/>.

#include "TFMXDecoder.h"

// Support for loading the separate file containing the samples.

#include <cstring>
#include <limits>

void tfmxaudiodecoder::TFMXDecoder::setSamples(const void* data, udword length) {
    const ubyte* p = static_cast<const ubyte*>(data);
    input.smplData.assign(p, p + length);
    input.smplLoaded = false;
}

// FatTracker: the SMPL file comes from memory (setSamples) instead of a file callback.
bool tfmxaudiodecoder::TFMXDecoder::loadSamplesFile() {
    if (input.smplLoaded) {  // if loaded before, reuse it
        input.mdatSize = input.mdatSizeCurrent;
        input.smplSize = input.smplSizeCurrent;
        return true;
    }
    if (input.smplData.empty()) {
        return false;
    }
    const size_t fLenRaw = input.smplData.size();
    if (fLenRaw > std::numeric_limits<udword>::max() - input.bufLen) {
        return false;
    }
    udword fLen = static_cast<udword>(fLenRaw);
    char* newInputBuf = new char[fLen + input.bufLen];
    memcpy(newInputBuf, input.buf, input.bufLen);
    memcpy(newInputBuf + input.len, input.smplData.data(), fLen);
    delete[] input.buf;
    input.buf = reinterpret_cast<ubyte*>(newInputBuf);
    input.smplSize = fLen;
    input.mdatSize = input.len - offsets.header;
    offsets.sampleData = input.len;
    input.bufLen += input.smplSize;
    input.len += input.smplSize;
    pBuf.setBuffer(input.buf, input.bufLen);
    return true;
}
