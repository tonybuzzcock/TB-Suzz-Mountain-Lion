// PanelView.mm
// The custom panel for TB Suzz: an original design laid out like the
// Carlsbro Suzz pedal, in its proportions. Opaque silver, black lettering,
// grey knurled knobs with a red position line for GAIN and SUSTAIN (scales
// marked min and max as on the pedal), the treadle with the stomp switch
// (glows red when the effect is in) and the name plate. A strip of
// plugin controls runs along the bottom (TB electronic logo, Input, Output,
// presets and the readout).
//
// Cocoa, manual retain/release, and only APIs that exist in Mac OS X 10.8.
// Everything is drawn in code; no image files.

#import <Cocoa/Cocoa.h>

#include "Editor.h"
#include "Params.h"
#include "Presets.h"
#include "TBLogo.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tbsz;

namespace
{

enum CtlType { kCtlKnob, kCtlFootswitch, kCtlSmallKnob };

struct Ctl
{
    int param;
    int type;
    CGFloat cx, cy, r;
    const char* label;
    const char* posA;      // small knob left
    const char* posB;      // small knob right
};

// Portrait, in the pedal's proportions: top rail, knob section, the treadle
// with the stomp switch, the name plate, then the plugin strip.
const CGFloat kStripTop = 560.0;
const CGFloat kRail = 64.0;                 // bottom of the top rail
const CGFloat kLedge = 262.0;               // knob section / treadle
const CGFloat kTitleY = 104.0;
const NSRect kTreadle = { { 24.0, 274.0 }, { 432.0, 172.0 } };
const NSRect kPlate = { { 40.0, 464.0 }, { 400.0, 80.0 } };

// Main panel, then the plugin strip.
const Ctl kCtls[] = {
    { kGain,      kCtlKnob,      144, 178, 30, "GAIN",       0, 0 },
    { kSustain,   kCtlKnob,      336, 178, 30, "SUSTAIN",    0, 0 },
    { kEffect,    kCtlFootswitch,240, 386, 24, "EFFECT",     0, 0 },
    // Plugin strip
    { kInput,     kCtlSmallKnob, 262, 612, 21, "INPUT",      "-12", "+12" },
    { kOutput,    kCtlSmallKnob, 392, 612, 21, "OUTPUT",     "-24", "+24" },
};

// Preset section in the strip: a SAVE button and slots 1..8, then the readout
const NSRect kSaveRect = { { 40.0, 664.0 }, { 54.0, 26.0 } };
const CGFloat kSlotX0 = 104.0, kSlotPitch = 42.0, kSlotW = 36.0, kSlotY = 664.0, kSlotH = 26.0;
const CGFloat kPresetLabelY = 650.0;
const NSRect kReadoutRect = { { 20.0, 706.0 }, { 440.0, 56.0 } };

inline NSRect slotRect (int k) { return NSMakeRect (kSlotX0 + k * kSlotPitch, kSlotY, kSlotW, kSlotH); }
const int kNumCtls = (int) (sizeof (kCtls) / sizeof (kCtls[0]));

// Pointer angle (radians, 0 = up, clockwise positive) for a 0..1 value.
inline CGFloat valueAngle (CGFloat v) { return (CGFloat) ((-150.0 + 300.0 * v) * M_PI / 180.0); }

// Point at angle a (0 = up, clockwise) and radius r around (cx, cy) in flipped coordinates.
inline NSPoint polar (CGFloat cx, CGFloat cy, CGFloat r, CGFloat a)
{
    return NSMakePoint (cx + r * std::sin (a), cy - r * std::cos (a));
}

NSColor* rgb (int r, int g, int b) { return [NSColor colorWithCalibratedRed: r / 255.0 green: g / 255.0 blue: b / 255.0 alpha: 1.0]; }

NSFont* boldFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica-Bold" size: size];
    return f ? f : [NSFont boldSystemFontOfSize: size];
}

