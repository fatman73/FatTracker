// Default (silent) Paula voice used until the player attaches its own voices.
#include "PaulaVoice.h"

namespace tfmxaudiodecoder {
void PaulaVoice::on() {}
void PaulaVoice::off() {}
void PaulaVoice::takeNextBuf() {}
uword PaulaVoice::getLoopCount() { return 0; }
}  // namespace tfmxaudiodecoder
