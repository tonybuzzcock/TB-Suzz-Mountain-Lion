// Params.h
// Parameter table shared by the VST wrapper, the editor and the preset store.
//
// Front-panel controls first (GAIN, SUSTAIN and the S1 footswitch), then the
// plugin-only controls in the editor's bottom strip.

#ifndef TBSZ_PARAMS_H
#define TBSZ_PARAMS_H

namespace tbsz
{

enum Param
{
    // Front panel
    kGain = 0, kSustain, kEffect,
    // Plugin only (bottom strip of the editor)
    kGtrVol, kInput, kOutput,
    kNumParams
};

enum Kind { kKnob, kSwitch, kDb12, kDb24 };

const char* const kParamNames[kNumParams] = {
    "Gain", "Sustain", "Effect",
    "GtrVol", "Input", "Output"
};

const int kParamKind[kNumParams] = {
    kKnob, kKnob, kSwitch,
    kKnob, kDb12, kDb24
};

const float kDefaults[kNumParams] = {
    0.85f, 0.75f, 1.0f,
    1.0f, 0.5f, 0.5f
};

const double kInputRangeDb  = 12.0;
const double kOutputRangeDb = 24.0;

inline bool isOn (float v) { return v >= 0.5f; }

} // namespace tbsz

#endif
