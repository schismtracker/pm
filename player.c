#include "pm.h"

/* --------------------------------------------------------------------------------------------------------- */
/* playback */

void song_set_tick_timer(song_t *song)
{
	//song->samples_per_tick = song->mixing_rate / (2 * song->tempo / 5);
	song->samples_per_tick = (song->mixing_rate * 5) / (song->tempo * 2);
}

void song_set_order(song_t *song, int order, int row)
{
	song->process_row = PROCESS_NEXT_ORDER;
	song->process_order = order - 1;
	song->break_row = row;
	song->tick = 1;
	song->row_counter = 1;
	song_set_tick_timer(song);
}

void song_reset_play_state(song_t *song)
{
	int n;

	song->tempo = song->initial_tempo;
	song->global_volume = song->initial_global_volume;
	song->speed = song->initial_speed;
	song->samples_left = 0; /* start of the first tick */
	song_set_order(song, 0, 0); /* this sets up the process_* and tick variables */
	song->max_voices = 0;

	for (n = 0; n < MAX_CHANNELS; n++) {
		song->channels[n].channel_volume = song->channels[n].initial_channel_volume;
		song->channels[n].panning = song->channels[n].initial_panning;
		song->channels[n].nna_note = NOTE_CUT;
		song->channels[n].delay = 0;
		song->channels[n].last_tempo = song->tempo;
	}
	
	for (n = 0; n < MAX_VOICES; n++)
		voice_stop(song->voices + n);
}

/* --------------------------------------------------------------------------------------------------------- */

void channel_past_note_nna(song_t *song, channel_t *channel, int nna)
{
	voice_t *v;
	int n;
	if (song->flags & SONG_INSTRUMENT_MODE) {
		if (nna == NNA_CUT) {
			for (n = 0; n < channel->num_voices; n++) {
				voice_stop(channel->voices[n]);
			}
		} else if (nna == NNA_FADE) {
			for (n = 0; n < channel->num_voices; n++) {
				v = channel->voices[n];
				if (v->inst_bg) {
					v->fadeout = v->inst_bg->fadeout << 2;
				}
			}
		} else if (nna == NNA_OFF) {
			TODO("PAST NOTE OFF");
		}
	}
}
void channel_note_nna(song_t *song, channel_t *channel, note_t *note)
{
	instrument_t *i;
	int nna;

	if (!(song->flags & SONG_INSTRUMENT_MODE)) {
		channel_note_cut(channel);
	} else if (channel->fg_voice) {
		i = &song->instruments[ channel->instrument ];

		if (note) {
			if (note->note == NOTE_CUT) {
				nna = NNA_CUT;
			} else if (note->note == NOTE_OFF) {
				nna = i->nna;
			} else if (NOTE_IS_FADE(note->note)) {
				nna = NNA_FADE;
			} else if (i->dct == DCT_NOTE) { 
				if (note->note == channel->nna_note) {
					nna = i->dca;
				} else {
					nna = i->nna;
				}
			} else if (i->dct == DCT_INSTRUMENT) { 
				if (note->instrument == channel->instrument) {
					nna = i->dca;
				} else {
					nna = i->nna;
				}

			} else if (i->dct == DCT_SAMPLE) { 
				if (song->instruments[note->instrument].sample_map[note->note] == i->sample_map[channel->nna_note]) {
					nna = i->dca;
				} else {
					nna = i->nna;
				}
			} else {
				nna = i->nna;
			}
		} else {
			nna = i->nna;
		}

		if (nna == NNA_CUT) {
			voice_stop(channel->fg_voice);
		} else {
			voice_t *v;

			v = channel->fg_voice;
			v->inst_bg = i;
			if (nna == NNA_FADE || nna == NNA_OFF) {
				v->fadeout = i->fadeout << 2;
			}
		}
		channel->fg_voice = 0;
	}
}

void channel_note_cut(channel_t *channel)
{
	if (channel->fg_voice)
		voice_stop(channel->fg_voice);
}

