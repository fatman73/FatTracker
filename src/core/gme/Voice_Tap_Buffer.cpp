// FatTracker: per-voice buffers around a Multi_Buffer (see Voice_Tap_Buffer.h)

#include "Voice_Tap_Buffer.h"

#include <string.h>

#include "blargg_source.h"

Voice_Tap_Buffer::Voice_Tap_Buffer( Multi_Buffer* inner_ )
	: Multi_Buffer( 2 )
	, inner( inner_ )
	, rate_( 0 )
	, msec_( blip_default_length )
	, clock_( 0 )
	, bass_( 16 )
	, written_( 0 )
{
	memset( rings, 0, sizeof rings );
}

Voice_Tap_Buffer::~Voice_Tap_Buffer()
{
	for ( Voice* v : voices )
		delete v;
	delete inner;
}

blargg_err_t Voice_Tap_Buffer::setup( Voice& v )
{
	for ( int k = 0; k < 4; k++ )
	{
		Blip_Buffer& b = k < 3 ? v.bufs [k] : v.mono;
		if ( rate_ )
			RETURN_ERR( b.set_sample_rate( rate_, msec_ ) );
		if ( clock_ )
			b.clock_rate( clock_ );
		b.bass_freq( bass_ );
	}
	return 0;
}

blargg_err_t Voice_Tap_Buffer::set_channel_count( int n )
{
	RETURN_ERR( inner->set_channel_count( n ) );
	while ( (int) voices.size() < n )
	{
		Voice* v = BLARGG_NEW Voice;
		CHECK_ALLOC( v );
		v->dest = inner->channel( (int) voices.size(), 0 );
		voices.push_back( v );
		RETURN_ERR( setup( *v ) );
	}
	return 0;
}

Multi_Buffer::channel_t Voice_Tap_Buffer::channel( int i, int type )
{
	if ( i < 0 || i >= (int) voices.size() )
		return inner->channel( i, type ); // not tapped: straight into the mix
	Voice& v = *voices [i];
	v.dest = inner->channel( i, type );
	channel_t c;
	c.center = &v.bufs [0];
	c.left   = &v.bufs [1];
	c.right  = &v.bufs [2];
	return c;
}

blargg_err_t Voice_Tap_Buffer::set_sample_rate( long rate, int msec ) noexcept
{
	RETURN_ERR( inner->set_sample_rate( rate, msec ) );
	rate_ = rate;
	msec_ = msec;
	for ( Voice* v : voices )
		RETURN_ERR( setup( *v ) );
	return Multi_Buffer::set_sample_rate( inner->sample_rate(), inner->length() );
}

void Voice_Tap_Buffer::clock_rate( uint32_t rate )
{
	inner->clock_rate( rate );
	clock_ = rate;
	for ( Voice* v : voices )
	{
		for ( Blip_Buffer& b : v->bufs )
			b.clock_rate( rate );
		v->mono.clock_rate( rate );
	}
}

void Voice_Tap_Buffer::bass_freq( int freq )
{
	inner->bass_freq( freq );
	bass_ = freq;
	for ( Voice* v : voices )
	{
		for ( Blip_Buffer& b : v->bufs )
			b.bass_freq( freq );
		v->mono.bass_freq( freq );
	}
}

void Voice_Tap_Buffer::clear()
{
	inner->clear();
	for ( Voice* v : voices )
	{
		for ( Blip_Buffer& b : v->bufs )
			b.clear();
		v->mono.clear();
	}
	memset( rings, 0, sizeof rings );
}

void Voice_Tap_Buffer::end_frame( blip_time_t t )
{
	for ( size_t i = 0; i < voices.size(); i++ )
	{
		Voice& v = *voices [i];
		Blip_Buffer* const dest [3] = { v.dest.center, v.dest.left, v.dest.right };
		Blip_Buffer::buf_t_* const mono = i < (size_t) max_taps ? v.mono.buffer_ : 0;
		for ( int k = 0; k < 3; k++ )
		{
			Blip_Buffer& b = v.bufs [k];
			Blip_Buffer::buf_t_* const src = b.buffer_;
			Blip_Buffer::buf_t_* const dst = dest [k] ? dest [k]->buffer_ : 0;
			long const n = b.buffer_size_ + blip_buffer_extra_;
			for ( long j = 0; j < n; j++ )
			{
				Blip_Buffer::buf_t_ const d = src [j];
				if ( d )
				{
					if ( dst )
						dst [j] += d;
					if ( mono )
						mono [j] += d;
					src [j] = 0;
				}
			}
			if ( b.clear_modified() && dest [k] )
				dest [k]->set_modified();
			b.end_frame( t );
		}
		v.mono.end_frame( t );
	}
	inner->end_frame( t );
}

long Voice_Tap_Buffer::read_samples( blip_sample_t* out, long count )
{
	long const n = inner->read_samples( out, count );
	long const frames = n / 2;
	if ( frames <= 0 )
		return n;
	for ( size_t i = 0; i < voices.size(); i++ )
	{
		Voice& v = *voices [i];
		for ( Blip_Buffer& b : v.bufs )
			b.remove_silence( frames ); // (empty: the frame's deltas went to the mix)
		if ( i < (size_t) max_taps )
		{
			float* const ring = rings [i];
			unsigned pos = written_;
			int const bass = BLIP_READER_BASS( v.mono );
			BLIP_READER_BEGIN( r, v.mono );
			for ( long f = 0; f < frames; f++ )
			{
				ring [pos++ & (ring_size - 1)] = BLIP_READER_READ_RAW( r ) * (1.0f / (1 << (blip_sample_bits - 1)));
				BLIP_READER_NEXT( r, bass );
			}
			BLIP_READER_END( r, v.mono );
			v.mono.remove_samples( frames );
		}
		else
		{
			v.mono.remove_silence( frames );
		}
	}
	written_ += (unsigned) frames;
	return n;
}
