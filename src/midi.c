#include <windows.h>
#include <mmsystem.h>
#include "ebmusv2.h"
#include "misc.h"

static HMIDIIN hMidiIn = NULL;

static void outputMidiError(unsigned int err) {
	char errmsg[256];
	midiInGetErrorText(err, &errmsg[0], 255);
	MessageBox2(errmsg, "MIDI Error", MB_ICONEXCLAMATION);
}

void closeMidiInDevice() {
	if (hMidiIn != NULL) {
		midiInStop(hMidiIn);
		midiInClose(hMidiIn);
		hMidiIn = NULL;
	}
}

void openMidiInDevice(int deviceId, void* callback) {
	if (deviceId > -1) {
		unsigned int err;
		if ((err = midiInOpen(&hMidiIn, deviceId, (DWORD_PTR)(void*)callback, 0, CALLBACK_FUNCTION))) {
			outputMidiError(err);
			return;
		}

		if ((err = midiInStart(hMidiIn))) {
			midiInClose(hMidiIn);
			outputMidiError(err);
			return;
		}
	}
}
