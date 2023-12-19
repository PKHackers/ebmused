#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include "id.h"
#include "ebmusv2.h"
#include "misc.h"

// TODO: make non-const, add UI to allow changing this at runtime
static const enum InterpolationMethod {
	// The method used by the actual SNES DSP, a weighted average that filters
	// out some high frequency content
	INTERPOLATION_SNES,
	// A polynomial spline approximation that preserves endpoints and estimated
	// slopes at those endpoints, based on 4 samples' worth of data like the
	// SNES method.
	INTERPOLATION_CUBIC,
	// A straight line is drawn between every pair of points. Similar to the
	// original interpolation code used by ebmused.
	INTERPOLATION_LINEAR
} interpolation_method = INTERPOLATION_SNES;
int mixrate = 44100;
int bufsize = 2205;
int chmask = 0xFF;
int timer_speed = 500;
HWAVEOUT hwo;
static BOOL song_playing = FALSE;
FILE* wav_file = NULL;

BOOL is_playing(void) { return song_playing; }
BOOL start_playing(void) {
	if (sound_init()) {
		song_playing = TRUE;
		EnableMenuItem(hmenu, ID_PLAY, MF_GRAYED);
	}

	return song_playing;
}
void stop_playing(void) {
	stop_capturing_audio();
	song_playing = FALSE;
	EnableMenuItem(hmenu, ID_STOP, MF_GRAYED);
}

static WAVEHDR wh[2], *curbuf = &wh[0];
static int bufs_used;

int sound_init() {
	WAVEFORMATEX wfx;

	if (hwo) {
		printf("Already playing!\n");
		return 0;
	}

	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 2;
	wfx.nSamplesPerSec = mixrate;
	wfx.nAvgBytesPerSec = mixrate*4;
	wfx.nBlockAlign = 4;
	wfx.wBitsPerSample = 16;
	wfx.cbSize = sizeof wfx;

	int error = waveOutOpen(&hwo, WAVE_MAPPER, &wfx, (DWORD_PTR)hwndMain, 0, CALLBACK_WINDOW);
	if (error) {
		char buf[60];
		sprintf(buf, "waveOut device could not be opened (%d)", error);
		MessageBox2(buf, NULL, MB_ICONERROR);
		return 0;
	}

	wh[0].lpData = malloc(bufsize*4 * 2);
	wh[0].dwBufferLength = bufsize*4;
	wh[1].lpData = wh[0].lpData + bufsize*4;
	wh[1].dwBufferLength = bufsize*4;
	waveOutPrepareHeader(hwo, &wh[0], sizeof *wh);
	waveOutPrepareHeader(hwo, &wh[1], sizeof *wh);

	return 1;
}

BOOL is_capturing_audio(void) {
	return wav_file ? TRUE : FALSE;
}

BOOL start_capturing_audio(void) {
	if (song_playing || sound_init()) {
		char *file = open_dialog(GetSaveFileName, "WAV files (*.wav)\0*.wav\0", "wav", OFN_OVERWRITEPROMPT);
		if (file) {
			stop_capturing_audio();
			wav_file = fopen(file, "wb");

			if (wav_file) {
				update_menu_item(ID_CAPTURE_AUDIO, "Stop C&apturing");
				EnableMenuItem(hmenu, ID_STOP, MF_ENABLED);

				DWORD size_placeholder = 0;
				DWORD format_header_size = 16;
				WORD formatTag = WAVE_FORMAT_PCM;
				WORD num_channels = 2;
				DWORD sample_rate = mixrate;
				DWORD avg_byte_rate = mixrate*4;
				WORD block_alignment = 4;
				WORD bit_depth = 16;

				fputs("RIFF", wav_file);
				fwrite(&size_placeholder, sizeof size_placeholder, 1, wav_file);
				fputs("WAVE", wav_file);
				fputs("fmt ", wav_file);
				fwrite(&format_header_size, sizeof format_header_size, 1, wav_file);
				fwrite(&formatTag, sizeof formatTag, 1, wav_file);
				fwrite(&num_channels, sizeof num_channels, 1, wav_file);
				fwrite(&sample_rate, sizeof sample_rate, 1, wav_file);
				fwrite(&avg_byte_rate, sizeof avg_byte_rate, 1, wav_file);
				fwrite(&block_alignment, sizeof block_alignment, 1, wav_file);
				fwrite(&bit_depth, sizeof bit_depth, 1, wav_file);
				fputs("data", wav_file);
				fwrite(&size_placeholder, sizeof size_placeholder, 1, wav_file);
			}
		}
	}

	return wav_file ? TRUE : FALSE;
}