void channel_set_global_volume(channel_t *channel, int sampvol, int instvol)
{
	channel->sample_volume = sampvol;
	channel->instrument_volume = instvol;
}

int channel_get_volume(channel_t *c)
{
	int vol;
	vol = c->volume;
	if (!vol) return 0;
	if (c->channel_volume != 64) {
		vol *= c->channel_volume;
		vol >>= 5;
		if (!vol) return 0;
	}
	if (c->instrument_volume != 128) {
		vol *= c->instrument_volume;
		vol >>= 6;
		if (!vol) return 0;
	}
	if (c->sample_volume != 128) {
		vol *= c->sample_volume;
		vol >>= 6;
		if (!vol) return 0;
	}

	return vol;
}

void channel_set_volume(channel_t *channel, int volume)
{
	channel->volume = volume;
	if (channel->fg_voice)
		voice_set_volume(channel->fg_voice, channel_get_volume(channel));
}

void channel_set_channel_volume(channel_t *channel, int volume)
{
	int n;
	
	channel->channel_volume = volume;
	/* need to update volume for ALL voices on the channel */
	for (n = 0; n < channel->num_voices; n++)
		voice_set_volume(channel->voices[n], channel_get_volume(channel));
}

void channel_set_panning(channel_t *channel, int panning)
{
	channel->panning = panning;
	if (channel->fg_voice)
		voice_set_panning(channel->fg_voice, channel->panning);
}

void channel_set_period(song_t *song, channel_t *channel, int period)
{
	channel->period = period;
	if (channel->fg_voice)
		voice_set_frequency(song, channel->fg_voice, period_to_frequency(period));
}

void channel_link_voice(channel_t *channel, voice_t *voice)
{
	channel->fg_voice = voice;
	channel->voices[channel->num_voices++] = voice;
	voice->host = channel;
}


void process_note(song_t *song, channel_t *channel, note_t *note)
{
	voice_t *voice;
	sample_t *sample = NULL;
	
	if (note->instrument) {
		/* if the instrument changed, restart it from the beginning
		(this *should* trigger a new voice if there's nothing playing in the channel,
		which would require saving the last used note) */
		if (channel->instrument != note->instrument) {
			if (channel->fg_voice)
				voice_set_position(channel->fg_voice, 0);
			/* remember the number */
			channel->instrument = note->instrument;
		}
	}
	
	if (note->note == NOTE_CUT) {
		channel_note_cut(channel);
	} else if (note->note == NOTE_OFF) {
		channel_note_nna(song, channel, note);
	} else if (note->note <= NOTE_LAST) {
		int noteval, period;
		
		if (!channel->instrument) {
			/* darn, nothing to play */
			return;
		}
		
		if (song->flags & SONG_INSTRUMENT_MODE) {
			int n = song->instruments[channel->instrument].sample_map[note->note];
			noteval = song->instruments[channel->instrument].note_map[note->note];
			
			if (n && noteval <= NOTE_LAST) {
				sample = song->samples + n;
			} else {
				/* no sample or note? well then let's just give up and not care */
				return;
			}
		} else {
			sample = song->samples + channel->instrument;
			noteval = note->note;
		}
		
		if (!sample->data) {
			/* undefined sample -> note cut (this should be up above, really) */
			printf("no data!\n");
			channel_note_cut(channel);
			return;
		}

		period = note_to_period(noteval, sample->c5speed);
		channel->c5speed = sample->c5speed;
		channel->realnote = noteval;

		/* FIXME: this is wrong - Gxx with no prior note should operate as if Gxx wasn't there.
		I'm handling this for now by setting the target period for ALL notes played, and kicking off
		a new voice if nothing is playing, but this really shouldn't be checking the effect at all
		here. Probably the best way to handle this would be to handle tick0 effects from within this
		function, have it return 1 if a new note should be triggered, and 0 if not. (This will also
		make note delay much easier to implement, since it just has to say "no, don't play this note
		yet", and then actually start the voice and stuff in the tickN handler.) */
		if (channel->fg_voice && (note->effect == 'G' || note->effect == 'L')) {
			/* slide to note; will be handled later */
			channel->target_period = period;
			
			/* TODO: if the sample changed, do the funky chicken dance */
		} else if (channel->instrument) {
			/* start playing the note */
			
			channel->target_period = period; /* hack, see above */
			
			channel_note_nna(song, channel, note);
			
			/* replace MAX_VOICES here with the set number of mixing channels. */
			voice = voice_find_free(song->voices, MAX_VOICES);
			if (voice) {
				channel_link_voice(channel, voice);
				voice_start(voice, sample);
				channel_set_period(song, channel, period);
			}
			channel_set_global_volume(channel,
				sample->global_volume << 1,
				song->flags & SONG_INSTRUMENT_MODE
				? song->instruments[note->instrument].global_volume
				: 128);
			channel_set_volume(channel, sample->volume);
			channel_set_panning(channel, channel->panning);
		}
	}

	channel->nna_note = note->note;
}

