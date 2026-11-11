// Suzz.cpp
// See Suzz.h for the signal path. Component values are from the drawing;
// the estimates (things the drawing doesn't fix) are marked.

#include "Suzz.h"

#include <cmath>

namespace tbsz
{

namespace
{
    // --- From the drawing --------------------------------------------------
    const double kBattery = 9.0;                       // BAT1
    const double kR1 = 120.0e3, kR2 = 120.0e3;         // bias divider for pin 3
    const double kC1 = 10.0e-9;                        // input coupling
    const double kR3 = 470.0e3, kC3 = 47.0e-12;        // feedback (C3 styroflex)
    const double kR4 = 470.0, kC2 = 1.0e-6;            // to SUSTAIN
    const double kP1 = 22.0e3;                         // SUSTAIN, reverse log, rheostat
    const double kC4 = 4.7e-6, kR5 = 100.0e3;          // output coupling
    const double kR6 = 3.3e3, kC5 = 220.0e-9;          // output low-pass
    const double kP2 = 470.0;                          // GAIN ("really only 470 Ohm")

    // CA741CE
    const double kGbw741  = 1.0e6;     // Hz
    const double kSlew741 = 0.5;       // V/us

    // --- Estimates (not fixed by the drawing) --------------------------------
    // 741 output swing on a single 9 V battery into about 3K6, measured from
    // the 4.5 V bias point: it gets to within about 1.5 V of the top rail and
    // about 1.9 V of the bottom one, rounding off over the last 0.4 V.
    const double kSwingUp   = 3.0;     // V
    const double kSwingDown = 2.6;     // V
    const double kKnee741   = 0.4;     // V
    // The guitar: pickup resistance, its 250K volume pot (on full) and the cable
    const double kPickupR = 6000.0;
    const double kVolPot  = 250.0e3;
    const double kCable   = 470.0e-12;
    // P2's taper: an audio (log) track assumed
    const bool kGainIsLog = true;