void stop_capturing_audio(void) {
	if (wav_file) {
		update_menu_item(ID_CAPTURE_AUDIO, "Capture &Audio...");

		int size = ftell(wav_file) - 8;
		fseek(wav_file, 4, SEEK_SET);
		fwrite(&size, sizeof size, 1, wav_file);

		fseek(wav_file, 40, SEEK_SET);
		size -= 36;
		fwrite(&size, sizeof size, 1, wav_file);
		fclose(wav_file);
		wav_file = NULL;
	}
}

static void sound_uninit() {
	waveOutUnprepareHeader(hwo, &wh[0], sizeof *wh);
	waveOutUnprepareHeader(hwo, &wh[1], sizeof *wh);
	waveOutClose(hwo);
	free(wh[0].lpData);
	hwo = NULL;
}

// Move the envelope forward enough SNES ticks to match one tick of ebmused.
// Returns 1 if the note was keyed off as a result of calculating the envelope, or 0 otherwise.
// Note that mixing_rate values near INT_MAX may result in undefined behavior. But no one should
// be trying to play back 2 GHz audio anyway.
static BOOL do_envelope(struct channel_state *c, int mixing_rate) {
	BOOL hit_zero = FALSE;

	// c->env_fractional_counter keeps track of accumulated error from running or
	// not running the below loop. When it gets to be high enough, we run the
	// loop again.
	// Examples:
	// If mixing_rate is 32000, this loop will run 1 time every function call.
	// If mixing_rate is 16000, this loop will run 2 times every call.
	// If mixing_rate is 64000, this loop will run 0/1 times every two calls.
	// If mixing_rate is 48000, this loop will run 0/1/1 times every three calls.
	c->env_fractional_counter += 32000;
	while (c->env_fractional_counter >= mixing_rate && !hit_zero) {
		++c->env_counter;
		c->env_fractional_counter -= mixing_rate;
		c->env_height = c->next_env_height;
		c->env_state = c->next_env_state;

		switch (c->env_state) {
		case ENV_STATE_ATTACK:
			if (c->env_counter >= c->attack_rate) {
				c->env_counter = 0;
				c->next_env_height = c->env_height + (c->attack_rate == 1 ? 1024 : 32);
				if (c->next_env_height > 0x7FF) {
					c->next_env_height = 0x7FF;
				}
				if (c->next_env_height >= 0x7E0) {
					c->next_env_state = ENV_STATE_DECAY;
				}
			}
			break;
		case ENV_STATE_DECAY:
			if (c->env_counter >= c->decay_rate) {
				c->env_counter = 0;
				c->next_env_height = c->env_height - (((c->env_height - 1) >> 8) + 1);
			}
			if (c->next_env_height <= c->sustain_level) {
				c->next_env_state = ENV_STATE_SUSTAIN;
			}
			break;
		case ENV_STATE_SUSTAIN:
			if (c->sustain_rate != 0 && c->env_counter >= c->sustain_rate) {
				c->env_counter = 0;
				c->next_env_height = c->env_height - (((c->env_height - 1) >> 8) + 1);
			}
			break;
		case ENV_STATE_KEY_OFF:
		default:
			// "if (env_counter >= 1)" should always be true, because we just incremented it
			// (1 being rates[31], the fixed rate used in the release phase)
			c->env_counter = 0;
			c->next_env_height = c->env_height - 8;
			// We want to check if the sample has *already* hit zero, not if it will next tick.
			if (c->env_height < 0) {
				c->samp_pos = -1;
				hit_zero = TRUE;
			}
			break;
		case ENV_STATE_GAIN:
			if (c->gain_rate != 0 && c->env_counter >= c->gain_rate) {
				c->env_counter = 0;
				// There is no work to do for direct gain -- the current level
				// is sustained, and c->next_env_height should already equal it
				if (c->inst_gain & 0x80) {
					switch ((c->inst_gain >> 5) & 3) {
					case 0: // Linear decrease
					case 1: // Exponential decrease
						// Unimplemented
						c->samp_pos = -1;
						hit_zero = TRUE;
						break;
					case 2: // Linear increase
						c->next_env_height = c->env_height + 32;
						break;
					case 3: // Bent increase
						c->next_env_height = (c->env_height < 0x600) ? c->env_height + 32 : c->env_height + 8;
						break;
					}

					if (c->next_env_height > 0x7FF) {
						c->next_env_height = 0x7FF;
					} else if (c->next_env_height < 0) {
						c->next_env_height = 0;
					}
				}
			}
		}
	}
	return hit_zero;
}