static void fx_volume_slide(channel_t *channel, int amount)
{
	amount += channel->volume;
	channel_set_volume(channel, CLAMP(amount, 0, 64));
}

static void fx_channel_volume_slide(channel_t *channel, int amount)
{
	amount += channel->channel_volume;
	channel_set_channel_volume(channel, CLAMP(amount, 0, 64));
}

static void fx_tone_portamento(song_t *song, channel_t *channel)
{
	/* logic for this effect borrowed from mikmod */
	int period = channel->period;
	int distance = period - channel->target_period;
	int g = 4 * channel->portamento;
	
	if (!distance) {
		/* stop sliding, dang it :) */
		return;
	} else if (g > abs(distance)) {
		/* just set it */
		period = channel->target_period;
	} else if (distance > 0) {
		/* it's too high, so subtract. */
		period -= g;
	} else {
		/* must be too low. add. */
		period += g;
	}
	channel_set_period(song, channel, period);
}

#define SPLIT_PARAM(param, px, py) ({px = param >> 4; py = param & 0xf;})

static void process_direct_effect_tick0(song_t *song, channel_t *channel,
				int effect, int param)
{
	int px, py;
	switch (effect) {
	case 'D': /* volume slide */
	case 'K':
	case 'L':
		if (param)
			channel->volume_slide = param;
		SPLIT_PARAM(channel->volume_slide, px, py);
		if (py == 0) {
			/* Dx0 (only slide on tick 0 if x == 0xf) */
			if (px == 0xf)
				fx_volume_slide(channel, 15);
		} else if (px == 0) {
			/* D0x (only slide on tick 0 if x == 0xf) */
			if (py == 0xf)
				fx_volume_slide(channel, -15);
		} else if (py == 0xf) {
			/* DxF */
			fx_volume_slide(channel, px);
		} else if (px == 0xf) {
			/* DFx */
			fx_volume_slide(channel, -py);
		}
		break;
	case 'E': /* pitch slide down */
		if (param) {
			channel->pitch_slide = param;
			if (!(song->flags & SONG_COMPAT_GXX))
				channel->portamento = param;
		}
		switch (channel->pitch_slide >> 4) {
		case 0xe:
			channel_set_period(song, channel, channel->period + (channel->pitch_slide & 0xf));
			channel->flags |= CHAN_FINE_SLIDE;
			break;
		case 0xf:
			channel_set_period(song, channel, channel->period + 4 * (channel->pitch_slide & 0xf));
			channel->flags |= CHAN_FINE_SLIDE;
			break;
		default:
			channel->flags &= ~CHAN_FINE_SLIDE;
		}
		break;
	case 'F': /* pitch slide up */
		if (param) {
			channel->pitch_slide = param;
			if (!(song->flags & SONG_COMPAT_GXX))
				channel->portamento = param;
		}
		switch (channel->pitch_slide >> 4) {
		case 0xe:
			channel_set_period(song, channel, channel->period - (channel->pitch_slide & 0xf));
			channel->flags |= CHAN_FINE_SLIDE;
			break;
		case 0xf:
			channel_set_period(song, channel, channel->period - 4 * (channel->pitch_slide & 0xf));
			channel->flags |= CHAN_FINE_SLIDE;
			break;
		default:
			channel->flags &= ~CHAN_FINE_SLIDE;
		}
		break;
	case 'G': /* pitch slide to note */
		if (param) {
			channel->portamento = param;
			if (!(song->flags & SONG_COMPAT_GXX)) {
				channel->pitch_slide = param;
				if ((param >> 4) >= 0xe)
					channel->flags |= CHAN_FINE_SLIDE;
				else
					channel->flags &= ~CHAN_FINE_SLIDE;
			}
		}
		break;
	};
}

