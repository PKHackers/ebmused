#include <stdio.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include "id.h"
#include "ebmusv2.h"

int mixrate = 44100;
int bufsize = 2205;
int chmask = 255;
int timer_speed = 500;
HWAVEOUT hwo;
BOOL song_playing;

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

			// Linear interpolation between audio samples
			int s1 = s->data[ipos];
			s1 += (s->data[ipos+1] - s1) * (c->samp_pos & 0x7FFF) >> 15;

			// Linear interpolation between envelope ticks
			int env_height = c->env_height +
				(c->next_env_height - c->env_height) * c->env_fractional_counter / mixrate;
			left  += s1 * c->env_height / 0x800 * c->left_vol  / 128;
			right += s1 * c->env_height / 0x800 * c->right_vol / 128;

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
		song_playing = FALSE;
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