static short get_interpolated_sample(const short *sample_data, int inter_sample_pos) {
	// For the "accurate" "Gaussian" SNES DSP interpolation method
	static const short interpolation_table[512] = {
		0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,
		0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x002,0x002,0x002,0x002,0x002,
		0x002,0x002,0x003,0x003,0x003,0x003,0x003,0x004,0x004,0x004,0x004,0x004,0x005,0x005,0x005,0x005,
		0x006,0x006,0x006,0x006,0x007,0x007,0x007,0x008,0x008,0x008,0x009,0x009,0x009,0x00A,0x00A,0x00A,
		0x00B,0x00B,0x00B,0x00C,0x00C,0x00D,0x00D,0x00E,0x00E,0x00F,0x00F,0x00F,0x010,0x010,0x011,0x011,
		0x012,0x013,0x013,0x014,0x014,0x015,0x015,0x016,0x017,0x017,0x018,0x018,0x019,0x01A,0x01B,0x01B,
		0x01C,0x01D,0x01D,0x01E,0x01F,0x020,0x020,0x021,0x022,0x023,0x024,0x024,0x025,0x026,0x027,0x028,
		0x029,0x02A,0x02B,0x02C,0x02D,0x02E,0x02F,0x030,0x031,0x032,0x033,0x034,0x035,0x036,0x037,0x038,
		0x03A,0x03B,0x03C,0x03D,0x03E,0x040,0x041,0x042,0x043,0x045,0x046,0x047,0x049,0x04A,0x04C,0x04D,
		0x04E,0x050,0x051,0x053,0x054,0x056,0x057,0x059,0x05A,0x05C,0x05E,0x05F,0x061,0x063,0x064,0x066,
		0x068,0x06A,0x06B,0x06D,0x06F,0x071,0x073,0x075,0x076,0x078,0x07A,0x07C,0x07E,0x080,0x082,0x084,
		0x086,0x089,0x08B,0x08D,0x08F,0x091,0x093,0x096,0x098,0x09A,0x09C,0x09F,0x0A1,0x0A3,0x0A6,0x0A8,
		0x0AB,0x0AD,0x0AF,0x0B2,0x0B4,0x0B7,0x0BA,0x0BC,0x0BF,0x0C1,0x0C4,0x0C7,0x0C9,0x0CC,0x0CF,0x0D2,
		0x0D4,0x0D7,0x0DA,0x0DD,0x0E0,0x0E3,0x0E6,0x0E9,0x0EC,0x0EF,0x0F2,0x0F5,0x0F8,0x0FB,0x0FE,0x101,
		0x104,0x107,0x10B,0x10E,0x111,0x114,0x118,0x11B,0x11E,0x122,0x125,0x129,0x12C,0x130,0x133,0x137,
		0x13A,0x13E,0x141,0x145,0x148,0x14C,0x150,0x153,0x157,0x15B,0x15F,0x162,0x166,0x16A,0x16E,0x172,
		0x176,0x17A,0x17D,0x181,0x185,0x189,0x18D,0x191,0x195,0x19A,0x19E,0x1A2,0x1A6,0x1AA,0x1AE,0x1B2,
		0x1B7,0x1BB,0x1BF,0x1C3,0x1C8,0x1CC,0x1D0,0x1D5,0x1D9,0x1DD,0x1E2,0x1E6,0x1EB,0x1EF,0x1F3,0x1F8,
		0x1FC,0x201,0x205,0x20A,0x20F,0x213,0x218,0x21C,0x221,0x226,0x22A,0x22F,0x233,0x238,0x23D,0x241,
		0x246,0x24B,0x250,0x254,0x259,0x25E,0x263,0x267,0x26C,0x271,0x276,0x27B,0x280,0x284,0x289,0x28E,
		0x293,0x298,0x29D,0x2A2,0x2A6,0x2AB,0x2B0,0x2B5,0x2BA,0x2BF,0x2C4,0x2C9,0x2CE,0x2D3,0x2D8,0x2DC,
		0x2E1,0x2E6,0x2EB,0x2F0,0x2F5,0x2FA,0x2FF,0x304,0x309,0x30E,0x313,0x318,0x31D,0x322,0x326,0x32B,
		0x330,0x335,0x33A,0x33F,0x344,0x349,0x34E,0x353,0x357,0x35C,0x361,0x366,0x36B,0x370,0x374,0x379,
		0x37E,0x383,0x388,0x38C,0x391,0x396,0x39B,0x39F,0x3A4,0x3A9,0x3AD,0x3B2,0x3B7,0x3BB,0x3C0,0x3C5,
		0x3C9,0x3CE,0x3D2,0x3D7,0x3DC,0x3E0,0x3E5,0x3E9,0x3ED,0x3F2,0x3F6,0x3FB,0x3FF,0x403,0x408,0x40C,
		0x410,0x415,0x419,0x41D,0x421,0x425,0x42A,0x42E,0x432,0x436,0x43A,0x43E,0x442,0x446,0x44A,0x44E,
		0x452,0x455,0x459,0x45D,0x461,0x465,0x468,0x46C,0x470,0x473,0x477,0x47A,0x47E,0x481,0x485,0x488,
		0x48C,0x48F,0x492,0x496,0x499,0x49C,0x49F,0x4A2,0x4A6,0x4A9,0x4AC,0x4AF,0x4B2,0x4B5,0x4B7,0x4BA,
		0x4BD,0x4C0,0x4C3,0x4C5,0x4C8,0x4CB,0x4CD,0x4D0,0x4D2,0x4D5,0x4D7,0x4D9,0x4DC,0x4DE,0x4E0,0x4E3,
		0x4E5,0x4E7,0x4E9,0x4EB,0x4ED,0x4EF,0x4F1,0x4F3,0x4F5,0x4F6,0x4F8,0x4FA,0x4FB,0x4FD,0x4FF,0x500,
		0x502,0x503,0x504,0x506,0x507,0x508,0x50A,0x50B,0x50C,0x50D,0x50E,0x50F,0x510,0x511,0x511,0x512,
		0x513,0x514,0x514,0x515,0x516,0x516,0x517,0x517,0x517,0x518,0x518,0x518,0x518,0x518,0x519,0x519
	};

	int s = 0;
	switch(interpolation_method) {
	default:
		// If this happens in a debug build, catch it, because it's a mistake
		assert(0 && "unimplemented interpolation method");
		// Otherwise... might as well just use a good default
		// fallthrough
	case INTERPOLATION_SNES: {
		unsigned char i = inter_sample_pos >> (15 - 8);
		short weights[4] = {
			interpolation_table[255 - i],
			interpolation_table[511 - i],
			interpolation_table[256 + i],
			interpolation_table[  0 + i]
		};
		// Use an unsigned short to ensure wrapping behavior
		unsigned short r = 0;

		// fullsnes uses >> 10, because it treats the BRR output as 15-bit, but we decode to
		// 16-bit PCM with the bottom bit clear
		r += sample_data[0] * weights[0] >> 11;
		r += sample_data[1] * weights[1] >> 11;
		r += sample_data[2] * weights[2] >> 11;
		s = (short)r + (sample_data[3] * weights[3] >> 11);
		s =  s >  32767 ?  32767 :
		    (s < -32768 ? -32768 : s);
		}
		break;
	case INTERPOLATION_CUBIC: {
		// Create a cubic curve starting at sample_data[1] (t = 0) and ending at sample_data[2] (t = 1),
		// also specifying the slopes at both of those points using a two-sided average.
		// Then use that cubic curve to estimate the point between sample_data[1] and sample_data[2].
		double t_power[4] = {
			[0] = 1.,
			[1] = inter_sample_pos / (double)(1 << 15),
			[2] = inter_sample_pos * inter_sample_pos / (double)(1 << 30),
			[3] = (long long)inter_sample_pos * inter_sample_pos * inter_sample_pos / (double)(1LL << 45)
		};

		double start = sample_data[1];
		double end = sample_data[2];
		double start_slope = (sample_data[2] - sample_data[0]) / 2.;
		double end_slope = (sample_data[3] - sample_data[1]) / 2.;

		// We add together four cubic equations that only affect the position or slope of either
		// the starting point or the ending point to find the right cubic equation for the sample.
		// coefficients[0] is the sum of all the t^0 (constant) terms,
		// coefficients[1] is the sum of all the t^1 terms, etc.
		// The equations are:
		//  2t^3 - 3t^2 + 0t + 1, scaled by the desired starting position
		// at t = 0, this equals 1 and has a slope of 0
		// at t = 1, this equals 0 and has a slope of 0
		//  1t^3 - 2t^2 + 1t + 0, scaled by the desired starting slope
		// at t = 0, this equals 0 and has a slope of 1
		// at t = 1, this equals 0 and has a slope of 0
		// -2t^3 + 3t^2 + 0t + 0, scaled by the desired ending position
		// at t = 0, this equals 0 and has a slope of 0
		// at t = 1, this equals 1 and has a slope of 0
		//  1t^3 - 1t^2 + 0t + 0, scaled by the desired ending slope
		// at t = 0, this equals 0 and has a slope of 0
		// at t = 1, this equals 0 and has a slope of 1
		double coefficients[4] = {
			[0] =  1. * start,
			[1] =               1. * start_slope,
			[2] = -3. * start - 2. * start_slope + 3. * end - 1. * end_slope,
			[3] =  2. * start + 1. * start_slope - 2. * end + 1. * end_slope
		};

		double r = t_power[0] * coefficients[0] + t_power[1] * coefficients[1]
		         + t_power[2] * coefficients[2] + t_power[3] * coefficients[3];
		assert(isfinite(r));
		s =  r >  32767. ?  32767 :
		    (r < -32768. ? -32768 : (int)r);
		}
		break;
	case INTERPOLATION_LINEAR:
	    // Slope, or rise/run, is (sample_data[2] - sample_data[1]) / (1 - 0)
	    // y-intercept is sample_data[1]
		s = sample_data[1] + (((long long)sample_data[2] - sample_data[1]) * inter_sample_pos >> 15);
		s =  s >  32767 ?  32767 :
		    (s < -32768 ? -32768 : s);
		break;
	}

	// The interpolation result is also 15-bit, treated by us as 16-bit.
	s >>= 1;
	s *= 2;
	return (short)s;
}