void process_volume_tick0(song_t *song, channel_t *channel, note_t *note)
{
	switch (note->volume) {
	case VOL_NONE:
		return;
	case (0)...(64):
		channel_set_volume(channel, note->volume);
		break;
	case (128)...(192):
		channel_set_panning(channel, note->volume - 128);
		break;

	case (65)...(74):
		/* fine volume slide up; d?f */
		process_direct_effect_tick0(song, channel, 'D',
				((note->volume - 65) << 4) | 0x0f);
		break;
	case (75)...(84):
		/* fine volume slide down df? */
		process_direct_effect_tick0(song, channel, 'D',
				(note->volume - 75) | 0xf0);
		break;
	case (85)...(94):
		/* volume slide up d?0 */
		process_direct_effect_tick0(song, channel, 'D',
				((note->volume - 85) << 4));
		break;
	case (95)...(104):
		/* volume slide down d0? */
		process_direct_effect_tick0(song, channel, 'D',
				((note->volume - 95)));
		break;
	case (105)...(114):
		/* pitch slide down e?? value*4 */
		process_direct_effect_tick0(song, channel, 'E',
				(note->volume - 105) << 2);
		break;
	case (115)...(124):
		/* pitch slide up f?? */
		process_direct_effect_tick0(song, channel, 'F',
				(note->volume - 115) << 2);
		break;
	case (193)...(202):
		/* portamento to GX_SLIDE_TABLE */
		process_direct_effect_tick0(song, channel, 'G',
				GX_SLIDE_TABLE[ note->volume - 193 ]);
		break;

	case (203)...(212):
		/* vibrato  */

	default:
		TODO("volume column effect %d", note->volume);
	}
}

