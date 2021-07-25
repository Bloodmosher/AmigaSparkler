// Amiga Sparkler Copyright 2021 by Bloodmosher
// Use this tool to verify RGB2HDMI boards built for the Amiga.
// The default settings at startup typically exhibit sparkling
// on boards that have issues.
// Any modifications to this code must include the above comment
// followed by documentation of the changes below.

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/exec.h>
#include <intuition/intuition.h>
#include <hardware/cia.h>
#include <hardware/intbits.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>
#include <graphics/copper.h>
#include <hardware/dmabits.h>
#include <devices/keyboard.h>

struct ExecLibrary* SysBase = NULL;
struct GfxBase* GfxBase = NULL;
struct DiskfontBase* DiskfontBase = NULL;

extern struct Custom custom;
extern struct CIA ciaa;

// Two copperlists, second used for interlaced mode
UWORD* g_pCopperList = NULL;
UWORD* g_pCopperList2 = NULL;

// Pointer to the main bitmap used for displaying test patterns
struct BitMap* g_pBitmap = NULL;

// RastPort used by Draw() to draw on bitmaps
struct RastPort g_rp;

// Any registers that need to be restored on exit can be saved here
struct Custom g_oldRegs;

// Keyboard related stuff
struct IOStdReq* KeyIO = NULL;
struct MsgPort* KeyMP;
char* keyMatrix;
#define MATRIX_SIZE 16L

void ReadKeyboard()
{
    KeyIO->io_Command = KBD_READMATRIX;
    KeyIO->io_Data = (APTR)keyMatrix;
    KeyIO->io_Length = MATRIX_SIZE;
    
    DoIO((struct IORequest*)KeyIO);
}

BOOL GetKeyState(int rawKey)
{
    if (keyMatrix == NULL)
        return FALSE;
    
    // raw key is the bit number
    int byteNumber = rawKey / 8;
    int bitNumber = rawKey % 8;

    if ((keyMatrix[byteNumber] & (1<<bitNumber))!=0)
        return TRUE;

    return FALSE;
}

// Allocate and initialize a bitmap with the specified line mode
void createBitmap(int width, int height, int lineMode)
{
    g_pBitmap = AllocBitMap(width, height, 4, BMF_CLEAR|BMF_DISPLAYABLE);

    g_rp.BitMap = g_pBitmap;
    struct RastPort* rp = &g_rp;

    int bytesPerLine = width/8; 

    BOOL evenX = FALSE;
    int xCounter = 0;
    for (int x=0; x < bytesPerLine; x++) 
    {
        BOOL evenLine = TRUE;
        for (int y=0; y < height; y++)
        {
            int index = (y * bytesPerLine);
            index += x;
            UBYTE val = 0x55;

            if (y == height - 1)
            {
                g_pBitmap->Planes[0][index] = 0xff;
            }
            else if (lineMode == 1) // alternating pixels
            {
                if (evenLine)
                {
                    g_pBitmap->Planes[0][index] = 0x55;
                } 
                else
                {
                    g_pBitmap->Planes[0][index] = 0xAA;
                }
            }
            else if (lineMode == 2) // vertical bars
            {
                if (evenX)
                {
                    g_pBitmap->Planes[0][index] = 0xAA;
                }
                else
                {
                    g_pBitmap->Planes[0][index] = 0xAA;
                }
            }
            else if (lineMode == 3) // horizontal bars
            {
                if (evenLine)
                {
                    g_pBitmap->Planes[0][index] = 0xFF;
                }
                else
                {
                    g_pBitmap->Planes[0][index] = 0x00;
                }
            }
            else if (lineMode == 4) // solid fill
            {
                g_pBitmap->Planes[0][index] = 0xff;
            }
            else if (lineMode == 5) // vertical bars 2
            {
                if (xCounter == 0)
                {
                    g_pBitmap->Planes[0][index] = 0x92;
                }
                else if (xCounter == 1)
                {
                    g_pBitmap->Planes[0][index] = 0x49;
                }
                else if (xCounter == 2)
                {
                    g_pBitmap->Planes[0][index] = 0x24;
                }
                
            }
            else if (lineMode == 6) // vertical bars 3
            {
                g_pBitmap->Planes[0][index] = 0x88;
            }
            else if (lineMode == 7) // vertical bars 3
            {
                if (xCounter == 0)
                {
                    g_pBitmap->Planes[0][index] = 0x84;
                }
                else if (xCounter == 1)
                {
                        g_pBitmap->Planes[0][index] = 0x21;
                }
                else if (xCounter == 2)
                {
                    g_pBitmap->Planes[0][index] = 0x8;
                }
                else if (xCounter == 3)
                {
                    g_pBitmap->Planes[0][index] = 0x42;
                }
                else if (xCounter == 4)
                {
                    g_pBitmap->Planes[0][index] = 0x10;
                }
            }

            evenLine = !evenLine;
        }
        evenX = !evenX;
        xCounter++;
        if (lineMode == 5 && xCounter == 3)
        {
            xCounter = 0;
        }
        if (lineMode == 7 && xCounter == 5)
        {
            xCounter = 0;
        }
    }
}