//DWORD cnt;

static void fill_buffer() {
	short (*bufp)[2] = (short (*)[2])curbuf->lpData;

	if (hwndTracker != NULL)
		tracker_scrolled();

	int bytes_left = curbuf->dwBufferLength;
	while (bytes_left > 0) {
		if ((state.next_timer_tick -= timer_speed) < 0) {
			state.next_timer_tick += mixrate;
			if (!do_timer()) {
				curbuf->dwBufferLength -= bytes_left;
				break;
			}
		}

//		for (int blah = 0; blah < 50; blah++) {
		int left = 0, right = 0;
		struct channel_state *c = state.chan;
		for (int cm = chmask; cm; c++, cm >>= 1) {
			if (!(cm & 1)) continue;

			if (c->samp_pos < 0) continue;

			int ipos = c->samp_pos >> 15;

			struct sample *s = c->samp;
			if (!s) continue;
			if (ipos > s->length) {
				printf("This can't happen. %d > %d\n", ipos, s->length);
				c->samp_pos = -1;
				continue;
			}

			// Tick the envelope forward once and check if the note becomes
			// completely silent
			if (do_envelope(c, mixrate)) {
				continue;
			}

			int s1 = get_interpolated_sample(&s->data[ipos], c->samp_pos & 0x7FFF);

			// Linear interpolation between envelope ticks
			int env_height = c->env_height +
				(long long)(c->next_env_height - c->env_height) * c->env_fractional_counter / mixrate;
			left  += s1 * env_height / 0x800 * c->left_vol  / 128;
			right += s1 * env_height / 0x800 * c->right_vol / 128;

//			int sp = c->samp_pos;

			c->samp_pos += c->note_freq;
			if ((c->samp_pos >> 15) >= s->length) {
				if (s->loop_len)
					c->samp_pos -= s->loop_len << 15;
				else
					c->samp_pos = -1;
			}
//			if (blah != 1) c->samp_pos = sp;
		}
		if (left < -32768) left = -32768;
		else if (left > 32767) left = 32767;
		if (right < -32768) right = -32768;
		else if (right > 32767) right = 32767;
		(*bufp)[0] = left;
		(*bufp)[1] = right;
//		}
		bufp++;
		bytes_left -= 4;
	}
/*	{	MMTIME mmt;
		mmt.wType = TIME_BYTES;
		waveOutGetPosition(hwo, &mmt, sizeof(mmt));
		printf("%lu / %lu", mmt.u.cb + cnt, curbuf->dwBufferLength);
		for (int i = mmt.u.cb + cnt; i >= 0; i -= 500)
			putchar(219);
		putchar('\n');
	}*/
	if (wav_file) {
		fwrite(curbuf->lpData, curbuf->dwBufferLength, 1, wav_file);
	}

	waveOutWrite(hwo, curbuf, sizeof *wh);
	bufs_used++;
	curbuf = &wh[(curbuf - wh) ^ 1];
}