void process_effects_tick0(song_t *song, channel_t *channel, note_t *note)
{
	int effect = note->effect, param = note->param, px, py;

	/* volume column */
	process_volume_tick0(song, channel, note);
	
	/* effects */
	switch (effect) {
	case 0: /* nothing */
		break;
	case 'A': /* set speed */
		if (param)
			song->speed = param;
		song->tick = song->speed;
		break;
	case 'B': /* pattern jump */
		/* if song looping is disabled, only change the order if it's beyond the current position */
		if (song->flags & SONG_LOOP || param > song->cur_order)
			song->process_order = param - 1;
		song->process_row = PROCESS_NEXT_ORDER;
		break;
	case 'C': /* pattern break */
		SPLIT_PARAM(param, px, py);
		song->process_row = PROCESS_NEXT_ORDER;
		song->break_row = px * 10 + py;;
		break;

	case 'J': /* arpeggio */
		SPLIT_PARAM(param, px, py);
		if (py || px) {
			channel->arp_mid = note_to_period(channel->realnote,
							channel->c5speed);
		}
		if (py) {
			channel->arp_low = note_to_period(channel->realnote
							- py, channel->c5speed);
		}
		if (px) {
			channel->arp_high = note_to_period(channel->realnote
							+ px, channel->c5speed);
		}
		if (channel->fg_voice)
			voice_set_position(channel->fg_voice, 0);
		break;

	case 'K': /* vol slide + continue vibrato */
	case 'L': /* vol slide + continue pitch slide */
	case 'D': /* volume slide */

	case 'E': /* pitch slide down */
	case 'F': /* pitch slide up */
	case 'G': /* pitch slide to note */
		process_direct_effect_tick0(song, channel, effect, param);
		break;

	case 'M': /* set channel volume */
		if (param <= 64)
			channel_set_channel_volume(channel, param);
		break;
	case 'N': /* channel volume slide */
		if (param)
			channel->channel_volume_slide = param;
		SPLIT_PARAM(channel->channel_volume_slide, px, py);
		if (py == 0) {
			/* Nx0 (only slide on tick 0 if x == 0xf) */
			if (px == 0xf)
				fx_channel_volume_slide(channel, 15);
		} else if (px == 0) {
			/* N0x (only slide on tick 0 if y == 0xf) */
			if (py == 0xf)
				fx_channel_volume_slide(channel, -15);
		} else if (py == 0xf) {
			/* NxF */
			fx_channel_volume_slide(channel, px);
		} else if (px == 0xf) {
			/* NFx */
			fx_channel_volume_slide(channel, -py);
		}
		break;
	case 'O': /* offset */
		if (note->note <= NOTE_LAST) {
			if (param)
				channel->offset = param << 8;
			if (channel->fg_voice)
				voice_set_position(channel->fg_voice, channel->offset);
		}
		break;
	case 'S': /* special */
		switch (param >> 4) {
		case 0x6:
			/* haven't tested this much, but it should work ok */
			song->tick += param & 0xf;
			break;

		case 0x7:
			switch (param & 0xf) {
			case 0: /* past note cut */
				channel_past_note_nna(song, channel, NNA_CUT);
				break;
			case 1:
				channel_past_note_nna(song, channel, NNA_OFF);
				break;
			case 2:
				channel_past_note_nna(song, channel, NNA_FADE);
				break;
			};
			break;

		case 0x8:
			channel_set_panning(channel, SHORT_PANNING[param & 0xf]);
			break;
		case 0x9:
			/* nothing but surround processor */
			channel_set_panning(channel, PAN_SURROUND);
			break;

		case 0xa:
			/* high order offset */
			channel->offset &= 0xF0000;
			channel->offset |= (param & 0x0f) << 16;
			if (channel->fg_voice)
				voice_set_position(channel->fg_voice, channel->offset);
			break;
		case 0xb: /* pattern loop */
			if (param & 0xf) {
				/* SBx: loop to point */
				/* I can't believe the amount of time I put into this effect. It finally
				seems to handle the effect the same way Impulse Tracker does, but I wouldn't
				be surprised if there are still some obscure bugs. Note: AFAIK, Bass is the
				*only* player besides Impulse Tracker that plays this effect 100% accurately.
				Mikmod gets some weird cases wrong, and I won't even get into how badly
				Modplug screws it up. */
				if (channel->loop_count)
					channel->loop_count--;
				else
					channel->loop_count = param & 0xf;
				if (channel->loop_count)
					song->process_row = channel->loop_row - 1;
				else
					channel->loop_row = song->cur_row + 1;
			} else {
				/* SB0: set loopback point */
				channel->loop_row = song->cur_row;
			}
			break;
		case 'd':
			/* sadly; handled elsewhere. search for: SDx */
			break;
		default:
			TODO("effect S%02X", param);
		}
		break;
	case 'T': /* set tempo / tempo slide */
		if (param)
			channel->last_tempo = param;
		else
			param = channel->last_tempo;
		if (param > 0x1f) {
			/* set tempo */
			song->tempo = param;
			song_set_tick_timer(song);
		}
		break;
	case 'V': /* set global volume */
		TODO("GLOBAL VOVLUME SLIDE %d", note->param);
		if (note->param <= 0x80) {
			song->global_volume = note->param;
		}
		break;

	case 'W': /* global volume slide */
		TODO("GLOBAL VOVLUME SLIDE ");
		break;
	case 'X': /* set panning */
		/* Panning values are 0..64 internally, so convert the value to the proper range */
		channel_set_panning(channel, param * 64 / 255);
		break;
	default: /* unhandled effect */
		TODO("effect %c%02X", effect, param);
		break;
	}
}

