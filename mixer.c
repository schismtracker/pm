#include "pm.h"

/* --------------------------------------------------------------------------------------------------------- */
/* voice functions (low-level mixing) */

void channel_remove_voice(channel_t *channel, voice_t *voice)
{
	int n;
	
	if (!channel)
		return;
	
	if (channel->fg_voice == voice)
		channel->fg_voice = NULL;
	for (n = 0; n < channel->num_voices; n++) {
		if (channel->voices[n] == voice) {
			channel->voices[n] = channel->voices[--channel->num_voices];
			break;
		}
	}
}

void voice_stop(voice_t *voice)
{
	voice->data = NULL;
	channel_remove_voice(voice->host, voice);
}


/* This could be optimized a lot by writing separate functions for non-looping, forwards, and pingpong, 8-
and 16-bit, surround, maybe for pure left/right/center panning, etc. Essentially, instead of having one
function that does a lot, have a lot of little ones that are as short as possible, because any conditionals
will slow down the mixing. (although it's not really possible to get rid of the sample length check...) A
further optimization would be to check if the voice's volume is zero and increment the position without doing
any mixing. The non-mixing, non-looping function might even be reduced to voice->cur += (size * voice->inc)
and stopping the sample if it goes past the length. */
static void voice_get_sample(voice_t *voice, int32_t *l, int32_t *r)
{
	int32_t s;

	/* get the sample */
	if (voice->flags & SAMP_16BIT)
		s = ((int16_t *) voice->data)[voice->cur >> FRACBITS];
	else
		s = voice->data[voice->cur >> FRACBITS] << 8;
	
	/* increment the position */
	voice->cur += voice->inc;
	
	/* check loops */
	if (voice->flags & SAMP_LOOP) {
		if (voice->flags & SAMP_PINGPONG) {
			/* if we're going BACKWARDS, and the position is before the loop start, or if we're
			going FORWARDS, and the position is after the loop end, change direction */
			if ((voice->inc < 0 && voice->cur < voice->loop_start)
			    || (voice->inc > 0 && voice->cur >= voice->loop_end)) {
				voice->inc = -voice->inc;
				voice->cur += 2 * voice->inc; /* adjust for previous increment */
			}
		} else {
			if (voice->cur >= voice->loop_end)
				voice->cur = voice->loop_start | (voice->cur & FRACMASK);
		}
	} else if (voice->cur >= voice->length) {
		/* kill it */
		voice_stop(voice);
	}
	if (voice->panning == PAN_SURROUND) {
		int32_t q = (s * voice->lvol) >> 14;
		*l += q;
		*r -= q;
	} else {
		/* finally, send it back */
		*l += (s * voice->lvol) >> 14;
		*r += (s * voice->rvol) >> 14;
	}
}

void voice_process(voice_t *voice, int32_t *buffer, int size)
{
	/* don't mix muted channels (this could surely be handled less stupidly,
	but it's only a temporary hack to get it working) */
	if (voice->host->flags & CHAN_MUTE) {
		int32_t junk[2];
		while (voice->data && size) {
			voice_get_sample(voice, junk, junk + 1);
			size--;
		}
		return;
	}
	
	while (voice->data && size) {
		voice_get_sample(voice, buffer, buffer + 1);
		buffer += 2;
		size--;
	}
}


static void voice_calc_volume(voice_t *voice)
{
	if (voice->panning == PAN_SURROUND || voice->panning == 32) {
		/* this optimization appears worth it */
		voice->rvol = voice->lvol = voice->volume * voice->nfc / 32;
	} else {
		voice->lvol = voice->volume * (64 - voice->panning) * voice->nfc / 1024;
		voice->rvol = voice->volume * voice->panning * voice->nfc / 1024;
	}
}

void voice_start(voice_t *voice, sample_t *sample)
{
	voice->data = sample->data;
	voice->length = sample->length << FRACBITS;
	voice->loop_start = sample->loop_start << FRACBITS;
	voice->loop_end = sample->loop_end << FRACBITS;
	voice->flags = sample->flags;
	voice->cur = 0;
	voice->nfc = 1024;
	voice->fadeout = 0;
}

/* Bleh. REALLY wish this didn't have to take the song as a parameter, but
I don't see any other way to get the mixing rate here without saving it in
every voice or keeping a global, and those are both bogus. */
void voice_set_frequency(song_t *song, voice_t *voice, int frequency)
{
	voice->frequency = frequency;
	voice->inc = (frequency << FRACBITS) / song->mixing_rate;
}

void voice_set_volume(voice_t *voice, int volume)
{
	voice->volume = CLAMP(volume, 0, 128);
	voice_calc_volume(voice);
}

void voice_set_panning(voice_t *voice, int panning)
{
	if (panning == PAN_SURROUND)
		voice->panning = PAN_SURROUND;
	else
		voice->panning = CLAMP(panning, 0, 64);
	voice_calc_volume(voice);
}

void voice_set_position(voice_t *voice, int position)
{
	voice->cur = position << FRACBITS;
}

void voice_fade(voice_t *voice)
{
	if (voice->nfc > voice->fadeout) {
		voice->nfc -= voice->fadeout;
		voice_calc_volume(voice);
	} else {
		voice_stop(voice);
	}
}

voice_t *voice_find_free(voice_t *voices, int num_voices)
{
	voice_t *voice = voices;
	voice_t *lsv = NULL; /* least significant background voice -- used if all voices are playing */
	int n;
	
	for (n = 0; n < num_voices; n++, voice++) {
		if (voice->data == NULL)
			return voice;
		/* check if this is a background voice that's quieter than the current lsv */
		if (voice->host->fg_voice != voice && (lsv == NULL || lsv->volume > voice->volume))
			lsv = voice;
	}
	return lsv;
}