void winmm_message(UINT uMsg) {
	if (uMsg == MM_WOM_CLOSE)
		return;

	if (uMsg == MM_WOM_DONE) {
		bufs_used--;
//		cnt -= bufsize*4;
	}/* else
		cnt = 0;*/

	if (song_playing) {
		while (bufs_used < 2)
			fill_buffer();
	} else {
		if (bufs_used == 0)
			sound_uninit();
	}
}

BOOL CALLBACK OptionsDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_INITDIALOG:
		SetDlgItemInt(hWnd, IDC_RATE, mixrate, FALSE);
		SetDlgItemInt(hWnd, IDC_BUFSIZE, bufsize, FALSE);
		stop_playing();
		EnableMenuItem(hmenu, ID_PLAY, MF_ENABLED);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
			int new_rate, new_bufsize;
		case IDOK:
			new_rate = GetDlgItemInt(hWnd, IDC_RATE, NULL, FALSE);
			new_bufsize = GetDlgItemInt(hWnd, IDC_BUFSIZE, NULL, FALSE);
			if (new_rate < 8000) new_rate = 8000;
			if (new_rate >= 128000) new_rate = 128000;
			if (new_bufsize < new_rate/100) new_bufsize = new_rate/100;
			if (new_bufsize > new_rate) new_bufsize = new_rate;

			mixrate = new_rate;
			bufsize = new_bufsize;
			// fallthrough
		case IDCANCEL:
			EndDialog(hWnd, LOWORD(wParam));
			break;
		}
	default: return FALSE;
	}
	return TRUE;
}