NSFont* obliqueFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica-BoldOblique" size: size];
    return f ? f : boldFont (size);
}

NSFont* plainFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica" size: size];
    return f ? f : [NSFont systemFontOfSize: size];
}

void drawText (const char* s, NSFont* font, NSColor* color, CGFloat cx, CGFloat cy)
{
    NSString* str = [NSString stringWithUTF8String: s];
    NSDictionary* attrs = [NSDictionary dictionaryWithObjectsAndKeys:
                             font, NSFontAttributeName, color, NSForegroundColorAttributeName, nil];
    const NSSize sz = [str sizeWithAttributes: attrs];
    [str drawAtPoint: NSMakePoint (cx - sz.width * 0.5, cy - sz.height * 0.5) withAttributes: attrs];
}

void drawTextLeft (const char* s, NSFont* font, NSColor* color, CGFloat x, CGFloat cy)
{
    NSString* str = [NSString stringWithUTF8String: s];
    NSDictionary* attrs = [NSDictionary dictionaryWithObjectsAndKeys:
                             font, NSFontAttributeName, color, NSForegroundColorAttributeName, nil];
    const NSSize sz = [str sizeWithAttributes: attrs];
    [str drawAtPoint: NSMakePoint (x, cy - sz.height * 0.5) withAttributes: attrs];
}

// Bronze: warm metallic, lighter at the top
NSGradient* bronzeGradient()
{
    return [[[NSGradient alloc] initWithColorsAndLocations:
               rgb (222, 168, 112), 0.0,
               rgb (196, 136, 82), 0.55,
               rgb (168, 110, 62), 1.0, nil] autorelease];
}

// Lettering in the spirit of the Shatter display face: heavy slanted capitals
// sliced by two horizontal cuts into three strips, each knocked sideways.
// Built from Helvetica Bold glyph outlines, so no font file is needed.
void drawShatteredText (const char* s, CGFloat size, CGFloat cx, CGFloat cy)
{
    NSFont* f = boldFont (size);
    NSBezierPath* glyphs = [NSBezierPath bezierPath];
    NSPoint pen = NSMakePoint (0.0, 0.0);
    for (const char* c = s; *c; ++c)
    {
        char name[2] = { *c, 0 };
        const NSGlyph g = [f glyphWithName: [NSString stringWithUTF8String: name]];
        [glyphs moveToPoint: pen];
        [glyphs appendBezierPathWithGlyph: g inFont: f];
        pen.x += [f advancementForGlyph: g].width + size * 0.04;
    }

    // Glyphs come y-up: flip into the panel's coordinates, slant, centre
    NSAffineTransform* t = [NSAffineTransform transform];
    NSAffineTransformStruct m = { 1.12, 0.0, 0.22, -1.0, 0.0, 0.0 };   // a touch wider; x' + 0.22 y: tops lean forward
    [t setTransformStruct: m];
    [glyphs transformUsingAffineTransform: t];
    const NSRect b = [glyphs bounds];
    NSAffineTransform* mv = [NSAffineTransform transform];
    [mv translateXBy: cx - NSMidX (b) yBy: cy - NSMidY (b)];
    [glyphs transformUsingAffineTransform: mv];
    const NSRect r = [glyphs bounds];

    // Fracture lines (fraction of the height at the centre, and their tilt), and
    // how far each strip between them is knocked sideways
    const int kStrips = 3;
    const CGFloat cutAt[kStrips + 1]  = { -0.2, 0.36, 0.64, 1.2 };
    const CGFloat cutTilt[kStrips + 1] = { 0.0, 0.0, 0.0, 0.0 };
    const CGFloat shift[kStrips] = { -3.0, 2.5, -2.0 };
    const CGFloat gap = 1.8;
    const CGFloat x0 = NSMinX (r) - 30.0, x1 = NSMaxX (r) + 30.0, mid = NSMidX (r);

    [[NSColor blackColor] set];
    for (int k = 0; k < kStrips; ++k)
    {
        const CGFloat yt = NSMinY (r) + cutAt[k] * r.size.height + gap;
        const CGFloat yb = NSMinY (r) + cutAt[k + 1] * r.size.height - gap;
        NSBezierPath* band = [NSBezierPath bezierPath];
        [band moveToPoint: NSMakePoint (x0, yt + cutTilt[k] * (x0 - mid))];
        [band lineToPoint: NSMakePoint (x1, yt + cutTilt[k] * (x1 - mid))];
        [band lineToPoint: NSMakePoint (x1, yb + cutTilt[k + 1] * (x1 - mid))];
        [band lineToPoint: NSMakePoint (x0, yb + cutTilt[k + 1] * (x0 - mid))];
        [band closePath];

        [NSGraphicsContext saveGraphicsState];
        [band addClip];
        NSAffineTransform* sh = [NSAffineTransform transform];
        [sh translateXBy: shift[k] yBy: 0.0];
        [[sh transformBezierPath: glyphs] fill];
        [NSGraphicsContext restoreGraphicsState];
    }
}

} // namespace