void freeBitmap()
{
    FreeBitMap(g_pBitmap);
}

struct 
{
    int copperList1ColorStartIndex;
    int copperList2ColorStartIndex;
    
    UWORD r[2];
    UWORD g[2];
    UWORD b[2];

} Globals;

struct DebugInfo
{
    int width;
    int height;
    BOOL interlaced;
    BOOL hires;
    int lineMode;
    BOOL colorOrTextChanged;
    BOOL showhelp;
    char debugText[255];
    BOOL pal;
    struct TextFont* pFont1;
};

// Create copperlists and start the display
void setupDisplay(BOOL hires, BOOL interlaced, BOOL pal)
{
    const int loresInterlacedBpl = 0x28;
    const int hiresInterlacedBpl = 0x50;

    if (pal)
    {
        custom.beamcon0 = 0x20;
    }
    else
    {
        custom.beamcon0 = 0x00;
    }
    
    custom.dmacon = DMAF_ALL;
    UWORD oldIntena = custom.intenar;
    custom.intena = 0x7fff; // disable interrupts

    UWORD bplmod = 0;
    if (interlaced)
    {
        if (hires)
        {
            bplmod = hiresInterlacedBpl;
        }
        else
        {
            bplmod = loresInterlacedBpl;
        }
    }
    
    int i = 0;
    g_pCopperList[i++] = 0x100; // bplcon0
    
    UWORD bplcon0 = 0x4200;
    if (hires)
    {
        bplcon0 = 0xC200;
    }
    if (interlaced)
    {
        bplcon0 |= 0x04;
    }
    g_pCopperList[i++] = bplcon0;

    g_pCopperList[i++] = 0x108; // bpl1mod
    g_pCopperList[i++] = bplmod;

    g_pCopperList[i++] = 0x10A; // bpl2mod
    g_pCopperList[i++] = bplmod;
    
    g_pCopperList[i++] = 0x092; // DDFSTART
    g_pCopperList[i++] = 0x38;

    g_pCopperList[i++] = 0x094; // DDFSTOP
    g_pCopperList[i++] = 0xd0;

    UWORD colorIndex = 0x180;

    // Setup a color palette; has more colors than we actually use
    UWORD colorValues[] = { 0, 0xfbf, 0x710, 0xC10, 0x910, 0xE20, 0xFCB, 0xFFF, 0xF42, 0x0, 0xF98, 0xF65, 0xC54, 0x322, 0x444, 0x888 };
    Globals.copperList1ColorStartIndex = i;

    for (int j=0;j < 16;j++)
    {
        g_pCopperList[i++] = colorIndex;
        colorIndex+=2;
        g_pCopperList[i++] = colorValues[j];
    }

    int bitplaneIndex = 0;
    g_pCopperList[i++] = 0x00E0;
    g_pCopperList[i] = ((unsigned int)(g_pBitmap->Planes[bitplaneIndex]) & 0xFFFF0000) >> 16;

    i++;
    g_pCopperList[i++] = 0x00E2;
    g_pCopperList[i] = ((unsigned int)(g_pBitmap->Planes[bitplaneIndex]) & 0xFFFF);
    i++;

    bitplaneIndex++;
    g_pCopperList[i++] = 0x00E4;
    g_pCopperList[i] = ((unsigned int)(g_pBitmap->Planes[bitplaneIndex]) & 0xFFFF0000) >> 16;
    
    i++;
    g_pCopperList[i++] = 0x00E6;
    g_pCopperList[i] = ((unsigned int)(g_pBitmap->Planes[bitplaneIndex]) & 0xFFFF);
    i++;

    bitplaneIndex++;
    g_pCopperList[i++] = 0x00E8;
    g_pCopperList[i] = ((unsigned int)(g_pBitmap->Planes[bitplaneIndex]) & 0xFFFF0000) >> 16;

    i++;
    g_pCopperList[i++] = 0x00EA;
    g_pCopperList[i] = ((unsigned int)(g_pBitmap->Planes[bitplaneIndex]) & 0xFFFF);
    i++;

    bitplaneIndex++;
    g_pCopperList[i++] = 0x00EC;
    g_pCopperList[i] = ((unsigned int)(g_pBitmap->Planes[bitplaneIndex]) & 0xFFFF0000) >> 16;

    i++;
    g_pCopperList[i++] = 0x00EE;
    g_pCopperList[i] = ((unsigned int)(g_pBitmap->Planes[bitplaneIndex]) & 0xFFFF);
    i++;

    // move #$3481, $dff08e (DIWSTRT)
    g_pCopperList[i++] = 0x008e;
    // g_pCopperList[i++] = 0x3481;
    g_pCopperList[i++] = 0x2c81;

    //0090 FCC1 move #$FCC1,$DFF090 (DIWSTOP)
    g_pCopperList[i++] = 0x0090;
    
    if (pal)
        g_pCopperList[i++] = 0x2CC1;
    else
        g_pCopperList[i++] = 0xF4C1;

    // For interlaced mode we need another copperlist for the other field
    if (interlaced)
    {
        int j = 0;
        g_pCopperList2[j++] = 0x100; // bplcon0
        
        bplcon0 = 0x4200;
        if (hires)
        {
            bplcon0 = 0xC200;
        }
        if (interlaced)
        {
            bplcon0 |= 0x04;
        }
        g_pCopperList2[j++] = bplcon0;

        g_pCopperList2[j++] = 0x108; // bpl1mod
        g_pCopperList2[j++] = bplmod;

        g_pCopperList2[j++] = 0x10A; // bpl2mod
        g_pCopperList2[j++] = bplmod;
        
        g_pCopperList2[j++] = 0x092; // DDFSTART
        g_pCopperList2[j++] = 0x38;

        g_pCopperList2[j++] = 0x094; // DDFSTOP
        g_pCopperList2[j++] = 0xd0;

        Globals.copperList2ColorStartIndex = j;

        for (int k=0;k < 16;k++)
        {
            g_pCopperList2[j++] = colorIndex;
            colorIndex+=2;
            g_pCopperList2[j++] = colorValues[k];
        }

        bitplaneIndex = 0;
        g_pCopperList2[j++] = 0x00E0;
        
        unsigned int bitplaneOffset = bplmod; 
        
        unsigned int addr = g_pBitmap->Planes[bitplaneIndex];
        addr += bitplaneOffset; // start on the next line for this field
        
        g_pCopperList2[j] = ((unsigned int)(addr) & 0xFFFF0000) >> 16;

        j++;
        g_pCopperList2[j++] = 0x00E2;
        g_pCopperList2[j] = ((unsigned int)(addr) & 0xFFFF);
        j++;

        bitplaneIndex++;
        addr = g_pBitmap->Planes[bitplaneIndex];
        addr += bitplaneOffset; // start on the next line for this field

        g_pCopperList2[j++] = 0x00E4;
        g_pCopperList2[j] = ((unsigned int)(addr) & 0xFFFF0000) >> 16;
        j++;

        g_pCopperList2[j++] = 0x00E6;
        g_pCopperList2[j] = ((unsigned int)(addr) & 0xFFFF);
        j++;

        bitplaneIndex++;
        addr = g_pBitmap->Planes[bitplaneIndex];
        addr += bitplaneOffset; // start on the next line for this field

        g_pCopperList2[j++] = 0x00E8;
        g_pCopperList2[j] = ((unsigned int)(addr) & 0xFFFF0000) >> 16;
        j++;

        g_pCopperList2[j++] = 0x00EA;
        g_pCopperList2[j] = ((unsigned int)(addr) & 0xFFFF);
        j++;

        bitplaneIndex++;
        addr = g_pBitmap->Planes[bitplaneIndex];
        addr += bitplaneOffset; // start on the next line for this field

        g_pCopperList2[j++] = 0x00EC;
        g_pCopperList2[j] = ((unsigned int)(addr) & 0xFFFF0000) >> 16;
        j++;

        g_pCopperList2[j++] = 0x00EE;
        g_pCopperList2[j] = ((unsigned int)(addr) & 0xFFFF);
        j++;

        // move #$3481, $dff08e (DIWSTRT)
        g_pCopperList2[j++] = 0x008e;
        // g_pCopperList[i++] = 0x3481;
        g_pCopperList2[j++] = 0x2c81;

        //0090 FCC1 move #$FCC1,$DFF090 (DIWSTOP)
        g_pCopperList2[j++] = 0x0090;
        
        if (pal)
        {
            g_pCopperList2[j++] = 0x2CC1;
        }
        else
        {
            g_pCopperList2[j++] = 0xF4C1;
        }

        // F401 FFFE wait HP=0(0x00),VP=244(0xF4) (VE=127,HE=127,BlitterFinishDisable=1)
        g_pCopperList2[j++] = 0xf401;
        g_pCopperList2[j++] = 0xfffe;

        // 0084 0002 move #$0002,$DFF084 (COP1LCH)
        g_pCopperList2[j++] = 0x080;
        g_pCopperList2[j++] = (UWORD)(((unsigned int)g_pCopperList & 0xFFFF0000) >> 16);

        // 0086 1050 move #$1050,$DFF086 (COP1LCL)
        g_pCopperList2[j++] = 0x082;
        g_pCopperList2[j++] = (UWORD)((unsigned int)g_pCopperList & 0xFFFF);

        //FFFF FFFE wait HP=254(0xFE),VP=255(0xFF) (VE=127,HE=127,BlitterFinishDisable=1)
        g_pCopperList2[j++] = 0xFFFF;
        g_pCopperList2[j++] = 0xFFFE;

        // the part that applies to the first copperlist
        // F401 FFFE wait HP=0(0x00),VP=244(0xF4) (VE=127,HE=127,BlitterFinishDisable=1)
        g_pCopperList[i++] = 0xf401;
        g_pCopperList[i++] = 0xfffe;

        // 0084 0002 move #$0002,$DFF084 (COP1LCH)
        g_pCopperList[i++] = 0x080;
        g_pCopperList[i++] = (UWORD)(((unsigned int)g_pCopperList2 & 0xFFFF0000) >> 16);

        // 0086 1050 move #$1050,$DFF086 (COP1LCL)
        g_pCopperList[i++] = 0x082;
        g_pCopperList[i++] = (UWORD)((unsigned int)g_pCopperList2 & 0xFFFF);
    }

    // copper list end
    g_pCopperList[i++] = 0xFFFF;
    g_pCopperList[i++] = 0xFFFE;


    int offset = 1; // color00, (write to value)
    // fix the colors to match what the user has set
    g_pCopperList[Globals.copperList1ColorStartIndex + offset] = (UWORD)((Globals.r[0] << 8) | (Globals.g[0] << 4) | Globals.b[0]);
    g_pCopperList2[Globals.copperList2ColorStartIndex + offset] = (UWORD)((Globals.r[0] << 8) | (Globals.g[0] << 4) | Globals.b[0]);

    offset = 3; // color00, value, color1 (2 bytes each), and now write to value (3 because this is a UWORD*)
    // fix the colors to match what the user has set
    g_pCopperList[Globals.copperList1ColorStartIndex + offset] = (UWORD)((Globals.r[1] << 8) | (Globals.g[1] << 4) | Globals.b[1]);
    g_pCopperList2[Globals.copperList2ColorStartIndex + offset] = (UWORD)((Globals.r[1] << 8) | (Globals.g[1] << 4) | Globals.b[1]);
    
    custom.cop1lc = g_pCopperList;
    custom.copjmp1 = 0;
    
    // DMAF_BLITTER is required if you want to use various RastPort functions like Text and SetRast
    custom.dmacon = DMAF_SETCLR|DMAF_RASTER|DMAF_COPPER|DMAF_BLITTER;
    custom.intena = INTF_SETCLR|INTF_INTEN|INTF_VERTB | oldIntena;
}

