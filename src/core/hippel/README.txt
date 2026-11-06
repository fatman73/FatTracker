Jochen Hippel / Future Composer decoder taken from libtfmxaudiodecoder by Michael Schwendt
(https://github.com/mschwendt/libtfmxaudiodecoder, src/Jochen and the shared base files of src),
GNU GPL v2 or later (see COPYING). Formats: Future Composer 1.0-1.3 (SMOD) and 1.4 (FC14),
Jochen Hippel TFMX / TFMX 7V, COSO, MCMD.
Changes for FatTracker: namespace renamed to hippeldec and include guards prefixed with HIPPEL_
(the TFMX decoder in core/tfmx is an older copy of the same base classes), Config.h of the
autotools build removed, line endings LF. PaulaVoice is implemented by FatTracker
(core/hippel_player.cpp).
Because of this code, FatTracker binaries are covered by the GPL.