//==============================================================================
@interface TBSuzzPanelView : NSView
{
    EditorHost* host;
    float shown[kNumParams];
    NSTimer* timer;
    int dragCtl;
    CGFloat dragStartY, dragStartValue;
    int readoutParam;
    bool saveMode;
    int activePreset;
    bool presetUsed[kNumPresetSlots];
    char status[64];
}
- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h;
- (void) shutdown;
@end

@implementation TBSuzzPanelView

- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h
{
    self = [super initWithFrame: frame];
    if (self)
    {
        host = h;
        for (int i = 0; i < kNumParams; ++i) shown[i] = host->getParam (i);
        dragCtl = -1;
        readoutParam = -1;
        saveMode = false;
        activePreset = -1;
        status[0] = 0;
        host->refreshPresets();
        for (int k = 0; k < kNumPresetSlots; ++k) presetUsed[k] = host->presetExists (k);
        timer = [[NSTimer timerWithTimeInterval: 1.0 / 20.0 target: self selector: @selector (tick:)
                                       userInfo: nil repeats: YES] retain];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSDefaultRunLoopMode];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSEventTrackingRunLoopMode];
    }
    return self;
}

- (void) shutdown
{
    if (timer) { [timer invalidate]; [timer release]; timer = nil; }
    host = 0;
}

- (void) dealloc
{
    [self shutdown];
    [super dealloc];
}

- (BOOL) isFlipped { return YES; }
- (BOOL) isOpaque { return YES; }
- (BOOL) acceptsFirstMouse: (NSEvent*) e { (void) e; return YES; }

// Host-side changes (automation, presets) redraw the panel.
- (void) tick: (NSTimer*) t
{
    (void) t;
    if (! host) return;
    bool changed = false;
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = host->getParam (i);
        if (v != shown[i]) { shown[i] = v; changed = true; }
    }
    if (changed) [self setNeedsDisplay: YES];
}

//------------------------------------------------------------------------------
// Drawing

