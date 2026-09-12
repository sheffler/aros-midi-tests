/*
    midi-info - standalone CAMD inspector (no support-file dependencies).

    Lists every MIDI cluster as a bus (senders -> [cluster] -> receivers) with
    the relevant per-link detail, then every MIDI node (client) with the
    clusters it is connected to.

    A "connection" in CAMD is a MidiLink joining a MidiNode to a Cluster as a
    sender or a receiver. Drivers are participants too, but they attach via
    internal nodes that NextClusterLink does not return, so they show up as
    "hardware/driver participant(s)" counted but not listed - e.g. the alsaseq
    driver is the (hidden) sender of alsaseq.in.0 and receiver of alsaseq.out.0.

    Usage:  midi-info
    Build:  x86_64-aros-gcc --sysroot <AROS/Development> midi-info.c -o midi-info
*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/camd.h>

#include <exec/nodes.h>
#include <midi/camd.h>

/* camd tags driver/hardware endpoints with this ln_Type (see AllocDriverData);
   NextClusterLink skips them, so we walk the raw lists to see them. */
#define CAMD_DRIVER_NODETYPE (NT_USER - MLTYPE_NTypes)

struct Library    *CamdBase;
struct DosLibrary *DOSBase;

/* ---- decoders (print directly, no buffer juggling) ---- */

static void print_chanmask(UWORD mask)
{
    int i, first = 1;
    if (mask == (UWORD)0xffff) { PutStr("all"); return; }
    if (mask == 0)             { PutStr("none"); return; }
    for (i = 0; i < 16; i++) {
        if (mask & (1 << i)) {
            Printf(first ? "%ld" : ",%ld", (LONG)(i + 1));
            first = 0;
        }
    }
}

static void print_events(ULONG mask)
{
    static const struct { ULONG bit; const char *name; } tab[] = {
        { CMF_Note,       "note"  }, { CMF_Prog,      "prog"  },
        { CMF_PitchBend,  "bend"  }, { CMF_Ctrl,      "ctrl"  },
        { CMF_Mode,       "mode"  }, { CMF_ChanPress, "cpress"},
        { CMF_PolyPress,  "ppress"}, { CMF_RealTime,  "rt"    },
        { CMF_SysCom,     "syscom"}, { CMF_SysEx,     "sysex" },
    };
    int i, first = 1;
    if ((mask & CMD_All) == CMD_All) { PutStr("all"); return; }
    if (mask == 0)                   { PutStr("none"); return; }
    for (i = 0; i < (int)(sizeof(tab)/sizeof(tab[0])); i++) {
        if ((mask & tab[i].bit) == tab[i].bit) {
            Printf(first ? "%s" : ",%s", tab[i].name);
            first = 0;
        }
    }
}

static void print_clienttype(UWORD t)
{
    static const struct { UWORD bit; const char *name; } tab[] = {
        { CCType_Sequencer,      "Sequencer"      },
        { CCType_SampleEditor,   "SampleEditor"   },
        { CCType_PatchEditor,    "PatchEditor"    },
        { CCType_Notator,        "Notator"        },
        { CCType_EventProcessor, "EventProcessor" },
        { CCType_EventFilter,    "EventFilter"    },
        { CCType_EventRouter,    "EventRouter"    },
        { CCType_ToneGenerator,  "ToneGenerator"  },
        { CCType_EventGenerator, "EventGenerator" },
        { CCType_GraphicAnimator,"GraphicAnimator"},
    };
    int i, first = 1;
    if (t == 0) { PutStr("-"); return; }
    for (i = 0; i < (int)(sizeof(tab)/sizeof(tab[0])); i++) {
        if (t & tab[i].bit) {
            Printf(first ? "%s" : "|%s", tab[i].name);
            first = 0;
        }
    }
}

