// FatTracker: a Multi_Buffer that gives each voice of a Classic_Emu its own Blip_Buffers, for per-voice
// meters and scopes, and still mixes exactly as the buffer it wraps. The voices write into their own
// buffers during a frame; at the end of the frame the deltas are moved into the wrapped buffer's channels
// (integer additions into the same positions, so what it mixes is bit-identical to writing there directly)
// and added into one mono buffer per voice, which is read alongside the mix.
// Not part of Game_Music_Emu (LGPL 2.1 like it, see LICENSE.txt).
#ifndef VOICE_TAP_BUFFER_H
#define VOICE_TAP_BUFFER_H

#include "Multi_Buffer.h"

#include <vector>

class Voice_Tap_Buffer : public Multi_Buffer {
public:
	enum { max_taps = 8 };      // voices read into rings (the others are only mixed)
	enum { ring_size = 8192 };  // samples kept per voice (a power of two)

	explicit Voice_Tap_Buffer( Multi_Buffer* inner ); // takes ownership
	~Voice_Tap_Buffer();

	// Each voice's output (1.0 = a full scale 16-bit sample; center + left + right): the ring of the
	// voice, written up to written() (index & (ring_size - 1)).
	int taps() const { return (int) voices.size() < max_taps ? (int) voices.size() : max_taps; }
	float const* ring( int voice ) const { return rings [voice]; }
	unsigned written() const { return written_; }

public:
	blargg_err_t set_channel_count( int ) override;
	channel_t channel( int index, int type ) override;
	blargg_err_t set_sample_rate( long rate, int msec = blip_default_length ) noexcept override;
	void clock_rate( uint32_t ) override;
	void bass_freq( int ) override;
	void clear() override;
	void end_frame( blip_time_t ) override;
	long read_samples( blip_sample_t*, long ) override;
	long samples_avail() const override { return inner->samples_avail(); }

private:
	struct Voice {
		Blip_Buffer bufs [3]; // center, left, right: what the voice writes during a frame
		Blip_Buffer mono;     // what the voice played (read like the mix)
		channel_t dest;       // where the wrapped buffer mixes it
	};
	Multi_Buffer* inner;
	std::vector<Voice*> voices;
	long rate_;
	int msec_;
	uint32_t clock_;
	int bass_;
	float rings [max_taps] [ring_size];
	unsigned written_;

	blargg_err_t setup( Voice& );
};

#endif