- (void) drawBackground
{
    const NSRect b = [self bounds];

    // Opaque silver faceplate
    NSGradient* face = [[[NSGradient alloc] initWithStartingColor: rgb (222, 224, 227)
                                                      endingColor: rgb (184, 188, 192)] autorelease];
    [face drawInRect: NSMakeRect (0, 0, b.size.width, kStripTop) angle: 90.0];

    // Bronze behind the knob section, as on the pedal
    [bronzeGradient() drawInRect: NSMakeRect (7.0, kRail, b.size.width - 14.0, kLedge - kRail) angle: 90.0];

    // Plugin strip, a slightly darker silver
    NSGradient* strip = [[[NSGradient alloc] initWithStartingColor: rgb (196, 199, 203)
                                                       endingColor: rgb (166, 170, 175)] autorelease];
    [strip drawInRect: NSMakeRect (0, kStripTop, b.size.width, b.size.height - kStripTop) angle: 90.0];

    [rgb (40, 40, 42) set];
    NSRectFill (NSMakeRect (0, kStripTop, b.size.width, 2.0));
    [rgb (238, 240, 242) set];
    NSRectFill (NSMakeRect (0, kStripTop + 2.0, b.size.width, 1.0));

    // Fine border around the faceplate
    NSBezierPath* border = [NSBezierPath bezierPathWithRect: NSInsetRect (NSMakeRect (0, 0, b.size.width, kStripTop), 6.5, 6.5)];
    [rgb (70, 72, 75) set];
    [border setLineWidth: 1.0];
    [border stroke];

    // Ledge lines: under the top rail, and between the knobs and the treadle
    NSColor* ink = [NSColor blackColor];
    const CGFloat ledges[2] = { kRail, kLedge };
    for (int k = 0; k < 2; ++k)
    {
        [rgb (60, 62, 65) set];
        NSRectFill (NSMakeRect (7.0, ledges[k], b.size.width - 14.0, 1.5));
        [rgb (240, 242, 244) set];
        NSRectFill (NSMakeRect (7.0, ledges[k] + 1.5, b.size.width - 14.0, 1.0));
    }

    // Section titles over each knob, on a rule broken by the lettering
    NSFont* tf = boldFont (17.0);
    const CGFloat xs[3] = { 22.0, 240.0, 458.0 };
    for (int k = 0; k < 2; ++k)
    {
        const char* title = k == 0 ? "GAIN" : "SUSTAIN";
        const CGFloat cx = k == 0 ? 144.0 : 336.0;
        NSDictionary* attrs = [NSDictionary dictionaryWithObject: tf forKey: NSFontAttributeName];
        const CGFloat w = [[NSString stringWithUTF8String: title] sizeWithAttributes: attrs].width;
        [rgb (30, 31, 33) set];
        NSRectFill (NSMakeRect (xs[k], kTitleY - 1.5, cx - w * 0.5 - 8.0 - xs[k], 3.0));
        NSRectFill (NSMakeRect (cx + w * 0.5 + 8.0, kTitleY - 1.5, xs[k + 1] - (cx + w * 0.5 + 8.0) - (k == 0 ? 4.0 : 0.0), 3.0));
        drawText (title, tf, ink, cx, kTitleY);
    }

    // Name plate (plain lettering, no maker's artwork)
    NSBezierPath* plate = [NSBezierPath bezierPathWithRect: kPlate];
    [bronzeGradient() drawInBezierPath: plate angle: 90.0];
    [rgb (80, 82, 85) set];
    [plate setLineWidth: 1.0];
    [plate stroke];
    drawShatteredText ("SUZZ", 74.0, NSMidX (kPlate), NSMidY (kPlate) + 1.0);

    // The TB electronic logo in the strip's corner, straight on the silver
    tblogo::drawTBLogo (NSMakeRect (16.0, 570.0, 130.0, 72.0));
}

- (void) drawScaleForCtl: (const Ctl&) c
{
    NSColor* ink = [NSColor blackColor];
    for (int i = 0; i < 11; ++i)
    {
        const CGFloat a = valueAngle (i / 10.0);
        NSBezierPath* t = [NSBezierPath bezierPath];
        [t moveToPoint: polar (c.cx, c.cy, c.r + 4.0, a)];
        [t lineToPoint: polar (c.cx, c.cy, c.r + 9.0, a)];
        [t setLineWidth: 1.2];
        [ink set];
        [t stroke];

        // The pedal marks only the ends: min and max
        if (i == 0 || i == 10)
        {
            const NSPoint p = polar (c.cx, c.cy, c.r + 24.0, a);
            drawText (i == 0 ? "min" : "max", obliqueFont (13.0), ink, p.x, p.y);
        }
    }
}