void ChangeColorValue(UWORD* colorValue, BOOL* colorOrTextChanged)
{
    if (GetKeyState(0x60) || GetKeyState(0x61))  // shift
    { 
        if (*colorValue < 0x0f)
        {
            (*colorValue)++;
            *colorOrTextChanged = TRUE;
        }
    }
    else
    {
        if (*colorValue > 0)
        {
            (*colorValue)--;
            *colorOrTextChanged = TRUE;
        }
    }
}

// Simple delay that works reasonably well on all Amigas
void Delay()
{
    for (int i=0;i<5;i++)
    {
        WaitTOF();
    }
}

void DrawDebugInfo(struct RastPort* rp, struct DebugInfo* dbgInfo)
{
    SetAPen(rp, 255);
    SetBPen(rp, 0);
    rp->DrawMode = JAM2;
    rp->cp_x = dbgInfo->hires ? 20 : 0;
    
    rp->cp_y = 10;
    char title[] = "Sparkler V1.0 - RGB2HDMI Test Tool - by Bloodmosher";
    Text(rp, title, strlen(title));

    rp->cp_x = dbgInfo->hires ? 20 : 0;
    rp->cp_y = 22;

    sprintf(dbgInfo->debugText, "%s %dx%d I:%d P:%d C0(R:%x G:%x B:%x) C1(R:%x G:%x B:%x)", 
                dbgInfo->pal ? "PAL" : "NTSC", 
                dbgInfo->width, 
                dbgInfo->height, 
                dbgInfo->interlaced, 
                dbgInfo->lineMode, 
                Globals.r[0], Globals.g[0], Globals.b[0], 
                Globals.r[1], Globals.g[1], Globals.b[1]);

    Text(rp, dbgInfo->debugText, strlen(dbgInfo->debugText));

    if (dbgInfo->showhelp)
    {
         char* helpLines[] = {
            {"F1: Toggle lores/hires"},
            {"F2: Toggle interlaced"},
            {"F3, F4, F5: Color 0 RGB - hold SHIFT for reverse direction"},
            {"F8, F9, F10: Color 1 RGB - hold SHIFT for reverse direction"},
            {"Number keys 1-7: Change image pattern"},
            {"SPACE: Toggle NTSC/PAL"},
            {"ESC: Exit"},
            {"HELP: Toggle help visibility"},
        };
        
        int helpLineCount = 8;
        int startY = 35;
        int positionX = 20;
        int lineSpacing = 10;

        for (int i=0;i<helpLineCount;i++)
        {
            rp->cp_x = positionX;
            rp->cp_y = startY + (i * lineSpacing);
            Text(rp, helpLines[i], strlen(helpLines[i]));
        }
    }
}

