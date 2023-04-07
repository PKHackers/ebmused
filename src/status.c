#include <stdio.h> // Must be included before ebmusv2.h

#include "ebmusv2.h"
#include <assert.h>
#include <commctrl.h>
#include <stdarg.h>

void set_status(const char* s) {
	SendMessage(hwndStatus, SB_SETTEXT, 0, (LPARAM)s);
}

void set_status_format(const char* format, ...) {
	char buf[256]; // I'm lazy.

	va_list args;
	va_start(args, format);
	int result = vsprintf(buf, format, args);
	assert(result >= 0 && result < 255);
	set_status(buf);
	va_end(args);
}

static const char* notes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

void set_code_tip_status(BYTE *code) {
	if (code[0] == 0x00) {
		set_status("End of pattern/subroutine");
	} else if (code[0] < 0x80) {
		if (code[1] < 0x80) {
			set_status_format("Note length: %d velocity: %d/7 release: %d/15", code[0], (code[1] >> 4) & 0x7, code[1] & 0xF);
		} else {
			set_status_format("Note length: %d", code[0]);
		}
	} else if (code[0] < 0xC8) {
		BYTE note = (code[0] & 0x7F);
		set_status_format("Note %s%c", notes[note % 12], '1' + note / 12);
	} else if (code[0] == 0xC8) {
		set_status("Hold");
	} else if (code[0] == 0xC9) {
		set_status("Rest");
	} else if (code[0] < 0xE0) {
		set_status_format("Percussion %d", code[0] - 0xC9);
	} else {
		switch (code[0]) {
			case 0xE0: set_status_format("Instrument %d", code[1]); break;
			case 0xE1: set_status_format("Pan %d", code[1]); break;
			case 0xE2: set_status_format("Pan (duration: %d pan: %d)", code[1], code[2]); break;
			case 0xE3: set_status_format("Enable vibrato (delay: %d freq: %d range: %d)", code[1], code[2], code[3]); break;
			case 0xE4: set_status("Disable vibrato"); break;
			case 0xE5: set_status_format("Master volume %d/255", code[1]); break;
			case 0xE6: set_status_format("Master volume (duration: %d volume: %d/255)", code[1], code[2]); break;
			case 0xE7: set_status_format("Tempo %d", code[1]); break;
			case 0xE8: set_status_format("Tempo (duration: %d tempo: %d)", code[1], code[2]); break;
			case 0xE9: set_status_format("Global transpose %d", code[1]); break;
			case 0xEA: set_status_format("Channel transpose %d", code[1]); break;
			case 0xEB: set_status_format("Enable tremolo (delay: %d freq: %d amp: %d)", code[1], code[2], code[3]); break;
			case 0xEC: set_status("Disable tremolo"); break;
			case 0xED: set_status_format("Channel volume %d/255", code[1]); break;
			case 0xEE: set_status_format("Channel volume (duration: %d volume: %d/255)", code[1], code[2]); break;
			case 0xEF: set_status("Call subroutine"); break;
			case 0xF0: set_status_format("Set vibrato attack %d", code[1]); break;
			case 0xF1: set_status_format("Enable portamento (delay: %d duration: %d range: +%d)", code[1], code[2], code[3]); break;
			case 0xF2: set_status_format("Enable portamento (delay: %d duration: %d range: -%d)", code[1], code[2], code[3]); break;
			case 0xF3: set_status("Disable portamento"); break;
			case 0xF4: set_status_format("Finetune %d/256", code[1]); break;
			case 0xF5:
				set_status_format(
					"Enable echo (channels: %c%c%c%c%c%c%c%c, lvolume: %d/255 rvolume: %d/255)",
					'0' + (code[1] & 1),
					'0' + ((code[1] >> 1) & 1),
					'0' + ((code[1] >> 2) & 1),
					'0' + ((code[1] >> 3) & 1),
					'0' + ((code[1] >> 4) & 1),
					'0' + ((code[1] >> 5) & 1),
					'0' + ((code[1] >> 6) & 1),
					'0' + ((code[1] >> 7) & 1),
					code[2],
					code[3]);
				break;
			case 0xF6: set_status("Disable echo"); break;
			case 0xF7: set_status_format("Echo settings (delay: %d feedback: %d filter: %d)", code[1], code[2], code[3]); break;
			case 0xF8: set_status_format("Echo volume (delay: %d lvolume: %d rvolume: %d)", code[1], code[2], code[3]); break;
			case 0xF9: 
				BYTE note = (code[3] & 0x7F);
				set_status_format("Pitch bend (delay: %d duration: %d note: %s%c)", code[1], code[2], notes[note%12], '1' + note / 12);
				break;
			case 0xFA: set_status_format("Base percussion instrument %d", code[1]); break;
			case 0xFB: set_status_format("Unknown [FB %02X %02X]", code[1], code[2]); break;
			case 0xFC: set_status("Mute channel (DEBUG)"); break;
			case 0xFD: set_status("Enable fast-foward (DEBUG)"); break;
			case 0xFE: set_status("Disable fast-foward (DEBUG)"); break;
			case 0xFF: set_status("INVALID"); break;
		}
	}
}
