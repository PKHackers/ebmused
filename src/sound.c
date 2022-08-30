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

static int do_envelope(struct channel_state *c, int mixing_rate) {
	if (c->note_release != 0) {
		if (c->inst_adsr1 & 0x1F)
			c->env_height *= c->decay_rate;
	} else {
		// release takes about 15ms (not dependent on tempo)
		c->env_height -= (32000 / 512.0) / mixing_rate;
		if (c->env_height < 0) {
			c->samp_pos = -1;
			// key off
			return 1;
		}
	}
	return 0;
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

			if (do_envelope(c, mixrate) == 1) {
				continue;
			}
			double volume = c->env_height / 128.0;

			int s1 = s->data[ipos];
			s1 += (s->data[ipos+1] - s1) * (c->samp_pos & 0x7FFF) >> 15;

			left  += s1 * c->left_vol  * volume;
			right += s1 * c->right_vol * volume;

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