// Grey knurled knob with a red position line.
- (void) drawKnobAt: (NSPoint) ctr radius: (CGFloat) r value: (CGFloat) v
{
    // Drop shadow
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (1.5, -2.5)];
    [sh setShadowBlurRadius: 4.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.45]];
    [sh set];
    [rgb (90, 93, 97) set];
    [[NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - r, ctr.y - r, 2 * r, 2 * r)] fill];
    [NSGraphicsContext restoreGraphicsState];

    // Knurled skirt: alternating ridges around the rim
    const int ridges = 40;
    for (int k = 0; k < ridges; ++k)
    {
        const CGFloat a0 = (CGFloat) (2.0 * M_PI * k / ridges);
        const CGFloat a1 = (CGFloat) (2.0 * M_PI * (k + 0.5) / ridges);
        NSBezierPath* w = [NSBezierPath bezierPath];
        [w moveToPoint: ctr];
        [w lineToPoint: polar (ctr.x, ctr.y, r, a0)];
        [w lineToPoint: polar (ctr.x, ctr.y, r, a1)];
        [w closePath];
        // light from the top left
        const CGFloat lit = 0.5 + 0.5 * std::cos (a0 + 0.8);
        [[NSColor colorWithCalibratedWhite: 0.40 + 0.22 * lit alpha: 1.0] set];
        [w fill];
    }

    // Top cap
    const CGFloat rc = r * 0.78;
    NSGradient* cap = [[[NSGradient alloc] initWithStartingColor: rgb (172, 175, 179)
                                                     endingColor: rgb (104, 107, 111)] autorelease];
    NSBezierPath* capPath = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - rc, ctr.y - rc, 2 * rc, 2 * rc)];
    [cap drawInBezierPath: capPath angle: -60.0];
    [rgb (70, 72, 75) set];
    [capPath setLineWidth: 0.8];
    [capPath stroke];

    // Red position line
    const CGFloat a = valueAngle (v);
    NSBezierPath* line = [NSBezierPath bezierPath];
    [line moveToPoint: polar (ctr.x, ctr.y, r * 0.18, a)];
    [line lineToPoint: polar (ctr.x, ctr.y, r * 0.98, a)];
    [line setLineWidth: r > 18.0 ? 3.0 : 2.4];
    [line setLineCapStyle: NSRoundLineCapStyle];
    [rgb (200, 30, 30) set];
    [line stroke];
}

// The treadle the stomp switch sits in: plain charcoal, a rounded top edge.
- (void) drawGrille
{
    const NSRect g = kTreadle;
    NSBezierPath* body = [NSBezierPath bezierPathWithRoundedRect: g xRadius: 6.0 yRadius: 6.0];
    NSGradient* base = [[[NSGradient alloc] initWithStartingColor: rgb (58, 59, 62) endingColor: rgb (30, 31, 33)] autorelease];
    [base drawInBezierPath: body angle: 90.0];
    // Light catching the rounded front edge
    NSGradient* edge = [[[NSGradient alloc] initWithStartingColor: rgb (110, 112, 116) endingColor: rgb (58, 59, 62)] autorelease];
    [edge drawInRect: NSMakeRect (NSMinX (g) + 4.0, NSMinY (g) + 1.0, g.size.width - 8.0, 10.0) angle: 90.0];
    [rgb (20, 20, 22) set];
    [body setLineWidth: 1.0];
    [body stroke];
}