int openstuff()
{
    if (!(GfxBase = OpenLibrary ("graphics.library", 0L))) 
    {
        printf("graphics open failed.\n");
        die();
    }
    
    DiskfontBase = OpenLibrary("diskfont.library", 0L);

    KeyIO = AllocMem(sizeof(struct IOStdReq), MEMF_CLEAR);

    if (!OpenDevice("keyboard.device", 0, (struct IORequest*)KeyIO, 0))
    {
        if (KeyIO != NULL)
        {
            keyMatrix = AllocMem(MATRIX_SIZE, MEMF_CHIP|MEMF_CLEAR);
            KeyIO->io_Command = KBD_READMATRIX;
            KeyIO->io_Data = (APTR)keyMatrix;
            KeyIO->io_Length = MATRIX_SIZE;
        }
    }
    
    return 1;
}

closestuff()
{
    if (GfxBase)
    {
        CloseLibrary (GfxBase);
    }
    
    if (DiskfontBase)
    {
        CloseLibrary(DiskfontBase);
    }
    
    if (keyMatrix != NULL)
    {
        CloseDevice((struct IORequest*)KeyIO);
        FreeMem(keyMatrix, MATRIX_SIZE);
    }

    if (KeyIO != NULL)
    {
        FreeMem(KeyIO, sizeof(struct IOStdReq));
        KeyIO = NULL;
    }
}

