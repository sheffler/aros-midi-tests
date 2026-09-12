/*
    midi-send - standalone CAMD MIDI sender (no support-file dependencies).

    Usage:  midi-send <cluster> <hexbyte> [<hexbyte> ...]

    Sends the given raw MIDI bytes, grouped into messages (running-status
    aware), to the named cluster as a Sender link.

    Examples:
        midi-send alsaseq.out.0 90 3C 64     note-on   ch1 C4 vel100
        midi-send alsaseq.out.0 80 3C 00     note-off  ch1 C4
        midi-send alsaseq.out.0 B0 07 40     control-change ch1 vol
        midi-send debugdriver.out.0 90 3C 64 (debugdriver kprintf's the bytes)

    Build:  x86_64-aros-gcc --sysroot <AROS/Development> midi-send.c -o midi-send
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

/* how many data bytes follow a given status byte */
static int data_len(UBYTE status)
{
    if (status < 0x80) return 0;
    switch (status & 0xf0) {
        case 0x80: case 0x90: case 0xa0:
        case 0xb0: case 0xe0: return 2;   /* note off/on, poly, ctrl, bend */
        case 0xc0: case 0xd0: return 1;   /* prog change, chan pressure    */
        case 0xf0:
            switch (status) {
                case 0xf1: case 0xf3: return 1;
                case 0xf2:            return 2;
                default:              return 0;   /* realtime / tune req    */
            }
    }
    return 0;
}

/* parse a 1-2 digit hex token; -1 on error */
static int parse_hex(const char *s)
{
    int v = 0, n = 0;
    while (*s) {
        char c = *s++;
        int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = (v << 4) | d;
        n++;
    }
    if (n == 0 || v > 0xff) return -1;
    return v;
}

int main(int argc, char **argv)
{
    int result = RETURN_FAIL;
    struct MidiNode *node;
    struct MidiLink *link;

    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 0L);
    if (DOSBase == NULL) return RETURN_FAIL;

    if (argc < 3) {
        PutStr("Usage: midi-send <cluster> <hexbyte> [<hexbyte> ...]\n"
               "Example: midi-send alsaseq.out.0 90 3C 64\n");
        CloseLibrary((struct Library *)DOSBase);
        return RETURN_WARN;
    }

    CamdBase = OpenLibrary((UBYTE *)"camd.library", 0L);
    if (CamdBase == NULL) {
        PutStr("Error opening 'camd.library'!\n");
        CloseLibrary((struct Library *)DOSBase);
        return RETURN_FAIL;
    }

    node = CreateMidi(MIDI_Name, "midi-send", TAG_END);
    if (node == NULL) {
        PutStr("Error creating midi node!\n");
    } else {
        link = AddMidiLink(node, MLTYPE_Sender,
                           MLINK_Location, argv[1],
                           TAG_END);
        if (link == NULL) {
            Printf("Error linking sender to '%s'\n", argv[1]);
        } else {
            int   i;
            UBYTE status = 0, data[2];
            int   need = 0, have = 0;

            result = RETURN_OK;
            for (i = 2; i < argc; i++) {
                int b = parse_hex(argv[i]);
                if (b < 0) {
                    Printf("Bad hex byte: %s\n", argv[i]);
                    result = RETURN_ERROR;
                    break;
                }
                if (b >= 0x80) {                 /* status byte */
                    status = (UBYTE)b;
                    need   = data_len(status);
                    have   = 0;
                    if (need == 0) {             /* zero-data message */
                        PutMidi(link, ((ULONG)status) << 24);
                        Printf("sent: %02lx\n", (ULONG)status);
                    }
                } else {                         /* data byte */
                    if (status == 0) {
                        Printf("Data byte %02lx with no status\n", (ULONG)b);
                        result = RETURN_ERROR;
                        break;
                    }
                    if (have < 2) data[have] = (UBYTE)b;
                    have++;
                    if (have == need) {
                        ULONG msg = ((ULONG)status) << 24;
                        if (need >= 1) msg |= ((ULONG)data[0]) << 16;
                        if (need >= 2) msg |= ((ULONG)data[1]) << 8;
                        PutMidi(link, msg);
                        Printf("sent: %02lx %02lx %02lx\n",
                               (ULONG)status,
                               (ULONG)(need >= 1 ? data[0] : 0),
                               (ULONG)(need >= 2 ? data[1] : 0));
                        have = 0;                /* keep status: running status */
                    }
                }
            }
            RemoveMidiLink(link);
        }
        DeleteMidi(node);
    }

    CloseLibrary(CamdBase);
    CloseLibrary((struct Library *)DOSBase);
    return result;
}