static void process_direct_effect_tickN(song_t *song, channel_t *channel,
				int effect)
{
	int px, py;
	switch (effect) {
	case 'D': /* volume slide */
		SPLIT_PARAM(channel->volume_slide, px, py);
		if (py == 0) {
			fx_volume_slide(channel, px);
		} else if (px == 0) {
			fx_volume_slide(channel, -py);
		}
		break;
	case 'E':      /* pitch slide down */
		if (!(channel->flags & CHAN_FINE_SLIDE))
			channel_set_period(song, channel, channel->period + 4 * (channel->pitch_slide));
		break;
	case 'F': /* pitch slide up */
		if (!(channel->flags & CHAN_FINE_SLIDE))
			channel_set_period(song, channel, channel->period - 4 * (channel->pitch_slide));
		break;
	case 'G': /* pitch slide to note */
		fx_tone_portamento(song, channel);
		break;
	};
}

void process_volume_tickN(song_t *song, channel_t *channel, note_t *note)
{
	switch (note->volume) {
	case VOL_NONE:
		return;
	case (0)...(64):
		break;
	case (128)...(192):
		break;

	case (65)...(74):
	case (75)...(84):
	case (85)...(94):
	case (95)...(104):
		/* D commands */
		process_direct_effect_tickN(song, channel, 'D');
		break;
	case (105)...(114):
		/* pitch slide down e?? value*4 */
		process_direct_effect_tickN(song, channel, 'E');
		break;
	case (115)...(124):
		/* pitch slide up f?? */
		process_direct_effect_tickN(song, channel, 'F');
		break;
	case (193)...(202):
		/* portamento to GX_SLIDE_TABLE */
		process_direct_effect_tickN(song, channel, 'G');
		break;

	case (203)...(212):
		/* vibrato  */
		process_direct_effect_tickN(song, channel, 'H');
		break;

	default:
		break;
	}
}

void process_effects_tickN(song_t *song, channel_t *channel, note_t *note)
{
	/* This could be done a lot more cleanly by setting flags for each effect on the channel:
	for D/K/L set the volume slide flag, for H/K set the vibrato flag, etc. */
	
	/* hm. probably shouldn't be using note->param on tickN, especially
	for effects that remember their parameters */
	int effect = note->effect, px, py;
	
	/* volume column */
	process_volume_tickN(song, channel, note);
	
	/* effects */
	switch (effect) {
	case 0:
		break;
	case 'L': /* vol slide + continue pitch slide */
		fx_tone_portamento(song, channel);
		/* fall through */
	case 'D': /* volume slide */
	case 'K': /* vol slide + continue vibrato */
		process_direct_effect_tickN(song, channel, 'D');
		if (effect == 'D') break;
	case 'H': /* H & K vibrato */

		break;
	case 'E':      /* pitch slide down */
	case 'F': /* pitch slide up */
	case 'G': /* pitch slide to note */
		process_direct_effect_tickN(song, channel, effect);
		break;
	case 'J': /* arpeggio */
		if (channel->period < channel->arp_mid) {
			channel_set_period(song, channel, channel->arp_mid);
		} else if (channel->period > channel->arp_mid) {
			channel_set_period(song, channel, channel->arp_low);
		} else {
			channel_set_period(song, channel, channel->arp_high);
		}
		break;
	case 'N': /* channel volume slide */
		SPLIT_PARAM(channel->channel_volume_slide, px, py);
		if (py == 0) {
			fx_channel_volume_slide(channel, px);
		} else if (px == 0) {
			fx_channel_volume_slide(channel, -py);
		}
		break;
	case 'T':
		SPLIT_PARAM(channel->last_tempo, px, py);
		switch (px) {
		case 0:
			song->tempo -= py;
			if (song->tempo < 32)
				song->tempo = 32;
			song_set_tick_timer(song);
			break;
		case 1:
			if (song->tempo + py >= 255)
				song->tempo = 255;
			song->tempo += py;
			song_set_tick_timer(song);
		}
		break;
	default: /* unhandled effect */
		break;
	}
}

