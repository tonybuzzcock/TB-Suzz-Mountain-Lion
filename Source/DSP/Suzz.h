// Suzz.h
// Carlsbro Suzz fuzz pedal (drawing by Gottfried Divos, version 1.0,
// 13.12.2006), modelled from the schematic:
//
//   guitar (pickup, 250K volume pot on full, cable) -> S1a -> C1 10n
//   -> IC1 pin 3 (CA741CE), biased at half the 9 V battery by R1 / R2 120K
//   IC1 non-inverting: R3 470K || C3 47p from pin 6 to pin 2,
//      pin 2 -> R4 470R -> C2 1u -> P1 SUSTAIN (22K reverse log, rheostat)
//      to ground. Gain = 1 + 470K / (470R + P1): about x22 (27 dB) at min,
//      about x1000 (60 dB) at max, falling below a few hundred Hz (C2).
//   -> C4 4u7 -> R5 100K to ground -> R6 3K3 -> C5 220n to ground
//   -> P2 GAIN 470R to ground; its wiper -> S1b -> output
//   S1 (DPDT footswitch): effect in, or input straight to output.
//
// Modelled as a circuit: one netlist with every part above, the 741 as a
// 1 MHz integrator with 0.5 V/us slew and a soft-kneed output swing that is
// a little different above and below the bias point (single 9 V supply).
// While the 741 is clipping, the rest of the circuit (C2, C3, the output
// filter) sees the clipped output, as in the pedal.
// Estimates (not fixed by the drawing): the 741's swing on 9 V, the pickup,
// cable and guitar volume pot (on full), and P2's taper. All are named constants in
// Suzz.cpp.
//
// Volts in, volts out (1.0 = 1 V). Runs at 4x with Oversampler4x.
// Plain C++98, no allocation after construction.

#ifndef TBSZ_SUZZ_H
#define TBSZ_SUZZ_H

#include "Netlist.h"
#include "Oversampler4x.h"

namespace tbsz
{

class Suzz
{
public:
    Suzz();

    void prepare (double sampleRate);
    void reset();

    // Front panel (0..1 pot rotation)
    void setGain (double v);            // P2 470R: output level
    void setSustain (double v);         // P1 22K rev log: IC1's gain
    void setEffectOn (bool on);         // S1 footswitch
    // Plugin only
    void setInputGainDb (double dB);
    void setOutputGainDb (double dB);

    // Mono in, mono out
    void process (const float* in, float* out, int numSamples);

    static int latencySamples() { return Oversampler4x::latencySamples(); }

    // Introspection for tests
    static double sustainOhms (double sustainPot);     // P1 in circuit
    static double ic1Gain (double sustainPot);         // mid-band, 1 + R3 / (R4 + P1)
    static double gainTaper (double gainPot);          // P2 wiper fraction
    static double biasVolts();                         // IC1 pin 3 / pin 6 DC
    static double swingUp();                           // above the bias point
    static double swingDown();                         // below it
    double lastIc1Out() const { return ic1Out; }       // volts relative to bias
    bool   ic1Clipping() const { return net.opAmpLimited(); }

    static double audioTaper (double x);

private:
    void buildCircuit();
    void applyPots();
    double tickOs (double x);

    double fsBase, fsOs;
    Oversampler4x up, down;
    Netlist net;
    int nOut, nIc1;
    int eP1;

    // Targets and smoothed values
    double gainT, sustT, effectT, inT, outT;
    double gain, sust, effect, inG, outG;
    double sustApplied;
    double smKnob, smSwitch;
    int potCountdown;
    double ic1Out;
};

} // namespace tbsz

#endif