// Chrome stomp switch: click to toggle.
- (void) drawFootswitch: (const Ctl&) c
{
    const NSPoint ctr = NSMakePoint (c.cx, c.cy);
    const bool on = isOn (shown[kEffect]);
    // Red glow around the switch with the effect in
    if (on)
    {
        const CGFloat gr = c.r + 30.0;
        NSGradient* halo = [[[NSGradient alloc] initWithColorsAndLocations:
                               [NSColor colorWithCalibratedRed: 1.0 green: 0.16 blue: 0.10 alpha: 0.95], 0.0,
                               [NSColor colorWithCalibratedRed: 1.0 green: 0.10 blue: 0.06 alpha: 0.55], 0.45,
                               [NSColor colorWithCalibratedRed: 1.0 green: 0.05 blue: 0.02 alpha: 0.0], 1.0, nil] autorelease];
        [halo drawInBezierPath: [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - gr, ctr.y - gr, 2 * gr, 2 * gr)]
              relativeCenterPosition: NSMakePoint (0.0, 0.0)];
    }
    // Hex nut
    NSBezierPath* nut = [NSBezierPath bezierPath];
    for (int k = 0; k < 6; ++k)
    {
        const NSPoint p = polar (ctr.x, ctr.y, c.r + 9.0, (CGFloat) (M_PI / 6.0 + k * M_PI / 3.0));
        if (k == 0) [nut moveToPoint: p]; else [nut lineToPoint: p];
    }
    [nut closePath];
    NSGradient* ng = [[[NSGradient alloc] initWithStartingColor: rgb (226, 228, 231) endingColor: rgb (118, 121, 125)] autorelease];
    [ng drawInBezierPath: nut angle: -60.0];
    [rgb (60, 62, 65) set];
    [nut setLineWidth: 0.8];
    [nut stroke];

    // Plunger: a dome, pressed in a touch and lit red when the effect is in
    const CGFloat r = on ? c.r - 1.5 : c.r;
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (1.0, on ? -1.0 : -2.5)];
    [sh setShadowBlurRadius: on ? 2.0 : 4.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.5]];
    [sh set];
    NSBezierPath* dome = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - r, ctr.y - r, 2 * r, 2 * r)];
    [rgb (150, 153, 157) set];
    [dome fill];
    [NSGraphicsContext restoreGraphicsState];
    NSGradient* dg = on
        ? [[[NSGradient alloc] initWithStartingColor: rgb (255, 214, 200) endingColor: rgb (200, 22, 16)] autorelease]
        : [[[NSGradient alloc] initWithStartingColor: rgb (250, 251, 252) endingColor: rgb (96, 99, 104)] autorelease];
    [dg drawInBezierPath: dome relativeCenterPosition: NSMakePoint (-0.35, 0.4)];
    [rgb (55, 57, 60) set];
    [dome setLineWidth: 1.0];
    [dome stroke];
}

- (void) drawReadout
{
    const NSRect box = kReadoutRect;
    NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect: box xRadius: 4 yRadius: 4];
    // Fluorescent lime green, like an E.L. panel, glowing brighter at the centre
    NSGradient* glow = [[[NSGradient alloc] initWithStartingColor: rgb (206, 255, 120)
                                                      endingColor: rgb (150, 232, 70)] autorelease];
    [glow drawInBezierPath: p relativeCenterPosition: NSMakePoint (0.0, 0.0)];
    [rgb (60, 62, 65) set];
    [p setLineWidth: 1.0];
    [p stroke];

    if (status[0])
    {
        drawText (status, boldFont (16.0), [NSColor blackColor], NSMidX (box), NSMidY (box));
    }
    else if (readoutParam >= 0 && host)
    {
        char disp[16] = { 0 };
        host->getParamDisplay (readoutParam, disp);
        char text[96];
        const char* name = 0;
        for (int i = 0; i < kNumCtls; ++i)
            if (kCtls[i].param == readoutParam) { name = kCtls[i].label; break; }
        const int kind = kParamKind[readoutParam];
        const char* unit = (kind == kDb12 || kind == kDb24) ? " dB" : "";
        std::snprintf (text, sizeof (text), "%s   %s%s", name ? name : "", disp, unit);
        drawText (text, boldFont (16.0), [NSColor blackColor], NSMidX (box), NSMidY (box));
    }
}