    inline double clamp01 (double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }
    inline double potR (double r) { return r < 1.0 ? 1.0 : r; }
}

//==============================================================================
double Suzz::audioTaper (double x)
{
    const double k = 4.394449154672439;    // 10% of the track at mid-rotation
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    return (std::exp (k * x) - 1.0) / (std::exp (k) - 1.0);
}

// Reverse log track: turning clockwise, the resistance in circuit falls fast
// at first, so IC1's gain climbs fairly evenly in dB across the rotation.
double Suzz::sustainOhms (double pot) { return kP1 * audioTaper (1.0 - clamp01 (pot)); }
double Suzz::ic1Gain (double pot)     { return 1.0 + kR3 / (kR4 + sustainOhms (pot)); }
double Suzz::gainTaper (double pot)   { return kGainIsLog ? audioTaper (clamp01 (pot)) : clamp01 (pot); }
double Suzz::biasVolts()              { return kBattery * kR2 / (kR1 + kR2); }
double Suzz::swingUp()                { return kSwingUp; }
double Suzz::swingDown()              { return kSwingDown; }

//==============================================================================
Suzz::Suzz()
    : fsBase (44100.0), fsOs (176400.0),
      nOut (0), nIc1 (0), eP1 (-1),
      gainT (0.85), sustT (0.75), effectT (1.0), inT (1.0), outT (1.0),
      sustApplied (-1.0), potCountdown (0), ic1Out (0.0)
{
    buildCircuit();
    prepare (44100.0);
}

void Suzz::buildCircuit()
{
    // AC circuit: every node is measured from its DC bias (the battery and
    // pin 3's bias are AC ground), so zero state is the DC solution.
    Netlist& n = net;
    n.clear();
    const int w  = n.addNode();     // guitar: pickup, volume pot (full), cable, jack
    const int p3 = n.addNode();     // IC1 pin 3
    const int p2 = n.addNode();     // IC1 pin 2
    const int a  = n.addNode();     // R4 / C2
    const int b  = n.addNode();     // C2 / P1
    const int o  = n.addNode();     // IC1 pin 6
    const int c  = n.addNode();     // C4 / R5 / R6
    const int d  = n.addNode();     // R6 / C5 / P2 (top of GAIN)

    n.addInput (w, kPickupR);
    n.addR (w, 0, kVolPot);
    n.addC (w, 0, kCable);
    n.addC (w, p3, kC1);
    n.addR (p3, 0, kR1);            // R1 to +9 V (AC ground)
    n.addR (p3, 0, kR2);
    n.addR (o, p2, kR3);
    n.addC (o, p2, kC3);
    n.addR (p2, a, kR4);
    n.addC (a, b, kC2);
    eP1 = n.addR (b, 0, kP1);
    n.addC (o, c, kC4);
    n.addR (c, 0, kR5);
    n.addR (c, d, kR6);
    n.addC (d, 0, kC5);
    n.addR (d, 0, kP2);
    n.setOpAmp (p3, p2, o, kGbw741, kSlew741, kSwingUp, kKnee741);
    n.setOpAmpSwing (kSwingUp, kSwingDown);
    nOut = d;
    nIc1 = o;
}

void Suzz::prepare (double sampleRate)
{
    fsBase = sampleRate > 1000.0 ? sampleRate : 44100.0;
    fsOs = fsBase * Oversampler4x::kFactor;

    smKnob   = std::exp (-1.0 / (0.020 * fsBase));
    smSwitch = std::exp (-1.0 / (0.006 * fsBase));

    gain = gainT; sust = sustT; effect = effectT; inG = inT; outG = outT;
    sustApplied = -1.0;
    applyPots();
    net.prepare (fsOs);
    reset();
}

void Suzz::reset()
{
    gain = gainT; sust = sustT; effect = effectT; inG = inT; outG = outT;
    applyPots();
    net.rebuild();
    net.reset();
    up.reset(); down.reset();
    potCountdown = 0;
    ic1Out = 0.0;
}

void Suzz::applyPots()
{
    net.setR (eP1, potR (sustainOhms (sust)));
    sustApplied = sust;
}

void Suzz::setGain (double v)           { gainT = clamp01 (v); }
void Suzz::setSustain (double v)        { sustT = clamp01 (v); }
void Suzz::setEffectOn (bool on)        { effectT = on ? 1.0 : 0.0; }
void Suzz::setInputGainDb (double dB)   { inT = std::pow (10.0, dB / 20.0); }
void Suzz::setOutputGainDb (double dB)  { outT = std::pow (10.0, dB / 20.0); }

//==============================================================================
double Suzz::tickOs (double x)
{
    net.process (&x);
    ic1Out = net.v (nIc1);
    return net.v (nOut);
}

void Suzz::process (const float* in, float* out, int numSamples)
{
    const double aK = 1.0 - smKnob, aW = 1.0 - smSwitch;
    double xs[Oversampler4x::kFactor];

    for (int n = 0; n < numSamples; ++n)
    {
        gain += (gainT - gain) * aK;
        sust += (sustT - sust) * aK;
        effect += (effectT - effect) * aW;
        inG += (inT - inG) * aK;
        outG += (outT - outG) * aK;

        // SUSTAIN changes the circuit: re-solve every 16 samples while it moves,
        // keeping the capacitor charges (no clicks)
        if (--potCountdown <= 0)
        {
            potCountdown = 16;
            if (std::fabs (sust - sustApplied) > 1.0e-5)
            {
                applyPots();
                net.rebuild();
            }
        }

        float a = in[n];
        if (! (a == a)) a = 0.0f;
        up.upsample ((double) a, xs);
        const double wiper = gainTaper (gain);
        for (int k = 0; k < Oversampler4x::kFactor; ++k)
        {
            const double wet = tickOs (xs[k] * inG) * wiper;
            // S1: effect in, or the input jack straight to the output jack
            xs[k] = effect * wet + (1.0 - effect) * xs[k];
        }
        out[n] = (float) (down.downsample (xs) * outG);
    }
}

} // namespace tbsz
