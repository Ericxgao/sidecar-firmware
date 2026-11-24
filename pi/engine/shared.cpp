// pi/engine/shared.cpp - see shared.h.
#include "shared.h"

Output g_Output;
peakfan::Fan g_PeakFan;
Spsc<uint8_t, 8192> g_MidiIn;
Spsc<uint8_t, kMidiOutBytes> g_MidiOut;
RecordReader<Spsc<uint8_t, kMidiOutBytes>> g_ToHost (&g_MidiOut);
Spsc<TCmd, 16> g_Cmds;
Spsc<dn2_native::LinkRequest, 4> g_Dn2LinkOut;
Spsc<dn2_native::UiReply, 2> g_Dn2UiOut;
uint32_t g_nDn2UiToken = 0;
uint32_t g_nDn2Transport = 0;
Spsc<uint32_t, 1024> g_KeysIn;
Spsc<uint32_t, 512> g_KeysToRytm;
char g_KeyNames[5][24];
Spsc<uint32_t, 256> g_AsampMidi;
volatile bool g_bRytmGone = false;
volatile bool g_bRytmResync = false;
unsigned g_nToHostFull = 0;
volatile bool g_bRytmOff = false;
volatile bool g_bComputerMidi;