- (void) drawPresets
{
    NSColor* ink = [NSColor blackColor];
    drawText ("PRESETS", boldFont (10.0), ink, (NSMinX (kSaveRect) + NSMaxX (slotRect (kNumPresetSlots - 1))) * 0.5, kPresetLabelY);

    // SAVE button: lit red while it is waiting for a slot
    NSBezierPath* sp = [NSBezierPath bezierPathWithRoundedRect: kSaveRect xRadius: 3 yRadius: 3];
    if (saveMode)
    {
        [rgb (200, 30, 30) set];
        [sp fill];
    }
    else
    {
        NSGradient* g = [[[NSGradient alloc] initWithStartingColor: rgb (232, 234, 236) endingColor: rgb (160, 163, 167)] autorelease];
        [g drawInBezierPath: sp angle: -90.0];
    }
    [rgb (40, 41, 43) set];
    [sp setLineWidth: 1.0];
    [sp stroke];
    drawText ("SAVE", boldFont (11.0), saveMode ? [NSColor whiteColor] : ink, NSMidX (kSaveRect), NSMidY (kSaveRect));

    // Slots: empty = silver, saved = charcoal, in use = lime green
    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        const NSRect r = slotRect (k);
        NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect: r xRadius: 3 yRadius: 3];
        NSColor* fill = (k == activePreset) ? rgb (178, 245, 96) : (presetUsed[k] ? rgb (46, 47, 50) : rgb (214, 216, 219));
        NSColor* text = (k == activePreset) ? ink : (presetUsed[k] ? [NSColor whiteColor] : rgb (120, 122, 126));
        [fill set];
        [bp fill];
        [rgb (40, 41, 43) set];
        [bp setLineWidth: 1.0];
        [bp stroke];
        char num[4];
        std::snprintf (num, sizeof (num), "%d", k + 1);
        drawText (num, boldFont (12.0), text, NSMidX (r), NSMidY (r));
    }
}

- (void) drawRect: (NSRect) dirty
{
    (void) dirty;
    [self drawBackground];
    [self drawGrille];
    NSColor* ink = [NSColor blackColor];

    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        switch (c.type)
        {
            case kCtlKnob:
                [self drawScaleForCtl: c];
                [self drawKnobAt: NSMakePoint (c.cx, c.cy) radius: c.r value: shown[c.param]];
                break;
            case kCtlFootswitch:
                [self drawFootswitch: c];
                break;
            case kCtlSmallKnob:
            {
                [self drawKnobAt: NSMakePoint (c.cx, c.cy) radius: c.r value: shown[c.param]];
                drawText (c.label, boldFont (11.0), ink, c.cx, c.cy - c.r - 13.0);
                drawText (c.posA, plainFont (9.5), ink, c.cx - c.r - 17.0, c.cy + c.r - 3.0);
                drawText (c.posB, plainFont (9.5), ink, c.cx + c.r + 17.0, c.cy + c.r - 3.0);
                break;
            }
            default: break;
        }
    }
    [self drawPresets];
    [self drawReadout];
}

//------------------------------------------------------------------------------
// Mouse

- (int) ctlAtPoint: (NSPoint) p
{
    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        {
            const CGFloat dx = p.x - c.cx, dy = p.y - c.cy;
            if (dx * dx + dy * dy < (c.r + 8) * (c.r + 8)) return i;
        }
    }
    return -1;
}

- (void) setParam: (int) param value: (float) v
{
    if (! host) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    host->guiSetParam (param, v);
    shown[param] = host->getParam (param);
    readoutParam = param;
    [self setNeedsDisplay: YES];
}