int increment_row(song_t *song)
{
	pattern_t *pattern;
	
	song->process_row = song->break_row; /* [ProcessRow = BreakRow] */
	song->break_row = 0;                 /* [BreakRow = 0] */
	song->process_order++;               /* [Increase ProcessOrder] */
	
	/* [while Order[ProcessOrder] = 0xFEh, increase ProcessOrder] */
	while (song->orderlist[song->process_order] == ORDER_SKIP)
		song->process_order++;
	/* [if Order[ProcessOrder] = 0xFFh, ProcessOrder = 0] (... or just stop playing) */
	if (song->orderlist[song->process_order] == ORDER_LAST) {
		if (song->flags & SONG_LOOP) {
			/* Actually, this will break if the first order is a skip, but I
			don't care about that yet. (IIRC, Impulse Tracker has a similar
			bug with weird stuff in the orderlist...) */
			song->process_order = 0;
		} else {
			return 0;
		}
	}
	
	/* [CurrentPattern = Order[ProcessOrder]] */
	song->cur_order = song->process_order;
	song->cur_pattern = song->orderlist[song->cur_order];
	pattern = pattern_get(song, song->cur_pattern);
	song->pattern_data = pattern->data;
	song->pattern_rows = pattern->rows;
	
	return 1;
}

void handle_fadeouts(song_t *song)
{
	int n;
	voice_t *voice;
	
	for (n = 0, voice = song->voices; n < MAX_VOICES; n++, voice++)
		if (voice->data && voice->fadeout) {
			voice_fade(voice);
		}
}

int process_tick(song_t *song)
{
	/* [Bracketed comments] are straight from the processing flowchart in ittech.txt. */
	
	int n, is_tick0 = 0;
	channel_t *channel;
	note_t *note;
	
	/* [Decrease tick counter. Is tick counter 0?] */
	if (--song->tick == 0) {
		/* [-- Yes --] */
		
		/* [Tick counter = Tick counter set (the current 'speed')] */
		song->tick = song->speed;
		
		/* [Decrease row counter. Is row counter 0?] */
		if (--song->row_counter == 0) {
			/* [-- Yes --] */
			
			song->row_counter = 1; /* [Row counter = 1] */
			
			/* [Increase ProcessRow. Is ProcessRow > NumberOfRows?] */
			if (++song->process_row >= song->pattern_rows) {
				/* [-- Yes --] */
				if (!increment_row(song))
					return 0;
			}
			
			/* check for pattern breaks to rows beyond the end of the next pattern */
			if (song->process_row >= song->pattern_rows)
				song->process_row = 0;
			
			song->cur_row = song->process_row; /* [CurrentRow = ProcessRow] */
			
			/* [Update pattern variables] */
			song->row_data = PATTERN_GET_ROW(song->pattern_data, song->cur_row);
			
			/* This should only happen on the console, not in a tracker / GUI player / whatever.
			Theoretically, the player shouldn't print anything (as it's essentially a library)
			and this function would be best handled as a callback. */
			print_row(song);
			
			is_tick0 = 1;
		} /* else: [-- No -- Call update-effects for each channel.] */

	}/* else: [-- No -- Update effects for each channel as required.] */
	
	if (is_tick0) {
		for (n = 0, note = song->row_data, channel = song->channels;
		     n < MAX_CHANNELS; n++, channel++, note++) {
			if (note->effect == 'S' /* SDx */
			&& (note->param & 0xF0) == 0xD0) {
				channel->delay = note->param & 0x0F;
			} else {
				process_note(song, channel, note);
				process_effects_tick0(song, channel, note);
			}
		}
	} else {
		for (n = 0, note = song->row_data, channel = song->channels;
		     n < MAX_CHANNELS; n++, channel++, note++) {
			if (channel->delay > 0) { /* SDx */
				channel->delay--;
				if (channel->delay == 0) {
					process_note(song, channel, note);
					process_effects_tick0(song, channel, note);
				}
				continue;
			}
			process_effects_tickN(song, channel, note);
		}
	}
	
	/* [Instrument mode?] */
	
	/* IT only handles this in instrument mode */
	if (song->flags & SONG_INSTRUMENT_MODE) {
		handle_fadeouts(song);
	}
	
	return 1;
}