die()
{
    closestuff();
    exit(-1);
}

int main(void)
{
    SysBase = *((struct Library**)0x00000004);

    printf("Sparkler V1.0 by Bloodmosher\n");

    int copperListSize = 2000;
    g_pCopperList = (UWORD*)AllocMem(copperListSize, MEMF_CHIP|MEMF_CLEAR);
    g_pCopperList2 = (UWORD*)AllocMem(copperListSize, MEMF_CHIP|MEMF_CLEAR);

    openstuff();

    struct View* oldView = GfxBase->ActiView;
    LoadView(NULL);
    WaitTOF();
    WaitTOF();

    g_oldRegs.intena = custom.intenar;
    g_oldRegs.dmacon = custom.dmaconr;

    // Set the default colors, bitmap info, and pattern to values that tend to exhibit sparkling on boards that have the issue
    Globals.r[0] = 0x0;
    Globals.g[0] = 0x0;
    Globals.b[0] = 0x0;

    Globals.r[1] = 0xf;
    Globals.g[1] = 0xb;
    Globals.b[1] = 0xf;
    
    BOOL changeDisplay = TRUE;

    struct DebugInfo dbgInfo;
    dbgInfo.width = 640;
    dbgInfo.height = 200;
    dbgInfo.interlaced = FALSE;
    dbgInfo.hires = TRUE;
    dbgInfo.lineMode = 1;
    dbgInfo.colorOrTextChanged = FALSE;
    dbgInfo.showhelp = TRUE;
    dbgInfo.pal = FALSE;

    createBitmap(320, 200, 1);

    InitRastPort(&g_rp);

    SetAPen(&g_rp, 255L);
    SetBPen(&g_rp, 0L);
    SetDrMd(&g_rp, JAM2);

    struct TextAttr textattr1 = { "helvetica.font", 11, 0, NULL };

    dbgInfo.pFont1 = OpenDiskFont(&textattr1);
    
    if (dbgInfo.pFont1)
    {
        SetFont(&g_rp, dbgInfo.pFont1);
    }

    setupDisplay(FALSE, FALSE, FALSE);
    
    if (((struct ExecBase*)SysBase)->VBlankFrequency == 50)
    {
        dbgInfo.pal = TRUE;
    }
    else
    {
        dbgInfo.pal = FALSE;
    }

    int count = 0;

    while(TRUE) 
    {
        ReadKeyboard();

        if (GetKeyState(0x057)) // F8 "r"
        { 
            ChangeColorValue(&Globals.r[1], &dbgInfo.colorOrTextChanged);
        }

        if (GetKeyState(0x058)) // F9 "g"
        { 
            ChangeColorValue(&Globals.g[1], &dbgInfo.colorOrTextChanged);
        }

        if (GetKeyState(0x059)) // F10 "b"
        {
            ChangeColorValue(&Globals.b[1], &dbgInfo.colorOrTextChanged);
        }

        if (GetKeyState(0x052)) // F3 "r"
        { 
            ChangeColorValue(&Globals.r[0], &dbgInfo.colorOrTextChanged);
        }

        if (GetKeyState(0x053)) // F4 "g"
        { 
            ChangeColorValue(&Globals.g[0], &dbgInfo.colorOrTextChanged);
        }

        if (GetKeyState(0x054)) // F5 "b"
        { 
            ChangeColorValue(&Globals.b[0], &dbgInfo.colorOrTextChanged);
        }

        if (GetKeyState(0x050)) // F1
        { 
            dbgInfo.hires = !dbgInfo.hires;
            changeDisplay = TRUE;
        }

        if (GetKeyState(0x051)) // F2
        { 
            dbgInfo.interlaced = !dbgInfo.interlaced;
            changeDisplay = TRUE;
        }

        if (GetKeyState(0x01)) 
        {
            dbgInfo.lineMode = 1;
            changeDisplay = TRUE;
        }

        if (GetKeyState(0x02)) 
        {
            dbgInfo.lineMode = 2;
            changeDisplay = TRUE;
        }
        
        if (GetKeyState(0x03)) 
        {
            dbgInfo.lineMode = 3;
            changeDisplay = TRUE;
        }

        if (GetKeyState(0x04)) 
        {
            dbgInfo.lineMode = 4;
            changeDisplay = TRUE;
        }

        if (GetKeyState(0x05)) 
        {
            dbgInfo.lineMode = 5;
            changeDisplay = TRUE;
        }

        if (GetKeyState(0x06)) 
        {
            dbgInfo.lineMode = 6;
            changeDisplay = TRUE;
        }
        
        if (GetKeyState(0x07)) 
        {
            dbgInfo.lineMode = 7;
            changeDisplay = TRUE;
        }

        if (GetKeyState(0x40)) 
        {
            dbgInfo.pal = !dbgInfo.pal;
            changeDisplay = TRUE;
        }

        if (GetKeyState(0x5F)) 
        {
            dbgInfo.showhelp = !dbgInfo.showhelp;
            
            if (dbgInfo.showhelp)
            {
                dbgInfo.colorOrTextChanged = TRUE;
            }
            else
            {
                changeDisplay = TRUE;
            }
        }

        if (GetKeyState(0x45)) // ESC - exit
        { 
            break;
        }

        if (dbgInfo.colorOrTextChanged && !changeDisplay)
        {
            WaitTOF();
            int offset = 1; // color00, (write to value)

            g_pCopperList[Globals.copperList1ColorStartIndex + offset] = (UWORD)((Globals.r[0] << 8) | (Globals.g[0] << 4) | Globals.b[0]);
            g_pCopperList2[Globals.copperList2ColorStartIndex + offset] = (UWORD)((Globals.r[0] << 8) | (Globals.g[0] << 4) | Globals.b[0]);

            offset = 3; // color00, value, color1 (2 bytes each), and now write to value (3 because this is a UWORD*)

            g_pCopperList[Globals.copperList1ColorStartIndex + offset] = (UWORD)((Globals.r[1] << 8) | (Globals.g[1] << 4) | Globals.b[1]);
            g_pCopperList2[Globals.copperList2ColorStartIndex + offset] = (UWORD)((Globals.r[1] << 8) | (Globals.g[1] << 4) | Globals.b[1]);

            g_rp.BitMap = g_pBitmap;

            struct RastPort* rp = &g_rp;
            DrawDebugInfo(rp, &dbgInfo);

            dbgInfo.colorOrTextChanged = FALSE;
            Delay();
        }

        if (changeDisplay)
        {
            WaitTOF();
            freeBitmap();
            if (dbgInfo.hires)
            {
                dbgInfo.width = 640;
            }
            else
            {
                dbgInfo.width = 320;
            }
            
            if (dbgInfo.interlaced)
            {
                if (dbgInfo.pal)
                    dbgInfo.height = 512;
                else
                    dbgInfo.height = 400;
            }
            else
            {
                if (dbgInfo.pal)
                    dbgInfo.height = 256;
                else
                    dbgInfo.height = 200;
            }
            
            createBitmap(dbgInfo.width, dbgInfo.height, dbgInfo.lineMode);
            setupDisplay(dbgInfo.hires, dbgInfo.interlaced, dbgInfo.pal);
            changeDisplay = FALSE;

            g_rp.BitMap = g_pBitmap;
            struct RastPort* rp = &g_rp;
            DrawDebugInfo(rp, &dbgInfo);
            Delay();
        }

        // exit it on mouse click
        if ((ciaa.ciapra & CIAF_GAMEPORT0) == 0)
        {
            break;
        }
    }

    // Returning to the system seems happier if not in int erlaced mode
    setupDisplay(FALSE, FALSE, FALSE);

    LoadView(oldView);
    WaitTOF();
    WaitTOF();

    custom.cop1lc = GfxBase->copinit;
    custom.dmacon = g_oldRegs.dmacon | 0x8000;
    custom.intena = g_oldRegs.intena | 0x8000;
    
    freeBitmap();

    if (dbgInfo.pFont1)
    {
        CloseFont(dbgInfo.pFont1);
    }

    FreeMem(g_pCopperList, copperListSize);
    FreeMem(g_pCopperList2, copperListSize);
    
    RethinkDisplay();
    closestuff();
    
    printf("\n");
    return 0;
}