/*
    midi-recv - standalone CAMD MIDI monitor (no support-file dependencies).

    Usage:  midi-recv <cluster>

    Creates a MidiNode with a receiver link on the named cluster and prints
    every message that arrives until Ctrl-C.

    Examples:
        midi-recv alsaseq.in.0      (watch MIDI coming from the Linux host)
        midi-recv hostmidi.in.0

    Build:  x86_64-aros-gcc --sysroot <AROS/Development> midi-recv.c -o midi-recv
*/

#define USE_INLINE_STDARG

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/camd.h>

#include <dos/dos.h>
#include <midi/camd.h>
#include <utility/tagitem.h>

struct Library    *CamdBase;
struct DosLibrary *DOSBase;

#define SYSEX_MAX 2048

static void print_msg(MidiMsg *msg)
{
    UBYTE status = msg->mm_Status;
    UBYTE d1     = msg->mm_Data1;
    UBYTE d2     = msg->mm_Data2;
    ULONG chan   = (status & 0x0f) + 1;
    const char *name;

    Printf("%08lx  ", (ULONG)msg->mm_Msg);      /* raw message */

    switch (status & 0xf0) {
        case 0x80:
            Printf("ch%2ld note-off      %3ld vel %3ld\n", chan, (ULONG)d1, (ULONG)d2);
            return;
        case 0x90:
            if (d2 == 0)
                Printf("ch%2ld note-off      %3ld vel %3ld\n", chan, (ULONG)d1, (ULONG)d2);
            else
                Printf("ch%2ld note-on       %3ld vel %3ld\n", chan, (ULONG)d1, (ULONG)d2);
            return;
        case 0xa0:
            Printf("ch%2ld poly-pressure  %3ld val %3ld\n", chan, (ULONG)d1, (ULONG)d2);
            return;
        case 0xb0:
            Printf("ch%2ld control-change %3ld val %3ld\n", chan, (ULONG)d1, (ULONG)d2);
            return;
        case 0xc0:
            Printf("ch%2ld program-change %3ld\n", chan, (ULONG)d1);
            return;
        case 0xd0:
            Printf("ch%2ld channel-press  %3ld\n", chan, (ULONG)d1);
            return;
        case 0xe0:
            Printf("ch%2ld pitch-bend     %5ld\n", chan, (ULONG)(((UWORD)d2 << 7) | d1));
            return;
        case 0xf0:
            switch (status) {
                case 0xf8: name = "clock";          break;
                case 0xfa: name = "start";          break;
                case 0xfb: name = "continue";       break;
                case 0xfc: name = "stop";           break;
                case 0xfe: name = "active-sensing"; break;
                case 0xff: name = "reset";          break;
                default:   name = "system";         break;
            }
            Printf("%s\n", name);
            return;
    }
    Printf("unknown %02lx\n", (ULONG)status);
}

int main(int argc, char **argv)
{
    int              result = RETURN_FAIL;
    BYTE             sig    = -1;
    struct MidiNode *node   = NULL;
    struct MidiLink *link   = NULL;
    UBYTE           *sysex  = NULL;
    MidiMsg          msg;
    BOOL             alive;

    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 0L);
    if (DOSBase == NULL) return RETURN_FAIL;

    if (argc < 2) {
        PutStr("Usage: midi-recv <cluster>\nExample: midi-recv alsaseq.in.0\n");
        CloseLibrary((struct Library *)DOSBase);
        return RETURN_WARN;
    }

    CamdBase = OpenLibrary((UBYTE *)"camd.library", 0L);
    if (CamdBase == NULL) {
        PutStr("Error opening 'camd.library'!\n");
        CloseLibrary((struct Library *)DOSBase);
        return RETURN_FAIL;
    }

    sysex = AllocVec(SYSEX_MAX, MEMF_ANY);
    sig   = AllocSignal(-1);
    if (sig == -1) {
        PutStr("No signal!\n");
        goto cleanup;
    }

    node = CreateMidi(MIDI_Name,      "midi-recv",
                      MIDI_MsgQueue,   2048L,
                      MIDI_SysExSize,  (ULONG)SYSEX_MAX,
                      MIDI_RecvSignal, (IPTR)sig,
                      TAG_END);
    if (node == NULL) {
        PutStr("Error creating midi node!\n");
        goto cleanup;
    }

    link = AddMidiLink(node, MLTYPE_Receiver,
                       MLINK_Location, argv[1],
                       TAG_END);
    if (link == NULL) {
        Printf("Error linking receiver to '%s'\n", argv[1]);
        goto cleanup;
    }

    Printf("Listening on '%s' - press Ctrl-C to stop.\n", argv[1]);

    alive = TRUE;
    while (alive) {
        ULONG sigs = Wait((1L << sig) | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C)
            alive = FALSE;

        while (GetMidi(node, &msg)) {
            if (msg.mm_Status == 0xf0) {          /* SysEx */
                ULONG len = QuerySysEx(node);
                if (sysex != NULL && len <= SYSEX_MAX) {
                    ULONG got = GetSysEx(node, sysex, SYSEX_MAX);
                    Printf("sysex (%ld bytes)\n", (ULONG)got);
                } else {
                    SkipSysEx(node);
                    Printf("sysex (skipped, too big: %ld)\n", (ULONG)len);
                }
            } else {
                print_msg(&msg);
            }
        }
    }
    result = RETURN_OK;

cleanup:
    if (link)       RemoveMidiLink(link);
    if (node)       DeleteMidi(node);
    if (sig != -1)  FreeSignal(sig);
    if (sysex)      FreeVec(sysex);
    CloseLibrary(CamdBase);
    CloseLibrary((struct Library *)DOSBase);
    return result;
}