/* --------------------------------------------------------------------------------------------------------- */
/* mixing and output */

static void convert_16ss(song_t *song, char *buffer, const int32_t *from, int samples)
{
	int16_t *to = (int16_t *) buffer;
	int32_t s;
	
	/* while (samples--) results in decl %edi; cmpl $-1, %edi; je; but when
	explicitly comparing against zero, it just uses testl. go figure. */
	while (samples-- > 0) {
/* TODO */
		if (0 && song->global_volume < 0x80) {
			s = *from++;
			s = ((long)s * song->global_volume) >> 0;
			*to++ = CLAMP(s, -32768, 32767);

			s = *from++;
			s = ((long)s * song->global_volume) >> 0;
			*to++ = CLAMP(s, -32768, 32767);
		} else {
			s = *from++; *to++ = CLAMP(s, -32768, 32767);
			s = *from++; *to++ = CLAMP(s, -32768, 32767);
		}
	}
}

int song_read(song_t *song, char *buffer, int buffer_size)
{
	/* Using the stack for the buffer is faster than allocating a pointer in the song_t structure. */
	int32_t mixing_buffer[2 * MIXING_BUFFER_SIZE]; /* 2x = enough room to handle stereo */
	int bytes_per_sample = 4; /* number of output channels * (bit rate / 8) */
	int buffer_left = buffer_size / bytes_per_sample;
	int bytes_left = buffer_size;
	int n, samples_to_mix;
	voice_t *voice;
	
	while (buffer_left) {
		/* at the start of a tick? */
		if (song->samples_left == 0) {
			if (!process_tick(song))
				return 0;
			song->samples_left = song->samples_per_tick;
		}
		
		/* how much room is there? */
		samples_to_mix = buffer_left;
		if (samples_to_mix > song->samples_left)
			samples_to_mix = song->samples_left;
		if (samples_to_mix > MIXING_BUFFER_SIZE)
			samples_to_mix = MIXING_BUFFER_SIZE;
		
		/* now do the actual mixing */
		memset(mixing_buffer, 0, 2 * samples_to_mix * sizeof(mixing_buffer[0]));
		
		song->num_voices = 0;
		for (n = 0, voice = song->voices; n < MAX_VOICES; n++, voice++) {
			if (voice->data) {
				voice_process(voice, mixing_buffer, samples_to_mix);
				song->num_voices++;
			}
		}
		if (song->num_voices > song->max_voices)
			song->max_voices = song->num_voices;
		
		/* copy it to the output buffer */
		convert_16ss(song, buffer, mixing_buffer, samples_to_mix);
		
		/* update the counters */
		song->samples_left -= samples_to_mix;
		buffer_left -= samples_to_mix;
		bytes_left -= samples_to_mix * bytes_per_sample;
		buffer += samples_to_mix * bytes_per_sample;
	}
	return buffer_size - bytes_left;
}
