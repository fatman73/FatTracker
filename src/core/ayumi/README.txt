ayumi - AY-3-8910 / YM2149 emulator by Peter Sovietov (https://github.com/true-grue/ayumi), MIT licence
(see LICENSE). Used by core/ay_player.cpp and the other AY players (line endings LF), unchanged apart from
one addition marked FatTracker: struct ayumi keeps each channel's output level of the last ayumi_process()
call (channel_out) for the per-voice meters and scopes; the left / right output is not affected.