// SAVE button and preset slots. Returns YES if the click was handled here.
- (BOOL) presetClick: (NSPoint) p
{
    if (NSPointInRect (p, kSaveRect))
    {
        saveMode = ! saveMode;
        std::snprintf (status, sizeof (status), "%s", saveMode ? "SAVE: CLICK A SLOT 1-8" : "");
        [self setNeedsDisplay: YES];
        return YES;
    }
    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        if (! NSPointInRect (p, slotRect (k))) continue;
        if (saveMode)
        {
            const bool ok = host->savePreset (k);
            saveMode = false;
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (ok) activePreset = k;
            std::snprintf (status, sizeof (status), ok ? "PRESET %d SAVED" : "COULD NOT SAVE PRESET %d", k + 1);
        }
        else
        {
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (presetUsed[k] && host->loadPreset (k))
            {
                for (int j = 0; j < kNumParams; ++j) shown[j] = host->getParam (j);
                activePreset = k;
                std::snprintf (status, sizeof (status), "PRESET %d LOADED", k + 1);
            }
            else
                std::snprintf (status, sizeof (status), "PRESET %d IS EMPTY", k + 1);
        }
        [self setNeedsDisplay: YES];
        return YES;
    }
    return NO;
}

- (void) mouseDown: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    if (! host) return;
    if ([self presetClick: p]) return;
    const int i = [self ctlAtPoint: p];
    if (i < 0) return;
    const Ctl& c = kCtls[i];
    shown[c.param] = host->getParam (c.param);   // the live value, even between timer ticks
    readoutParam = c.param;
    status[0] = 0;
    activePreset = -1;     // settings are being changed away from the preset

    if (c.type == kCtlFootswitch)
    {
        // Each press toggles, like the pedal's latching switch
        host->guiBeginEdit (c.param);
        [self setParam: c.param value: isOn (shown[c.param]) ? 0.0f : 1.0f];
        host->guiEndEdit (c.param);
        return;
    }

    host->guiBeginEdit (c.param);
    if ([e clickCount] == 2)
        [self setParam: c.param value: kDefaults[c.param]];   // double-click: back to default
    dragCtl = i;
    dragStartY = p.y;
    dragStartValue = shown[c.param];
    [self setNeedsDisplay: YES];
}

- (void) mouseDragged: (NSEvent*) e
{
    if (dragCtl < 0 || ! host) return;
    const Ctl& c = kCtls[dragCtl];
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    const bool fine = ([e modifierFlags] & NSShiftKeyMask) != 0;
    const CGFloat perPixel = fine ? 1.0 / 1000.0 : 1.0 / 200.0;
    const CGFloat v = dragStartValue + (dragStartY - p.y) * perPixel;
    [self setParam: c.param value: (float) v];
}

- (void) mouseUp: (NSEvent*) e
{
    (void) e;
    if (dragCtl >= 0 && host) host->guiEndEdit (kCtls[dragCtl].param);
    dragCtl = -1;
}

- (void) scrollWheel: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    const int i = [self ctlAtPoint: p];
    if (i < 0 || ! host) return;
    const Ctl& c = kCtls[i];
    if (c.type != kCtlKnob && c.type != kCtlSmallKnob) return;
    const CGFloat d = [e deltaY];
    if (d == 0.0) return;
    status[0] = 0;
    activePreset = -1;
    host->guiBeginEdit (c.param);
    [self setParam: c.param value: (float) (shown[c.param] + d * 0.01)];
    host->guiEndEdit (c.param);
}

@end

//==============================================================================
namespace tbsz
{

// Both calls use their own autorelease pool, so nothing of this plugin's is
// left waiting in the host's pool when the editor closes or the plugin unloads.
void* editorOpen (void* parentView, EditorHost* host)
{
    NSView* parent = (NSView*) parentView;
    if (! parent || ! host) return 0;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    TBSuzzPanelView* v = [[TBSuzzPanelView alloc]
                                  initWithFrame: NSMakeRect (0, 0, kEditorWidth, kEditorHeight) host: host];
    if (v) [parent addSubview: v];
    [pool release];
    return (void*) v;    // retained until editorClose
}

void editorClose (void* handle)
{
    TBSuzzPanelView* v = (TBSuzzPanelView*) handle;
    if (! v) return;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [v shutdown];
    [v removeFromSuperview];
    [v release];
    [pool release];
}

} // namespace tbsz