/* one link line: "    name            port=P chan=.. events=.." */
static void print_link(struct MidiLink *link)
{
    struct MidiNode *mn = link->ml_MidiNode;
    const char *nm = (mn && mn->mi_Node.ln_Name) ? (const char *)mn->mi_Node.ln_Name
                                                  : "(unnamed)";
    Printf("        %-18s port=%-3ld chan=", (char *)nm, (LONG)link->ml_PortID);
    print_chanmask(link->ml_ChannelMask);
    PutStr(" events=");
    print_events(link->ml_EventTypeMask);
    if (link->ml_Flags & MLF_PrivateLink) PutStr(" [private]");
    if (link->ml_Flags & MLF_DeviceLink)  PutStr(" [device]");
    if (link->ml_ClusterComment)
        Printf(" \"%s\"", (char *)link->ml_ClusterComment);
    PutStr("\n");
}

/* Walk a cluster's raw sender/receiver List so we see BOTH app links and the
   driver/hardware endpoints (which NextClusterLink hides). */
static void list_side(struct List *list, const char *label)
{
    struct Node *n = list->lh_Head;

    if (n->ln_Succ == NULL) {           /* empty list */
        Printf("    %s: (none)\n", (char *)label);
        return;
    }
    Printf("    %s:\n", (char *)label);
    for (; n->ln_Succ != NULL; n = n->ln_Succ) {
        if (n->ln_Type == CAMD_DRIVER_NODETYPE)
            PutStr("        <hardware/driver endpoint>\n");
        else
            print_link((struct MidiLink *)n);
    }
}

int main(void)
{
    APTR lock;
    struct MidiCluster *cl;
    struct MidiNode    *node;
    int nclusters = 0;

    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 0L);
    if (DOSBase == NULL) return RETURN_FAIL;

    CamdBase = OpenLibrary((UBYTE *)"camd.library", 0L);
    if (CamdBase == NULL) {
        PutStr("Error opening 'camd.library'!\n");
        CloseLibrary((struct Library *)DOSBase);
        return RETURN_FAIL;
    }

    lock = LockCAMD(CD_Linkages);
    if (lock == NULL) {
        PutStr("Cannot lock CAMD.\n");
        CloseLibrary(CamdBase);
        CloseLibrary((struct Library *)DOSBase);
        return RETURN_FAIL;
    }

    /* ---- clusters as buses ---- */
    PutStr("=== CLUSTERS (senders -> [cluster] -> receivers) ===\n");
    cl = NextCluster(NULL);
    if (cl == NULL)
        PutStr("(no clusters)\n");
    while (cl != NULL) {
        /* NB: mcl_Participants / mcl_PublicParticipants are not maintained by
           AROS camd.library (always 0), so we don't report them - the raw
           lists below are the real picture. */
        Printf("\n%s  flags=0x%04lx\n",
               (char *)cl->mcl_Node.ln_Name,
               (ULONG)cl->mcl_Flags);

        list_side(&cl->mcl_Senders,   "senders");
        list_side(&cl->mcl_Receivers, "receivers");

        nclusters++;
        cl = NextCluster(cl);
    }

    /* ---- nodes (clients) and what they connect to ---- */
    PutStr("\n=== NODES (clients) ===\n");
    node = NextMidi(NULL);
    if (node == NULL)
        PutStr("(no nodes)\n");
    while (node != NULL) {
        struct MidiLink *link;

        Printf("\n%s  type=", (char *)node->mi_Node.ln_Name);
        print_clienttype(node->mi_ClientType);
        Printf("  msgqueue=%ld sysex=%ld\n",
               (LONG)node->mi_MsgQueueSize, (LONG)node->mi_SysExQueueSize);

        link = NextMidiLink(node, NULL, MLTYPE_Sender);
        while (link != NULL) {
            if (link->ml_Location)
                Printf("    sends to    %s\n",
                       (char *)link->ml_Location->mcl_Node.ln_Name);
            link = NextMidiLink(node, link, MLTYPE_Sender);
        }
        link = NextMidiLink(node, NULL, MLTYPE_Receiver);
        while (link != NULL) {
            if (link->ml_Location)
                Printf("    receives from %s\n",
                       (char *)link->ml_Location->mcl_Node.ln_Name);
            link = NextMidiLink(node, link, MLTYPE_Receiver);
        }

        node = NextMidi(node);
    }

    Printf("\n%ld cluster(s).\n", (LONG)nclusters);

    UnlockCAMD(lock);
    CloseLibrary(CamdBase);
    CloseLibrary((struct Library *)DOSBase);
    return RETURN_OK;
}
