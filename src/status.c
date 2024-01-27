#include <stdio.h> // Must be included before ebmusv2.h

#include "ebmusv2.h"
#include <assert.h>
#include <commctrl.h>
#include <stdarg.h>
#include <stdlib.h>

void format_status(int part, const char* format, ...) {
	if (hwndStatus) {
		va_list args;
		va_start(args, format);
		int size = vsnprintf(0, 0, format, args);
		va_end(args);

		char *buf = malloc(size + 1);
		va_start(args, format);
		vsprintf(buf, format, args);
		va_end(args);

		SendMessage(hwndStatus, SB_SETTEXT, part, (LPARAM)buf);

		free(buf);
	}
}

static const char* notes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

void set_tracker_status(int p, BYTE *code) {
	if (code[0] == 0x00) {
		format_status(p, "End of pattern/subroutine");
	} else if (code[0] < 0x80) {
		if (code[1] < 0x80) {
			format_status(p, "Note length: %d, release: %d/7, velocity: %d/15", code[0], (code[1] >> 4) & 0x7, code[1] & 0xF);
		} else {
			format_status(p, "Note length: %d", code[0]);
		}
	} else if (code[0] < 0xC8) {
		BYTE note = (code[0] & 0x7F);
		format_status(p, "Note %s%c", notes[note % 12], '1' + note / 12);
	} else if (code[0] == 0xC8) {
		format_status(p, "Hold");
	} else if (code[0] == 0xC9) {
		format_status(p, "Rest");
	} else if (code[0] < 0xE0) {
		format_status(p, "Percussion %d", code[0] - 0xC9);
	} else {
		switch (code[0]) {
			case 0xE0:
				format_status(p,
					code[1] < 0xCA ? "Instrument %d" : "Instrument %d (Percussion instrument %d)",
					code[1],
					code[1] - 0xCA);
				break;
			case 0xE1:
				format_status(p, "Pan %d%s%s",
					code[1] & 0x3F,
					code[1] & 0x80 ? " (left inverted)" : "",
					code[1] & 0x40 ? " (right inverted)" : "");
				break;
			case 0xE2: format_status(p, "Pan (duration: %d, pan: %d)", code[1], code[2]); break;
			case 0xE3:
				format_status(p, "Enable vibrato (delay: %d, freq: %d, range: \xB1%d%s)",
					code[1],
					code[2],
					code[3] <= 0xF0 ? code[3] : code[3] - 0xF0,
					code[3] <= 0xF0 ? "/256 semitones" : " semitones");
				break;
			case 0xE4: format_status(p, "Disable vibrato"); break;
			case 0xE5: format_status(p, "Master volume %d/255", code[1]); break;
			case 0xE6: format_status(p, "Master volume (duration: %d, volume: %d/255)", code[1], code[2]); break;
			case 0xE7: format_status(p, "Tempo %d", code[1]); break;
			case 0xE8: format_status(p, "Tempo (duration: %d, tempo: %d)", code[1], code[2]); break;
			case 0xE9: format_status(p, "Global transpose %+d semitones", (signed char)code[1]); break;
			case 0xEA: format_status(p, "Channel transpose %+d semitones", (signed char)code[1]); break;
			case 0xEB: format_status(p, "Enable tremolo (delay: %d, freq: %d, amp: %d)", code[1], code[2], code[3]); break;
			case 0xEC: format_status(p, "Disable tremolo"); break;
			case 0xED: format_status(p, "Channel volume %d/255", code[1]); break;
			case 0xEE: format_status(p, "Channel volume (duration: %d, volume: %d/255)", code[1], code[2]); break;
			case 0xEF: format_status(p, "Call subroutine %d, %d %s", code[1] | code[2] << 8, code[3], code[3] == 1 ? "time" : "times"); break;
			case 0xF0: format_status(p, "Set vibrato attack %d", code[1]); break;
			case 0xF1: format_status(p, "Enable portamento (delay: %d, duration: %d, end note: %+d)", code[1], code[2], (signed char)code[3]); break;
			case 0xF2: format_status(p, "Enable portamento (delay: %d, duration: %d, start note: %+d)", code[1], code[2], -(signed char)code[3]); break;
			case 0xF3: format_status(p, "Disable portamento"); break;
			case 0xF4: format_status(p, "Finetune %d/256 semitones", code[1]); break;
			case 0xF5:
				format_status(p, "Enable echo (channels: %c%c%c%c%c%c%c%c, left volume: %d/%s, right volume: %d/%s)",
					'0' + (code[1] & 1),
					'0' + ((code[1] >> 1) & 1),
					'0' + ((code[1] >> 2) & 1),
					'0' + ((code[1] >> 3) & 1),
					'0' + ((code[1] >> 4) & 1),
					'0' + ((code[1] >> 5) & 1),
					'0' + ((code[1] >> 6) & 1),
					'0' + ((code[1] >> 7) & 1),
					(signed char)code[2], code[2] > 0x7F ? "128" : "127",
					(signed char)code[3], code[3] > 0x7F ? "128" : "127");
				break;
			case 0xF6: format_status(p, "Disable echo"); break;
			case 0xF7: format_status(p, "Echo settings (delay: %d, feedback: %d, filter: %d)", code[1], (signed char)code[2], code[3]); break;
			case 0xF8: format_status(p, "Echo volume (delay: %d, left volume: %d, right volume: %d)", code[1], (signed char)code[2], (signed char)code[3]); break;
			case 0xF9: {
				BYTE note = (code[3] & 0x7F);
				format_status(p, "Pitch bend (delay: %d, duration: %d, note: %s%c)", code[1], code[2], notes[note%12], '1' + note / 12);
				break;
			}
			case 0xFA: format_status(p, "Base percussion instrument %d", code[1]); break;
			case 0xFB: format_status(p, "No op [FB %02X %02X]", code[1], code[2]); break;
			case 0xFC: format_status(p, "Mute channel (DEBUG)"); break;
			case 0xFD: format_status(p, "Enable fast-foward (DEBUG)"); break;
			case 0xFE: format_status(p, "Disable fast-foward (DEBUG)"); break;
			case 0xFF: format_status(p, "INVALID"); break;
		}
	}
}
