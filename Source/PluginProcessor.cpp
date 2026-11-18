#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetLibrary.h"
#include <cmath>
#include <cstdlib>
#include <set>
#include <algorithm>

namespace {
struct Preset { const char* name; std::initializer_list<std::pair<const char*,float>> values; };
const std::array<Preset,28> presets {{
    {"Init / Pure Canvas", {}},
    {"Keys / Glass Garden", {{"osc1Position",.08f},{"osc2Level",.24f},{"osc2Position",.24f},{"osc2Semi",12},{"ampAttack",.003f},{"ampDecay",1.2f},{"ampSustain",.12f},{"ampRelease",1.1f},{"cutoff",7500},{"filterEnv",.25f},{"reverbMix",.25f},{"delayMix",.14f}}},
    {"Pad / Northern Lights", {{"osc1Position",.62f},{"osc1Unison",5},{"osc1Detune",17},{"osc2Level",.38f},{"osc2Position",.38f},{"osc2Semi",12},{"osc2Unison",3},{"ampAttack",1.6f},{"ampDecay",1.9f},{"ampSustain",.7f},{"ampRelease",2.8f},{"cutoff",2400},{"filterEnv",.2f},{"chorusMix",.35f},{"reverbMix",.4f},{"reverbSize",.8f},{"mod1Source",1},{"mod1Dest",2},{"mod1Amount",.2f},{"lfo1Rate",.13f}}},
    {"Bass / Rubber Circuit", {{"osc1Position",.72f},{"osc2Level",.27f},{"osc2Position",.85f},{"osc2Semi",-12},{"subLevel",.4f},{"cutoff",480},{"resonance",.48f},{"filterEnv",.65f},{"envAttack",.003f},{"envDecay",.23f},{"envSustain",.05f},{"ampAttack",.002f},{"ampDecay",.2f},{"ampSustain",.6f},{"ampRelease",.12f},{"drive",.35f}}},
    {"Lead / Neon Skyline", {{"osc1Position",.66f},{"osc1Unison",7},{"osc1Detune",19},{"osc2Position",.66f},{"osc2Level",.3f},{"osc2Unison",3},{"osc2Semi",12},{"cutoff",6500},{"ampAttack",.008f},{"ampRelease",.25f},{"delayMix",.24f},{"delayTime",.375f},{"reverbMix",.18f}}},
    {"Pluck / Amber Drop", {{"osc1Position",.5f},{"osc1Unison",3},{"osc1Detune",10},{"cutoff",650},{"filterEnv",.75f},{"envAttack",.002f},{"envDecay",.32f},{"envSustain",0},{"ampAttack",.002f},{"ampDecay",.55f},{"ampSustain",0},{"ampRelease",.35f},{"delayMix",.3f},{"delayTime",.375f},{"reverbMix",.22f}}},
    {"Motion / Tide Machine", {{"osc1Position",.68f},{"osc1Unison",3},{"osc2Position",.95f},{"osc2Level",.3f},{"osc2Semi",7},{"cutoff",1500},{"resonance",.4f},{"mod1Source",1},{"mod1Dest",1},{"mod1Amount",.6f},{"mod2Source",2},{"mod2Dest",2},{"mod2Amount",.35f},{"lfo1Rate",2},{"lfo2Rate",.27f},{"lfo1Shape",1},{"ampRelease",.6f},{"reverbMix",.25f}}},
    {"Arp / Pixel Rain", {{"osc1Position",.3f},{"osc2Level",.2f},{"osc2Semi",12},{"ampDecay",.22f},{"ampSustain",.15f},{"ampRelease",.15f},{"arpEnabled",1},{"arpRate",2},{"arpOctaves",2},{"cutoff",4500},{"delayMix",.3f},{"delayTime",.375f},{"reverbMix",.2f}}},
    {"Bass / Sub Foundation", {{"osc1Position",0},{"osc1Level",.35f},{"subLevel",.7f},{"cutoff",1300},{"ampAttack",.005f},{"ampRelease",.14f},{"drive",.18f}}},
    {"Texture / Stardust", {{"osc1Position",.15f},{"osc1Unison",3},{"osc2Level",.3f},{"osc2Position",.3f},{"osc2Semi",19},{"osc3Level",.15f},{"osc3Position",.1f},{"osc3Semi",24},{"noiseLevel",.08f},{"filterType",1},{"cutoff",800},{"ampAttack",.8f},{"ampRelease",3},{"chorusMix",.4f},{"reverbMix",.5f},{"reverbSize",.9f},{"mod1Source",2},{"mod1Dest",7},{"mod1Amount",.6f},{"lfo2Rate",.2f}}},
    {"Sample / Copper Bell", {{"osc1Mode",1},{"osc1Position",0},{"subLevel",0},{"ampAttack",.001f},{"ampSustain",1},{"ampRelease",1.4f},{"reverbMix",.2f},{"delayMix",.12f}}},
    {"Granular / Cloud Bell", {{"osc1Mode",2},{"osc1Position",.2f},{"osc1GrainSize",.18f},{"osc1GrainDensity",25},{"osc1GrainSpray",.15f},{"osc1Unison",3},{"subLevel",0},{"ampAttack",.4f},{"ampSustain",1},{"ampRelease",1.4f},{"reverbMix",.35f},{"mod1Source",1},{"mod1Dest",2},{"mod1Amount",.15f},{"lfo1Rate",.12f}}},
    {"Motion / Painted Steps", {{"osc1Position",.65f},{"osc1Unison",3},{"osc1Detune",9},{"cutoff",950},{"resonance",.3f},{"ampAttack",.01f},{"ampSustain",.8f},{"ampRelease",.3f},{"lfo1Sync",1},{"lfo1Division",2},{"lfo1Kind",2},{"lfo1Mode",1},{"lfo1Interpolation",1},{"mod1Source",1},{"mod1Dest",1},{"mod1Amount",.65f},{"delayMix",.15f}}},
    {"Motion / Dual Current", {{"osc1Position",.66f},{"osc1Unison",3},{"osc1Detune",11},{"osc2Position",.3f},{"osc2Level",.23f},{"osc2Semi",7},{"subLevel",.24f},{"subFilterRoute",3},{"filterType",1},{"cutoff",180},{"resonance",.2f},{"filter2Enabled",1},{"filter2Cutoff",3400},{"filter2Resonance",.3f},{"filter2Slope",1},{"filter2Env",.4f},{"envAttack",.12f},{"envDecay",.7f},{"envSustain",.2f},{"ampAttack",.04f},{"ampSustain",.75f},{"ampRelease",.7f},{"lfo1Sync",1},{"lfo1Division",2},{"mod1Source",1},{"mod1DestBank",1},{"mod1FilterTarget",0},{"mod1Amount",.3f},{"mod2Source",6},{"mod2DestBank",1},{"mod2FilterTarget",2},{"mod2Amount",.35f},{"chorusMix",.12f},{"delayMix",.12f},{"reverbMix",.15f}}},
    {"Motion / Folded Orbit", {{"osc1Position",.12f},{"osc1Unison",3},{"osc1Detune",8},{"osc1Warp1Mode",1},{"osc1Warp1Amount",.28f},{"osc1Warp2Mode",6},{"osc1Warp2Amount",.4f},{"osc2Position",0},{"osc2Level",.12f},{"osc2Semi",12},{"subLevel",.18f},{"subFilterRoute",3},{"cutoff",3200},{"resonance",.25f},{"filterEnv",.25f},{"envAttack",.02f},{"envDecay",.65f},{"envSustain",.2f},{"ampAttack",.015f},{"ampSustain",.8f},{"ampRelease",.45f},{"lfo1Sync",1},{"lfo1Division",2},{"mod1Source",1},{"mod1WarpEnabled",1},{"mod1WarpTarget",0},{"mod1Amount",.2f},{"mod2Source",6},{"mod2WarpEnabled",1},{"mod2WarpTarget",1},{"mod2Amount",.35f},{"delayMix",.1f},{"reverbMix",.12f}}}
    ,{"Lead / Ribbon Glide", {{"voiceMode",2},{"glideTime",.18f},{"glideAlways",1},{"osc1Position",.58f},{"osc1Unison",3},{"osc1Detune",7},{"osc1Phase",.08f},{"osc1RandomUnison",0},{"osc2Level",.14f},{"osc2Position",.1f},{"osc2Semi",12},{"osc2Phase",.25f},{"osc2RandomUnison",0},{"subLevel",.1f},{"cutoff",3600},{"resonance",.18f},{"filterEnv",.15f},{"ampAttack",.008f},{"ampDecay",.25f},{"ampSustain",.78f},{"ampRelease",.18f},{"envAttack",.01f},{"envDecay",.32f},{"envSustain",.2f},{"envRelease",.15f},{"delayMix",.13f},{"delayTime",.375f},{"reverbMix",.1f},{"chorusMix",.06f}}}
    ,{"Keys / Phase Alloy", {{"osc1Level",.58f},{"osc1Position",0},{"osc1RandomUnison",0},{"osc1Warp1CrossMode",2},{"osc1Warp1CrossSource",1},{"osc1Warp1Amount",.16f},{"osc2Level",0},{"osc2Position",0},{"osc2Semi",12},{"osc2RandomUnison",0},{"subLevel",0},{"cutoff",7500},{"ampAttack",.002f},{"ampDecay",1.6f},{"ampSustain",.12f},{"ampRelease",.9f},{"envAttack",.002f},{"envDecay",.8f},{"envSustain",0},{"envRelease",.35f},{"mod1Source",3},{"mod1WarpEnabled",1},{"mod1WarpTarget",0},{"mod1Amount",.25f},{"reverbMix",.18f},{"delayMix",.1f}}}
    ,{"Bass / Vector Steel", {{"osc1Level",.52f},{"osc1Position",0},{"osc1Semi",-12},{"osc1RandomUnison",0},{"osc1Warp1CrossMode",1},{"osc1Warp1CrossSource",1},{"osc1Warp1Amount",.1f},{"osc2Level",0},{"osc2Position",0},{"osc2Semi",0},{"osc2RandomUnison",0},{"subLevel",.12f},{"subFilterRoute",3},{"cutoff",3600},{"resonance",.2f},{"ampAttack",.003f},{"ampDecay",.4f},{"ampSustain",.65f},{"ampRelease",.18f},{"envAttack",.003f},{"envDecay",.35f},{"envSustain",0},{"envRelease",.12f},{"mod1Source",3},{"mod1WarpEnabled",1},{"mod1WarpTarget",0},{"mod1Amount",.2f},{"mod2Source",6},{"mod2WarpEnabled",1},{"mod2WarpTarget",0},{"mod2Amount",.22f},{"drive",.12f}}}
    ,{"Motion / Prism Weave", {{"osc1Position",.62f},{"osc1Unison",3},{"osc1Detune",11},{"osc1Warp1Mode",1},{"osc1Warp1Amount",.15f},{"osc2Position",.25f},{"osc2Level",.22f},{"osc2Semi",7},{"subLevel",.14f},{"cutoff",1600},{"resonance",.3f},{"ampAttack",.08f},{"ampRelease",1.0f},{"env3Attack",.4f},{"env3Decay",1.6f},{"env3Sustain",.15f},{"env3Release",1.2f},{"lfo10Sync",1},{"lfo10Division",2},{"lfo10Shape",1},{"lfo3Rate",.17f},{"lfo4Rate",.11f},{"macro5",.75f},{"mod1SourceExtra",8},{"mod1Dest",1},{"mod1Amount",.38f},{"mod2SourceExtra",1},{"mod2Dest",2},{"mod2Amount",.25f},{"mod3SourceExtra",2},{"mod3Dest",7},{"mod3Amount",.4f},{"mod4SourceExtra",10},{"mod4WarpEnabled",1},{"mod4WarpTarget",0},{"mod4Amount",.3f},{"mod4AuxSourceExtra",12},{"chorusMix",.15f},{"delayMix",.12f},{"reverbMix",.2f}}}
    ,{"Pad / Four Horizons", {{"osc1Position",.42f},{"osc1Unison",5},{"osc1Detune",14},{"osc2Position",.1f},{"osc2Level",.2f},{"osc2Semi",12},{"subLevel",.08f},{"cutoff",1200},{"resonance",.2f},{"ampAttack",.9f},{"ampDecay",1.4f},{"ampSustain",.7f},{"ampRelease",2.0f},{"env3Attack",1.6f},{"env3Decay",2.0f},{"env3Sustain",.25f},{"env3Release",1.8f},{"env4Attack",.2f},{"env4Decay",2.2f},{"env4Sustain",.15f},{"env4Release",1.4f},{"lfo9Rate",.13f},{"macro8",.6f},{"mod1SourceExtra",10},{"mod1Dest",1},{"mod1Amount",.42f},{"mod2SourceExtra",11},{"mod2Dest",2},{"mod2Amount",.35f},{"mod3SourceExtra",7},{"mod3Dest",7},{"mod3Amount",.5f},{"mod3AuxSourceExtra",15},{"mod4SourceExtra",9},{"mod4Dest",3},{"mod4Amount",.2f},{"chorusMix",.3f},{"reverbMix",.35f},{"reverbSize",.7f}}}
    ,{"Motion / Clockwork Bloom", {{"osc1Position",.58f},{"osc1Unison",3},{"osc1Detune",9},{"osc2Position",.25f},{"osc2Level",.23f},{"osc2Semi",7},{"subLevel",.08f},{"cutoff",800},{"resonance",.25f},{"filterEnv",.55f},{"ampTempoSync",1},{"ampAttackDivision",1},{"ampHoldDivision",4},{"ampDecayDivision",4},{"ampSustain",.22f},{"ampReleaseDivision",4},{"ampAttackCurve",-.35f},{"ampDecayCurve",.45f},{"ampReleaseCurve",-.2f},{"envTempoSync",1},{"envDelayDivision",3},{"envAttackDivision",4},{"envHoldDivision",3},{"envDecayDivision",5},{"envSustain",.1f},{"envReleaseDivision",5},{"envAttackCurve",.4f},{"envDecayCurve",-.35f},{"envReleaseCurve",.25f},{"delayMix",.1f},{"reverbMix",.14f}}}
    ,{"Lead / Second Breath", {{"voiceMode",2},{"glideTime",.08f},{"osc1Position",.64f},{"osc1Unison",3},{"osc1Detune",7},{"osc2Position",.15f},{"osc2Level",.16f},{"osc2Semi",12},{"subLevel",.1f},{"cutoff",700},{"resonance",.3f},{"filterEnv",.6f},{"ampAttack",.005f},{"ampSustain",.8f},{"ampRelease",.25f},{"envAttack",.008f},{"envHold",.04f},{"envDecay",.45f},{"envSustain",.05f},{"envRelease",.2f},{"envAttackCurve",-.4f},{"envDecayCurve",-.25f},{"envRetriggerLegato",1},{"env3Attack",.015f},{"env3Decay",.3f},{"env3Sustain",0},{"env3Release",.15f},{"env3RetriggerLegato",1},{"env3DecayCurve",.3f},{"mod1SourceExtra",10},{"mod1Dest",2},{"mod1Amount",.18f},{"delayMix",.12f},{"delayTime",.375f},{"reverbMix",.08f}}}
    ,{"Motion / Swing Carousel", {{"osc1Position",.6f},{"osc1Unison",3},{"osc1Detune",9},{"osc2Level",.18f},{"osc2Position",.25f},{"osc2Semi",7},{"subLevel",.1f},{"cutoff",1200},{"resonance",.28f},{"ampAttack",.012f},{"ampSustain",.8f},{"ampRelease",.35f},{"lfo1Sync",1},{"lfo1Division",3},{"lfo1Kind",2},{"lfo1Mode",1},{"lfo1FollowSwing",1},{"lfo1Direction",2},{"lfo1Phase",.125f},{"swingAmount",.6f},{"swingDivision",0},{"mod1Source",1},{"mod1Dest",1},{"mod1Amount",.48f},{"mod2Source",1},{"mod2Dest",7},{"mod2Amount",.35f},{"delayMix",.08f},{"reverbMix",.12f}}}
    ,{"Keys / Phase Lantern", {{"osc1Position",.15f},{"osc1Level",.5f},{"osc1RandomUnison",0},{"subLevel",0},{"cutoff",9000},{"ampAttack",.004f},{"ampDecay",1.2f},{"ampSustain",.3f},{"ampRelease",.6f},{"lfo1AudioRateEnabled",1},{"lfo1AudioRateHz",110},{"lfo1Mode",1},{"lfo2Rate",.6f},{"lfo2Mode",1},{"lfo2Phase",.25f},{"lfo2Direction",1},{"mod1Source",1},{"mod1Dest",5},{"mod1Amount",.018f},{"mod2Source",2},{"mod2LfoPhaseEnabled",1},{"mod2LfoPhaseTarget",0},{"mod2Amount",.22f},{"reverbMix",.12f}}}
    ,{"Motion / Butterfly Current", {{"osc1Position",.62f},{"osc1Unison",3},{"osc1Detune",9},{"osc2Position",.2f},{"osc2Level",.16f},{"osc2Semi",7},{"subLevel",.12f},{"cutoff",1600},{"resonance",.22f},{"ampAttack",.04f},{"ampSustain",.8f},{"ampRelease",.8f},{"lfo1Chaos",1},{"lfo1Rate",1.8f},{"lfo1Mode",1},{"mod1Source",1},{"mod1Dest",1},{"mod1Amount",.52f},{"mod2Source",1},{"mod2Dest",2},{"mod2Amount",.3f},{"delayMix",.12f},{"reverbMix",.2f}}}
    ,{"Texture / Spiral Drift", {{"osc1Position",.37f},{"osc1Unison",3},{"osc1Detune",12},{"osc2Position",.15f},{"osc2Level",.25f},{"osc2Semi",12},{"noiseLevel",.01f},{"subLevel",.08f},{"cutoff",2200},{"resonance",.18f},{"ampAttack",.6f},{"ampSustain",.8f},{"ampRelease",1.4f},{"lfo1Chaos",2},{"lfo1Rate",.7f},{"lfo1Mode",0},{"mod1Source",1},{"mod1Dest",1},{"mod1Amount",.4f},{"mod2Source",1},{"mod2Dest",7},{"mod2Amount",.6f},{"chorusMix",.18f},{"reverbMix",.3f}}}
    ,{"Performance / Prism Handle", {{"osc1Position",.18f},{"osc1Unison",3},{"osc1Detune",8},{"osc1Warp1Mode",1},{"osc1Warp1Amount",.05f},{"osc2Position",.1f},{"osc2Level",.14f},{"osc2Semi",7},{"subLevel",.1f},{"cutoff",650},{"resonance",.22f},{"ampAttack",.04f},{"ampSustain",.8f},{"ampRelease",.55f},{"macro1",.4f},{"mod1Source",6},{"mod1Dest",1},{"mod1Amount",.4f},{"mod2Source",6},{"mod2Dest",2},{"mod2Amount",.35f},{"mod3Source",6},{"mod3WarpEnabled",1},{"mod3WarpTarget",0},{"mod3Amount",.25f},{"delayMix",.08f},{"reverbMix",.12f}}}
    ,{"Motion / Breathing Prism", {{"osc1Position",.18f},{"osc1Unison",3},{"osc1Detune",8},{"osc1Warp1Mode",1},{"osc1Warp1Amount",.05f},{"osc2Position",.1f},{"osc2Level",.14f},{"osc2Semi",7},{"subLevel",.1f},{"cutoff",650},{"resonance",.22f},{"ampAttack",.04f},{"ampSustain",.8f},{"ampRelease",.55f},{"macro1",.2f},{"mod1Source",6},{"mod1Dest",1},{"mod1Amount",.4f},{"mod2Source",6},{"mod2Dest",2},{"mod2Amount",.35f},{"mod3Source",6},{"mod3WarpEnabled",1},{"mod3WarpTarget",0},{"mod3Amount",.25f},{"lfo1Shape",1},{"lfo1Rate",.4f},{"lfo1Mode",1},{"mod4Source",1},{"mod4Polarity",1},{"mod4MacroEnabled",1},{"mod4MacroTarget",0},{"mod4Amount",.6f},{"delayMix",.08f},{"reverbMix",.12f}}}
}};
float v(std::atomic<float>* p) { return p->load(std::memory_order_relaxed); }
struct MacroResetScope
{
    explicit MacroResetScope(std::atomic<std::uint64_t>& value) : epoch(value) { epoch.fetch_add(1); }
    ~MacroResetScope() { epoch.fetch_add(1); }
    std::atomic<std::uint64_t>& epoch;
};
constexpr int maxStateBytes=40*1024*1024;
constexpr int maxSampleFrames=960000;
constexpr const char* editableWaveComment="Anyvoc Wavetable; frameLength=2048; normalise=0";
bool validWaveSamples(const juce::AudioBuffer<float>& audio)
{
    if(audio.getNumChannels()!=1 || audio.getNumSamples()<32 || audio.getNumSamples()>65536)return false;
    for(int n=0;n<audio.getNumSamples();++n){const auto sample=audio.getSample(0,n);if(!std::isfinite(sample) || std::abs(sample)>128.0f)return false;}
    return true;
}
bool isSampleParameter(const juce::String& id)
{
    for(int n=1;n<=3;++n)for(const auto* suffix:{"Mode","Root","Loop","Reverse","GrainSize","GrainDensity","GrainSpray"})
        if(id=="osc"+juce::String(n)+suffix)return true;
    return false;
}
bool isLfoParameter(const juce::String& id)
{
    if(id=="tempo")return true;
    for(int n=1;n<=2;++n){
        const auto prefix="lfo"+juce::String(n);
        for(const auto* suffix:{"Sync","Division","Mode","Interpolation","Kind"})if(id==prefix+suffix)return true;
        for(int point=0;point<=32;++point)if(id==prefix+"Point"+juce::String(point))return true;
    }
    return false;
}
bool isDualFilterParameter(const juce::String& id)
{
    for(const auto* name:{"filterEnabled","filter2Enabled","filter2Type","filter2Cutoff","filter2Resonance","filter2Env",
                         "filterRouting","filterBalance","filterSlope","filter2Slope","filterKeyTrack","filter2KeyTrack",
                         "osc1FilterRoute","osc2FilterRoute","osc3FilterRoute","subFilterRoute","noiseFilterRoute"})
        if(id==name)return true;
    for(int route=1;route<=8;++route)if(id=="mod"+juce::String(route)+"DestBank" || id=="mod"+juce::String(route)+"FilterTarget")return true;
    return false;
}
bool isWarpParameter(const juce::String& id)
{
    for(int osc=1;osc<=3;++osc)for(int slot=1;slot<=2;++slot){
        const auto prefix="osc"+juce::String(osc)+"Warp"+juce::String(slot);
        if(id==prefix+"Mode" || id==prefix+"Amount")return true;
    }
    for(int route=1;route<=8;++route){
        const auto prefix="mod"+juce::String(route);
        if(id==prefix+"WarpEnabled" || id==prefix+"WarpTarget")return true;
    }
    return false;
}
bool isVoicingParameter(const juce::String& id)
{
    if(id=="voiceMode" || id=="glideTime" || id=="glideAlways")return true;
    for(int osc=1;osc<=3;++osc)for(const auto* suffix:{"Phase","PhaseRandom","RandomUnison"})
        if(id=="osc"+juce::String(osc)+suffix)return true;
    return false;
}
bool isCrossModulationParameter(const juce::String& id)
{
    for(int osc=1;osc<=3;++osc)for(int slot=1;slot<=2;++slot){
        const auto prefix="osc"+juce::String(osc)+"Warp"+juce::String(slot);
        if(id==prefix+"CrossMode" || id==prefix+"CrossSource")return true;
    }
    return false;
}

constexpr std::array<const char*,19> modulationRouteSuffixes {
    "Source", "Dest", "Amount", "DestBank", "FilterTarget", "WarpEnabled", "WarpTarget",
    "Enabled", "Polarity", "Curve", "AuxSource", "AuxPolarity", "AuxCurve", "SourceExtra", "AuxSourceExtra",
    "LfoPhaseEnabled", "LfoPhaseTarget", "MacroEnabled", "MacroTarget"
};
bool isAdvancedModulationParameter(const juce::String& id)
{
    for(int route=1;route<=static_cast<int>(anyvoc::modulationRouteCount);++route){
        const auto prefix="mod"+juce::String(route);
        for(size_t suffix=route>8?0:7;suffix<13;++suffix)
            if(id==prefix+modulationRouteSuffixes[suffix])return true;
    }
    return false;
}

bool isExpandedSourceParameter(const juce::String& id)
{
    for(int lfo=3;lfo<=static_cast<int>(anyvoc::lfoCount);++lfo){
        const auto prefix="lfo"+juce::String(lfo);
        for(const auto* suffix:{"Rate","Shape","Kind","Sync","Division","Mode","Interpolation"})
            if(id==prefix+suffix)return true;
        for(int point=0;point<=32;++point)if(id==prefix+"Point"+juce::String(point))return true;
    }
    for(int env=3;env<=4;++env)for(const auto* suffix:{"Attack","Decay","Sustain","Release"})
        if(id=="env"+juce::String(env)+suffix)return true;
    for(int macro=5;macro<=static_cast<int>(anyvoc::macroCount);++macro)
        if(id=="macro"+juce::String(macro))return true;
    for(int route=1;route<=static_cast<int>(anyvoc::modulationRouteCount);++route){
        const auto prefix="mod"+juce::String(route);
        if(id==prefix+"SourceExtra" || id==prefix+"AuxSourceExtra")return true;
    }
    return false;
}

bool isExtendedEnvelopeParameter(const juce::String& id)
{
    for(const auto* prefix:{"amp","env","env3","env4"})
        for(const auto* suffix:{"Delay","Hold","AttackCurve","DecayCurve","ReleaseCurve","TempoSync",
                               "DelayDivision","AttackDivision","HoldDivision","DecayDivision","ReleaseDivision","RetriggerLegato"})
            if(id==juce::String(prefix)+suffix)return true;
    return false;
}

bool isLfoMotionParameter(const juce::String& id)
{
    if(id=="swingAmount" || id=="swingDivision")return true;
    for(int n=1;n<=static_cast<int>(anyvoc::lfoCount);++n)
        for(const auto* suffix:{"Phase","Direction","FollowSwing","AudioRateEnabled","AudioRateHz"})
            if(id=="lfo"+juce::String(n)+suffix)return true;
    for(int n=1;n<=static_cast<int>(anyvoc::modulationRouteCount);++n)
        for(const auto* suffix:{"LfoPhaseEnabled","LfoPhaseTarget"})
            if(id=="mod"+juce::String(n)+suffix)return true;
    return false;
}

bool isChaosLfoParameter(const juce::String& id)
{
    for(int n=1;n<=static_cast<int>(anyvoc::lfoCount);++n)
        if(id=="lfo"+juce::String(n)+"Chaos")return true;
    return false;
}

bool isMacroDestinationParameter(const juce::String& id)
{
    for(int n=1;n<=static_cast<int>(anyvoc::modulationRouteCount);++n)
        for(const auto* suffix:{"MacroEnabled","MacroTarget"})
            if(id=="mod"+juce::String(n)+suffix)return true;
    return false;
}

// Import runs off the audio callback. A bandlimited, symmetric resampler avoids
// adding latency to sample start markers and filters before downsampling.
juce::AudioBuffer<float> toSampleRate48k(const juce::AudioBuffer<float>& input,double rate)
{
    if(std::abs(rate-48000.0)<.01)return juce::AudioBuffer<float>(input);
    const int frames=juce::jlimit(1,maxSampleFrames,static_cast<int>(std::round(input.getNumSamples()*48000.0/rate)));
    juce::AudioBuffer<float> output(input.getNumChannels(),frames);
    const double ratio=rate/48000.0,cutoff=juce::jmin(1.0,1.0/ratio)*.95;
    constexpr int radius=32;
    for(int n=0;n<frames;++n){
        const double position=n*ratio;const int centre=static_cast<int>(position);
        double sum[2]{},weight=0;
        for(int tap=centre-radius+1;tap<=centre+radius;++tap){
            const double distance=position-tap;
            if(std::abs(distance)>=radius)continue;
            const double x=juce::MathConstants<double>::pi*distance*cutoff;
            const double sinc=std::abs(x)<1.e-9?1.0:std::sin(x)/x;
            const double w=cutoff*sinc*(.5+.5*std::cos(juce::MathConstants<double>::pi*distance/radius));
            weight+=w;
            if(tap>=0 && tap<input.getNumSamples())for(int c=0;c<input.getNumChannels();++c)sum[c]+=w*input.getSample(c,tap);
        }
        for(int c=0;c<input.getNumChannels();++c)output.setSample(c,n,juce::jlimit(-1.0f,1.0f,static_cast<float>(sum[c]/juce::jmax(.000001,weight))));
    }
    return output;
}
}

class AnyvocAudioProcessor::SoundHistory : private juce::AudioProcessorParameter::Listener
{
public:
    struct AssetState
    {
        std::shared_ptr<const WaveAsset> encoded;
        std::shared_ptr<const anyvoc::Wavetable> table;
        std::shared_ptr<const anyvoc::SampleData> sample;
    };
    struct Identity { juce::String name, id; int program = 0; };
    struct Snapshot { std::array<AssetState, 6> assets; Identity identity; };
    struct ParameterDelta { int index; float before, after; std::uint64_t epoch; };
    struct AssetDelta { size_t index; AssetState before, after; };
    struct Entry
    {
        juce::String name;
        std::vector<ParameterDelta> parameters;
        std::vector<AssetDelta> assets;
        Identity beforeIdentity, afterIdentity;
        bool identityChanged = false, resetSound = false;
    };
    struct Pending { Entry entry; Snapshot before; std::uint64_t token = 0; };
    struct Scope
    {
        Scope (SoundHistory* h, const juce::String& label) : history (h && onMessageThread() ? h : nullptr)
            { if (history) history->begin (label); }
        ~Scope() { if (history) history->end(); }
        SoundHistory* history;
    };
    struct ExternalPublication
    {
        explicit ExternalPublication (SoundHistory* h, bool enabled = true) : history (enabled ? h : nullptr)
        {
            if (history)
            {
                history->publications.fetch_add (1, std::memory_order_acq_rel);
                history->externalBoundary();
            }
        }
        ~ExternalPublication()
        {
            if (history)
            {
                // Publish a second boundary before making history available.
                // A UI query during a slow host restore must not consume the
                // only invalidation and capture a partially published sound.
                history->externalBoundary();
                history->publications.fetch_sub (1, std::memory_order_release);
            }
        }
        SoundHistory* history;
    };

    explicit SoundHistory (AnyvocAudioProcessor& p) : owner (p), parameterCount (p.getParameters().size()),
        epochs (std::make_unique<std::atomic<std::uint64_t>[]> (static_cast<size_t> (parameterCount)))
    {
        for (int i = 0; i < parameterCount; ++i) epochs[static_cast<size_t> (i)].store (0);
        for (auto* parameter : p.getParameters()) parameter->addListener (this);
    }
    ~SoundHistory() override
    {
        for (auto* parameter : owner.getParameters()) parameter->removeListener (this);
    }
    static bool onMessageThread()
    {
        const auto* manager = juce::MessageManager::getInstanceWithoutCreating();
        return manager != nullptr && manager->isThisTheMessageThread();
    }
    void externalBoundary()
    {
        // Hosts can restore state from a non-UI thread. Reclamation is deferred;
        // no history snapshots, locks, allocations or last asset releases here.
        boundary.fetch_add (1, std::memory_order_acq_rel);
        revision.fetch_add (1, std::memory_order_relaxed);
    }
    std::uint64_t generation() const { return boundary.load (std::memory_order_acquire); }
    bool isPublishing() const { return publications.load (std::memory_order_acquire) != 0; }
    void synchronize()
    {
        if (! onMessageThread()) return;
        const auto next = boundary.load (std::memory_order_acquire);
        if (next != observedBoundary)
        {
            undo.clear(); redo.clear(); pending.reset(); retainedBytes = 0;
            observedBoundary = next;
            // Preserve scope depth until existing mouse gestures close. Their
            // stale pre-restore snapshots must never become a new transaction.
        }
    }
    void begin (const juce::String& label)
    {
        if (! onMessageThread() || replaying) return;
        synchronize();
        if (depth++ == 0)
        {
            if (isPublishing()) return;
            const auto capturedBoundary = generation();
            pending = std::make_unique<Pending>();
            pending->token = ++transactionCounter;
            pending->entry.name = label.trim().isEmpty() ? "Edit sound" : label;
            pending->before = snapshot();
            if (isPublishing() || generation() != capturedBoundary) pending.reset();
        }
    }
    void fullSound()
    {
        if (onMessageThread() && pending) pending->entry.resetSound = true;
    }
    void end()
    {
        if (! onMessageThread()) return;
        synchronize();
        if (depth == 0 || --depth != 0 || ! pending) return;
        if (isPublishing()) { pending.reset(); return; }
        const auto capturedBoundary = generation();
        auto finished = std::move (pending);
        auto& entry = finished->entry;
        for (const auto& delta : entry.parameters)
            if (epochs[static_cast<size_t> (delta.index)].load() != delta.epoch)
            { clear(); return; }
        entry.parameters.erase (std::remove_if (entry.parameters.begin(), entry.parameters.end(),
            [] (const auto& delta) { return delta.before == delta.after; }), entry.parameters.end());
        const auto after = snapshot();
        if (isPublishing() || generation() != capturedBoundary) { clear(); return; }
        for (size_t i = 0; i < after.assets.size(); ++i)
            if (! sameAsset (finished->before.assets[i], after.assets[i]))
                entry.assets.push_back ({ i, finished->before.assets[i], after.assets[i] });
        if (entry.resetSound && ! sameIdentity (finished->before.identity, after.identity))
        {
            entry.identityChanged = true;
            entry.beforeIdentity = finished->before.identity;
            entry.afterIdentity = after.identity;
        }
        if (entry.parameters.empty() && entry.assets.empty() && ! entry.identityChanged) return;
        redo.clear();
        undo.push_back (std::make_unique<Entry> (std::move (entry)));
        trim();
        revision.fetch_add (1, std::memory_order_relaxed);
    }
    void write (juce::RangedAudioParameter& parameter, float normalized)
    {
        if (! onMessageThread() || replaying || isPublishing() || ! std::isfinite (normalized)) return;
        synchronize();
        normalized = juce::jlimit (0.0f, 1.0f, normalized);
        const auto before = parameter.getValue();
        if (before == normalized) return;
        const int index = parameter.getParameterIndex();
        if (! juce::isPositiveAndBelow (index, parameterCount)) return;
        const auto token = pending ? pending->token : 0;
        if (pending)
        {
            auto& list = pending->entry.parameters;
            const auto found = std::find_if (list.begin(), list.end(), [index] (const auto& d) { return d.index == index; });
            if (found == list.end())
            {
                list.push_back ({ index, before, before, epochs[static_cast<size_t> (index)].load() });
            }
        }
        WriteOrigin origin (*this, index, normalized);
        parameter.setValueNotifyingHost (normalized);
        synchronize();
        // A host listener can synchronously restore a project or enter another
        // setter. Never retain a pointer into the pending vector across callbacks.
        if (pending && pending->token == token)
        {
            auto& list = pending->entry.parameters;
            const auto found = std::find_if (list.begin(), list.end(), [index] (const auto& d) { return d.index == index; });
            if (found != list.end()) found->after = parameter.getValue();
        }
    }
    bool available (bool forward)
    {
        if (! onMessageThread()) return false;
        synchronize();
        return depth == 0 && ! replaying && ! isPublishing() && ! (forward ? redo : undo).empty();
    }
    juce::String name (bool forward)
    {
        if (! available (forward)) return {};
        return (forward ? redo : undo).back()->name;
    }
    juce::Result replay (bool forward)
    {
        if (! onMessageThread()) return juce::Result::fail ("Sound history is available on the editor thread.");
        synchronize();
        if (isPublishing()) return juce::Result::fail ("The host is restoring the sound. Try Undo or Redo after it finishes.");
        if (depth > 0 || replaying) return juce::Result::fail ("Finish the current edit before using Undo or Redo.");
        auto& from = forward ? redo : undo;
        auto& to = forward ? undo : redo;
        if (from.empty()) return juce::Result::fail (forward ? "Nothing to redo." : "Nothing to undo.");
        auto ownedEntry = std::move (from.back()); from.pop_back();
        const auto& entry = *ownedEntry;
        const auto replayBoundary = boundary.load();
        for (const auto& delta : entry.parameters)
            if (epochs[static_cast<size_t> (delta.index)].load() != delta.epoch
                || std::abs (owner.getParameters()[delta.index]->getValue() - (forward ? delta.before : delta.after)) > 0.000001f)
            {
                clear();
                return juce::Result::fail ("A parameter in this edit changed outside the editor. Stale sound history was cleared.");
            }
        const auto current = snapshot();
        for (const auto& delta : entry.assets)
            if (! sameAsset (current.assets[delta.index], forward ? delta.before : delta.after))
            {
                clear();
                return juce::Result::fail ("An asset in this edit changed. Stale sound history was cleared.");
            }
        const juce::ScopedValueSetter<bool> inReplay (replaying, true);
        auto interrupted = [&]
        {
            synchronize(); clear();
            return juce::Result::fail ("The host changed the sound during Undo or Redo. Stale history was cleared.");
        };
        for (const auto& delta : entry.parameters)
        {
            auto* parameter = owner.getParameters()[delta.index];
            const float next = forward ? delta.after : delta.before;
            auto changed = [&]
            {
                return isPublishing() || boundary.load() != replayBoundary
                    || epochs[static_cast<size_t> (delta.index)].load() != delta.epoch
                    || std::abs (parameter->getValue() - (forward ? delta.before : delta.after)) > 0.000001f;
            };
            // An earlier notification may automate a later parameter. Recheck
            // before writing so that an intervening host value is never replaced.
            if (changed()) return interrupted();
            WriteOrigin origin (*this, delta.index, next);
            parameter->beginChangeGesture();
            if (changed()) { parameter->endChangeGesture(); return interrupted(); }
            parameter->setValueNotifyingHost (next);
            parameter->endChangeGesture();
            if (isPublishing() || boundary.load() != replayBoundary || epochs[static_cast<size_t> (delta.index)].load() != delta.epoch)
                return interrupted();
        }
        if (isPublishing() || boundary.load() != replayBoundary) return interrupted();
        for (const auto& delta : entry.assets) publish (delta.index, forward ? delta.after : delta.before);
        if (entry.identityChanged)
        {
            const auto& expected = forward ? entry.beforeIdentity : entry.afterIdentity;
            const auto& next = forward ? entry.afterIdentity : entry.beforeIdentity;
            owner.currentProgram.store (next.program);
            // Saving has its own file identity. It must not be reverted by an
            // earlier sound edit, and Undo never changes files on disk.
            if (current.identity.name == expected.name && current.identity.id == expected.id)
                std::atomic_store (&owner.presetIdentity, std::make_shared<const PresetIdentity> (PresetIdentity { next.name, next.id }));
        }
        if (entry.resetSound) owner.soundResetRequested.store (true);
        owner.updateHostDisplay (juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged (true));
        if (isPublishing() || boundary.load() != replayBoundary) return interrupted();
        for (const auto& delta : entry.parameters)
            if (epochs[static_cast<size_t> (delta.index)].load() != delta.epoch) return interrupted();
        to.push_back (std::move (ownedEntry));
        revision.fetch_add (1, std::memory_order_relaxed);
        return juce::Result::ok();
    }
    void setLimits (int entries, size_t bytes)
    {
        if (! onMessageThread() || replaying) return;
        synchronize();
        maximumEntries = juce::jlimit (0, 256, entries);
        maximumBytes = juce::jmin (size_t (512) * 1024 * 1024, bytes);
        trim(); revision.fetch_add (1, std::memory_order_relaxed);
    }
    int count (bool forward) { if (! onMessageThread()) return 0; synchronize(); return isPublishing() ? 0 : static_cast<int> ((forward ? redo : undo).size()); }
    size_t bytes() { if (! onMessageThread()) return 0; synchronize(); return isPublishing() ? 0 : retainedBytes; }
    bool active() { if (! onMessageThread()) return false; synchronize(); return depth > 0; }
    std::atomic<std::uint64_t> revision { 1 };

private:
    struct WriteOrigin
    {
        WriteOrigin (SoundHistory& h, int index, float value) : previous (writer), previousIndex (writeIndex), previousValue (writeValue)
            { writer = &h; writeIndex = index; writeValue = value; }
        ~WriteOrigin() { writer = previous; writeIndex = previousIndex; writeValue = previousValue; }
        SoundHistory* previous;
        int previousIndex;
        float previousValue;
    };
    void parameterValueChanged (int index, float value) override
    {
        if (writer == this && writeIndex == index && writeValue == value) return;
        if (juce::isPositiveAndBelow (index, parameterCount)) epochs[static_cast<size_t> (index)].fetch_add (1, std::memory_order_relaxed);
    }
    void parameterGestureChanged (int, bool) override {}
    Snapshot snapshot() const
    {
        Snapshot result;
        for (size_t i = 0; i < 3; ++i)
        {
            result.assets[i] = { std::atomic_load (&owner.assets[i]), std::atomic_load (&owner.preparedTables[i]), {} };
            result.assets[i + 3] = { std::atomic_load (&owner.sampleAssets[i]), {}, std::atomic_load (&owner.preparedSamples[i]) };
        }
        result.identity = { owner.getLoadedPresetName(), owner.getLoadedPresetId(), owner.currentProgram.load() };
        return result;
    }
    static bool sameIdentity (const Identity& a, const Identity& b)
        { return a.program == b.program && a.name == b.name && a.id == b.id; }
    static bool sameAsset (const AssetState& a, const AssetState& b)
    {
        if (a.encoded == b.encoded) return true;
        return a.encoded && b.encoded && a.encoded->name == b.encoded->name
            && a.encoded->normalise == b.encoded->normalise && a.encoded->data == b.encoded->data;
    }
    void publish (size_t index, const AssetState& asset)
    {
        if (index < 3)
        {
            std::atomic_store (&owner.assets[index], asset.encoded);
            std::atomic_store (&owner.preparedTables[index], asset.table);
            owner.engine.setUserWavetable (static_cast<int> (index), asset.table);
            ++owner.wavetableRevisions[index];
        }
        else
        {
            index -= 3;
            std::atomic_store (&owner.sampleAssets[index], asset.encoded);
            std::atomic_store (&owner.preparedSamples[index], asset.sample);
            owner.engine.setUserSample (static_cast<int> (index), asset.sample);
        }
    }
    void clear()
    {
        undo.clear(); redo.clear(); pending.reset(); retainedBytes = 0;
        revision.fetch_add (1, std::memory_order_relaxed);
    }
    size_t measure() const
    {
        std::set<const void*> counted;
        size_t bytes = 0;
        auto add = [&] (const AssetState& asset)
        {
            if (asset.encoded && counted.insert (asset.encoded.get()).second)
                bytes += sizeof (WaveAsset) + asset.encoded->data.getSize() + asset.encoded->name.getNumBytesAsUTF8();
            if (asset.table && counted.insert (asset.table.get()).second)
                bytes += sizeof (anyvoc::Wavetable) + asset.table->samples.capacity() * sizeof (float);
            if (asset.sample && counted.insert (asset.sample.get()).second)
                bytes += sizeof (anyvoc::SampleData) + static_cast<size_t> (asset.sample->audio.getNumChannels())
                    * static_cast<size_t> (asset.sample->audio.getNumSamples()) * sizeof (float);
        };
        for (const auto* stack : { &undo, &redo }) for (const auto& entry : *stack)
        {
            bytes += sizeof (Entry) + entry->parameters.capacity() * sizeof (ParameterDelta)
                + entry->assets.capacity() * sizeof (AssetDelta) + entry->name.getNumBytesAsUTF8()
                + entry->beforeIdentity.name.getNumBytesAsUTF8() + entry->beforeIdentity.id.getNumBytesAsUTF8()
                + entry->afterIdentity.name.getNumBytesAsUTF8() + entry->afterIdentity.id.getNumBytesAsUTF8();
            for (const auto& asset : entry->assets) { add (asset.before); add (asset.after); }
        }
        return bytes;
    }
    void trim()
    {
        retainedBytes = measure();
        while ((! undo.empty() || ! redo.empty())
               && (undo.size() + redo.size() > static_cast<size_t> (maximumEntries) || retainedBytes > maximumBytes))
        {
            if (! undo.empty()) undo.erase (undo.begin());
            else redo.erase (redo.begin());
            retainedBytes = measure();
        }
    }
    AnyvocAudioProcessor& owner;
    const int parameterCount;
    std::unique_ptr<std::atomic<std::uint64_t>[]> epochs;
    std::atomic<std::uint64_t> boundary { 0 };
    std::atomic<unsigned> publications { 0 };
    std::uint64_t observedBoundary = 0;
    std::uint64_t transactionCounter = 0;
    std::vector<std::unique_ptr<Entry>> undo, redo;
    std::unique_ptr<Pending> pending;
    int depth = 0, maximumEntries = 64;
    bool replaying = false;
    size_t maximumBytes = size_t (128) * 1024 * 1024, retainedBytes = 0;
    inline static thread_local SoundHistory* writer = nullptr;
    inline static thread_local int writeIndex = -1;
    inline static thread_local float writeValue = 0.0f;
};

juce::AudioProcessorValueTreeState::ParameterLayout AnyvocAudioProcessor::makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto f = [&](juce::String id,juce::String name,float lo,float hi,float def,float skew=1.0f,const char* unit="") {
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,
            juce::NormalisableRange<float>(lo,hi,0.0f,skew),def,juce::AudioParameterFloatAttributes{}.withLabel(unit)));
    };
    auto i = [&](juce::String id,juce::String name,int lo,int hi,int def) {
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{id,1},name,lo,hi,def));
    };
    auto c = [&](juce::String id,juce::String name,juce::StringArray choices,int def=0) {
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id,1},name,choices,def));
    };
    for(int n=1;n<=3;++n) {
        auto id="osc"+juce::String(n); auto name="Osc "+juce::String::charToString(static_cast<juce::juce_wchar>('A'+n-1))+" ";
        f(id+"Position",name+"Wave",0,1,n==1?.33f:0);
        f(id+"Level",name+"Level",0,1,n==1?.65f:0);
        i(id+"Semi",name+"Semitone",-24,24,0); f(id+"Fine",name+"Fine",-100,100,0,1,"ct");
        i(id+"Unison",name+"Unison",1,7,1); f(id+"Detune",name+"Detune",0,50,12,1,"ct"); f(id+"Pan",name+"Pan",-1,1,0);
    }
    f("subLevel","Sub Level",0,1,.12f); f("noiseLevel","Noise Level",0,1,0);
    c("filterType","Filter Type",{"Low Pass","High Pass","Band Pass"});
    f("cutoff","Cutoff",20,20000,12000,.25f,"Hz"); f("resonance","Resonance",0,.95f,.15f); f("filterEnv","Filter Envelope Amount",-1,1,0);
    for(const auto& prefix : {juce::String("amp"),juce::String("env")}) {
        auto name=prefix=="amp"?"Amp ":"Filter Env ";
        f(prefix+"Attack",juce::String(name)+"Attack",.001f,8,.01f,.3f,"s");
        f(prefix+"Decay",juce::String(name)+"Decay",.001f,8,.2f,.3f,"s");
        f(prefix+"Sustain",juce::String(name)+"Sustain",0,1,prefix=="amp"?.7f:.3f);
        f(prefix+"Release",juce::String(name)+"Release",.001f,8,.3f,.3f,"s");
    }
    for(int n=1;n<=2;++n) { auto id="lfo"+juce::String(n); f(id+"Rate","LFO "+juce::String(n)+" Rate",.05f,20,static_cast<float>(n),.4f,"Hz"); c(id+"Shape","LFO "+juce::String(n)+" Shape",{"Sine","Triangle","Saw","Square"}); }
    for(int n=1;n<=4;++n) f("macro"+juce::String(n),"Macro "+juce::String(n),0,1,0);
    for(int n=1;n<=8;++n) {
        auto id="mod"+juce::String(n),name="Mod "+juce::String(n)+" ";
        c(id+"Source",name+"Source",{"Off","LFO 1","LFO 2","Filter Env","Velocity","Mod Wheel","Macro 1","Macro 2","Macro 3","Macro 4"});
        c(id+"Dest",name+"Destination",{"Off","Cutoff","Osc A Position","Osc B Position","Osc C Position","Pitch","Level","Pan"});
        f(id+"Amount",name+"Amount",-1,1,0);
    }
    f("drive","Drive",0,1,0); f("chorusMix","Chorus Mix",0,1,0);
    f("delayMix","Delay Mix",0,1,0); f("delayTime","Delay Time",.03f,1.5f,.3f,.5f,"s"); f("delayFeedback","Delay Feedback",0,.85f,.3f);
    f("reverbMix","Reverb Mix",0,1,0); f("reverbSize","Reverb Size",0,1,.5f); f("master","Master",0,1,.65f);
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"arpEnabled",1},"Arpeggiator",false));
    c("arpRate","Arp Rate",{"1/4","1/8","1/16","1/32"},1); i("arpOctaves","Arp Octaves",1,3,1); f("arpGate","Arp Gate",.1f,.95f,.7f);
    // Append new IDs so existing hosts retain the original automation ordering.
    for(int n=1;n<=3;++n){
        const auto id="osc"+juce::String(n),name="Osc "+juce::String(n)+" ";
        c(id+"Mode",name+"Mode",{"Wavetable","Sample","Granular"});
        i(id+"Root",name+"Sample Root",24,96,60);
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"Loop",2},name+"Sample Loop",false));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"Reverse",2},name+"Sample Reverse",false));
        f(id+"GrainSize",name+"Grain Size",.01f,.5f,.08f,.5f,"s");
        f(id+"GrainDensity",name+"Grain Density",2,80,16,.6f,"Hz");
        f(id+"GrainSpray",name+"Grain Spray",0,1,.1f);
    }
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"tempo",3},"Standalone Tempo",juce::NormalisableRange<float>(30,300,0),120,juce::AudioParameterFloatAttributes{}.withLabel("BPM")));
    for(int n=1;n<=2;++n){
        const auto id="lfo"+juce::String(n),name="LFO "+juce::String(n)+" ";
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id+"Kind",3},name+"Pattern Type",juce::StringArray{"Basic","Random","Custom"},0));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"Sync",3},name+"Tempo Sync",false));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id+"Division",3},name+"Division",juce::StringArray{"4 bars","2 bars","1 bar","1/2","1/4","1/8","1/16","1/32","1/8 T","1/16 T","1/8 D","1/16 D"},4));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id+"Mode",3},name+"Mode",juce::StringArray{"Free","Trigger","Envelope"},0));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id+"Interpolation",3},name+"Interpolation",juce::StringArray{"Linear","Step"},0));
        for(int point=0;point<=32;++point)layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id+"Point"+juce::String(point),3},name+"Point "+juce::String(point),-1.0f,1.0f,
            std::sin(juce::MathConstants<float>::twoPi*point/32.0f)));
    }
    auto filterFloat=[&](const char* id,const char* name,float lo,float hi,float def,float skew=1.0f){
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,5},name,juce::NormalisableRange<float>(lo,hi,0,skew),def));
    };
    auto filterChoice=[&](juce::String id,juce::String name,juce::StringArray choices,int def=0){
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id,5},name,choices,def));
    };
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"filterEnabled",5},"Filter A Enabled",true));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"filter2Enabled",5},"Filter B Enabled",false));
    filterChoice("filter2Type","Filter B Type",{"Low Pass","High Pass","Band Pass"});
    filterFloat("filter2Cutoff","Filter B Cutoff",20,20000,12000,.25f);
    filterFloat("filter2Resonance","Filter B Resonance",0,.95f,.15f);
    filterFloat("filter2Env","Filter B Envelope Amount",-1,1,0);
    filterChoice("filterRouting","Filter Routing",{"A > B","B > A","Parallel"});
    filterFloat("filterBalance","Parallel Filter Balance",0,1,.5f);
    filterChoice("filterSlope","Filter A Slope",{"12 dB","24 dB"});
    filterChoice("filter2Slope","Filter B Slope",{"12 dB","24 dB"});
    filterFloat("filterKeyTrack","Filter A Key Tracking",0,1,0);
    filterFloat("filter2KeyTrack","Filter B Key Tracking",0,1,0);
    for(const auto* source:{"osc1","osc2","osc3","sub","noise"})filterChoice(juce::String(source)+"FilterRoute",juce::String(source)+" Filter Route",{"Both","A only","B only","Bypass"});
    // Keep the old eight-choice destination parameter unchanged: normalized host
    // automation must not select a different destination after an upgrade.
    for(int route=1;route<=8;++route){
        const auto prefix="mod"+juce::String(route),name="Mod "+juce::String(route)+" ";
        filterChoice(prefix+"DestBank",name+"Destination Bank",{"Original","Filter"});
        filterChoice(prefix+"FilterTarget",name+"Filter Target",{"Filter B Cutoff","Filter A Resonance","Filter B Resonance","Parallel Balance"});
    }
    for(int osc=1;osc<=3;++osc)for(int slot=1;slot<=2;++slot){
        const auto prefix="osc"+juce::String(osc)+"Warp"+juce::String(slot);
        const auto name="Osc "+juce::String(osc)+" Warp "+juce::String(slot)+" ";
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix+"Mode",6},name+"Mode",juce::StringArray{"Off","Bend +","Bend -","Pulse Width","Sync","Mirror","Fold"},0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{prefix+"Amount",6},name+"Amount",0.0f,1.0f,0.0f));
    }
    // The v0.5 two-choice bank and four filter targets retain their normalized
    // automation mappings. A separate selector adds the six warp targets.
    for(int route=1;route<=8;++route){
        const auto prefix="mod"+juce::String(route),name="Mod "+juce::String(route)+" ";
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{prefix+"WarpEnabled",6},name+"Warp Destination",false));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix+"WarpTarget",6},name+"Warp Target",juce::StringArray{"Osc A Warp 1","Osc A Warp 2","Osc B Warp 1","Osc B Warp 2","Osc C Warp 1","Osc C Warp 2"},0));
    }
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"voiceMode",7},"Voice Mode",juce::StringArray{"Poly","Mono","Legato"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"glideTime",7},"Glide Time",juce::NormalisableRange<float>(0,2,0,.4f),0,juce::AudioParameterFloatAttributes{}.withLabel("s")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"glideAlways",7},"Always Glide",false));
    for(int osc=1;osc<=3;++osc){
        const auto prefix="osc"+juce::String(osc),name="Osc "+juce::String(osc)+" ";
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{prefix+"Phase",7},name+"Start Phase",0.0f,1.0f,0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{prefix+"PhaseRandom",7},name+"Phase Random",0.0f,1.0f,0.0f));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{prefix+"RandomUnison",7},name+"Random Unison",true));
    }
    // Append instead of extending the original seven-choice Warp Mode: changing
    // its range would reinterpret existing normalized host automation.
    for(int osc=1;osc<=3;++osc)for(int slot=1;slot<=2;++slot){
        const auto prefix="osc"+juce::String(osc)+"Warp"+juce::String(slot);
        const auto name="Osc "+juce::String(osc)+" Warp "+juce::String(slot)+" ";
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix+"CrossMode",8},name+"Cross Mode",juce::StringArray{"Classic","FM","PM","AM","RM"},0));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix+"CrossSource",8},name+"Cross Source",juce::StringArray{"Osc A","Osc B","Osc C","Sub","Noise"},0));
    }
    // Every pre-0.13 parameter keeps its position and normalized mapping.
    const juce::StringArray modSources {"Off","LFO 1","LFO 2","Filter Env","Velocity","Mod Wheel","Macro 1","Macro 2","Macro 3","Macro 4"};
    auto modChoice=[&](const juce::String& id,const juce::String& name,const juce::StringArray& choices){
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id,9},name,choices,0));
    };
    auto modFloat=[&](const juce::String& id,const juce::String& name){
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,9},name,
            juce::NormalisableRange<float>(-1.0f,1.0f,0.0f),0.0f));
    };
    for(int route=9;route<=static_cast<int>(anyvoc::modulationRouteCount);++route){
        const auto id="mod"+juce::String(route),name="Mod "+juce::String(route)+" ";
        modChoice(id+"Source",name+"Source",modSources);
        modChoice(id+"Dest",name+"Destination",{"Off","Cutoff","Osc A Position","Osc B Position","Osc C Position","Pitch","Level","Pan"});
        modFloat(id+"Amount",name+"Amount");
        modChoice(id+"DestBank",name+"Destination Bank",{"Original","Filter"});
        modChoice(id+"FilterTarget",name+"Filter Target",{"Filter B Cutoff","Filter A Resonance","Filter B Resonance","Parallel Balance"});
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"WarpEnabled",9},name+"Warp Destination",false));
        modChoice(id+"WarpTarget",name+"Warp Target",{"Osc A Warp 1","Osc A Warp 2","Osc B Warp 1","Osc B Warp 2","Osc C Warp 1","Osc C Warp 2"});
    }
    auto auxSources=modSources;auxSources.set(0,"None");
    for(int route=1;route<=static_cast<int>(anyvoc::modulationRouteCount);++route){
        const auto id="mod"+juce::String(route),name="Mod "+juce::String(route)+" ";
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"Enabled",9},name+"Enabled",true));
        modChoice(id+"Polarity",name+"Polarity",{"Original","Unipolar","Bipolar"});
        modFloat(id+"Curve",name+"Curve");
        modChoice(id+"AuxSource",name+"Aux Source",auxSources);
        modChoice(id+"AuxPolarity",name+"Aux Polarity",{"Original","Unipolar","Bipolar"});
        modFloat(id+"AuxCurve",name+"Aux Curve");
    }
    // Preserve every 0.13 parameter definition. New source selectors leave the
    // normalized mapping of the original ten-choice sources intact.
    auto sourceFloat=[&](const juce::String& id,const juce::String& name,float low,float high,float def,float skew=1.0f,const juce::String& unit={}){
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,10},name,
            juce::NormalisableRange<float>(low,high,0.0f,skew),def,juce::AudioParameterFloatAttributes{}.withLabel(unit)));
    };
    auto sourceChoice=[&](const juce::String& id,const juce::String& name,const juce::StringArray& choices,int def=0){
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id,10},name,choices,def));
    };
    for(int n=3;n<=static_cast<int>(anyvoc::lfoCount);++n){
        const auto id="lfo"+juce::String(n),name="LFO "+juce::String(n)+" ";
        sourceFloat(id+"Rate",name+"Rate",.05f,20,static_cast<float>(n),.4f,"Hz");
        sourceChoice(id+"Shape",name+"Shape",{"Sine","Triangle","Saw","Square"});
        sourceChoice(id+"Kind",name+"Pattern Type",{"Basic","Random","Custom"});
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"Sync",10},name+"Tempo Sync",false));
        sourceChoice(id+"Division",name+"Division",{"4 bars","2 bars","1 bar","1/2","1/4","1/8","1/16","1/32","1/8 T","1/16 T","1/8 D","1/16 D"},4);
        sourceChoice(id+"Mode",name+"Mode",{"Free","Trigger","Envelope"});
        sourceChoice(id+"Interpolation",name+"Interpolation",{"Linear","Step"});
        for(int point=0;point<=32;++point)layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id+"Point"+juce::String(point),10},name+"Point "+juce::String(point),-1.0f,1.0f,
            std::sin(juce::MathConstants<float>::twoPi*point/32.0f)));
    }
    for(int n=3;n<=4;++n){
        const auto id="env"+juce::String(n),name="Env "+juce::String(n)+" ";
        sourceFloat(id+"Attack",name+"Attack",.001f,8,.01f,.3f,"s");
        sourceFloat(id+"Decay",name+"Decay",.001f,8,.2f,.3f,"s");
        sourceFloat(id+"Sustain",name+"Sustain",0,1,.3f);
        sourceFloat(id+"Release",name+"Release",.001f,8,.3f,.3f,"s");
    }
    for(int n=5;n<=static_cast<int>(anyvoc::macroCount);++n)
        sourceFloat("macro"+juce::String(n),"Macro "+juce::String(n),0,1,0);
    const juce::StringArray extendedSources {"Legacy","LFO 3","LFO 4","LFO 5","LFO 6","LFO 7","LFO 8","LFO 9","LFO 10",
        "Amp Env","Env 3","Env 4","Macro 5","Macro 6","Macro 7","Macro 8"};
    for(int route=1;route<=static_cast<int>(anyvoc::modulationRouteCount);++route){
        const auto id="mod"+juce::String(route),name="Mod "+juce::String(route)+" ";
        sourceChoice(id+"SourceExtra",name+"Extended Source",extendedSources);
        sourceChoice(id+"AuxSourceExtra",name+"Extended Aux Source",extendedSources);
    }
    // Append only: the complete 0.14 host parameter prefix remains unchanged.
    const juce::StringArray envelopeDivisions {"Instant","1/64","1/32","1/16","1/8","1/4","1/2",
        "1 bar","2 bars","4 bars","1/8 T","1/16 T","1/8 D","1/16 D"};
    for(const auto* prefix:{"amp","env","env3","env4"}){
        const juce::String id(prefix);
        const auto name=id=="amp"?juce::String("Amp "):id=="env"?juce::String("Filter Env "):id=="env3"?juce::String("Env 3 "):juce::String("Env 4 ");
        auto addFloat=[&](const char* suffix,float low,float high,float def,float skew,const juce::String& unit){
            layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id+suffix,11},name+suffix,
                juce::NormalisableRange<float>(low,high,0.0f,skew),def,juce::AudioParameterFloatAttributes{}.withLabel(unit)));
        };
        addFloat("Delay",0,8,0,.3f,"s");addFloat("Hold",0,8,0,.3f,"s");
        for(const auto* suffix:{"AttackCurve","DecayCurve","ReleaseCurve"})addFloat(suffix,-1,1,0,1,{});
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"TempoSync",11},name+"Tempo Sync",false));
        constexpr std::array<const char*,5> stages {"Delay","Attack","Hold","Decay","Release"};
        constexpr std::array<int,5> defaults {0,3,0,5,5};
        for(size_t stage=0;stage<stages.size();++stage)
            layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id+stages[stage]+"Division",11},
                name+stages[stage]+" Division",envelopeDivisions,defaults[stage]));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"RetriggerLegato",11},name+"Retrigger in Legato",false));
    }
    // Append only: the complete 0.15 parameter prefix and its automation remain frozen.
    for(int n=1;n<=static_cast<int>(anyvoc::lfoCount);++n){
        const auto id="lfo"+juce::String(n),name="LFO "+juce::String(n)+" ";
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id+"Phase",12},name+"Phase",
            juce::NormalisableRange<float>(0.0f,1.0f,0.0f),0.0f));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id+"Direction",12},name+"Direction",juce::StringArray{"Forward","Reverse","Ping Pong"},0));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"FollowSwing",12},name+"Follow Swing",false));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"AudioRateEnabled",12},name+"Audio Rate Enabled",false));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id+"AudioRateHz",12},name+"Audio Rate",
            juce::NormalisableRange<float>(20.0f,1000.0f,0.0f,.5f),100.0f,juce::AudioParameterFloatAttributes{}.withLabel("Hz")));
    }
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"swingAmount",12},"Swing Amount",
        juce::NormalisableRange<float>(0.0f,1.0f,0.0f),0.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"swingDivision",12},"Swing Grid",juce::StringArray{"1/8","1/16"},0));
    juce::StringArray phaseTargets;
    for(int n=1;n<=static_cast<int>(anyvoc::lfoCount);++n)phaseTargets.add("LFO "+juce::String(n)+" Phase");
    for(int n=1;n<=static_cast<int>(anyvoc::modulationRouteCount);++n){
        const auto id="mod"+juce::String(n),name="Mod "+juce::String(n)+" ";
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"LfoPhaseEnabled",12},name+"LFO Phase Destination",false));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id+"LfoPhaseTarget",12},name+"LFO Phase Target",phaseTargets,0));
    }
    // A separate selector preserves the complete 0.16 host parameter prefix,
    // including the normalised meaning of the original Shape and Kind choices.
    for(int n=1;n<=static_cast<int>(anyvoc::lfoCount);++n)
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"lfo"+juce::String(n)+"Chaos",13},
            "LFO "+juce::String(n)+" Chaos",juce::StringArray{"Off","Lorenz","Rossler"},0));
    // Append only: all 1,192 parameters of 0.18 retain their exact mappings.
    // A separate bank preserves the legacy destination choice ranges.
    juce::StringArray macroTargets;
    for(int n=1;n<=static_cast<int>(anyvoc::macroCount);++n)macroTargets.add("Macro "+juce::String(n));
    for(int n=1;n<=static_cast<int>(anyvoc::modulationRouteCount);++n){
        const auto id="mod"+juce::String(n),name="Mod "+juce::String(n)+" ";
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id+"MacroEnabled",14},name+"Macro Destination",false));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id+"MacroTarget",14},name+"Macro Target",macroTargets,0));
    }
    return layout;
}

AnyvocAudioProcessor::AnyvocAudioProcessor()
 : AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)), apvts(*this,nullptr,"ANYVOC",makeParameters())
{
    auto p=[&](const juce::String& id){return apvts.getRawParameterValue(id);};
    for(int n=0;n<3;++n) { auto id="osc"+juce::String(n+1); oscParams[static_cast<size_t>(n)]={p(id+"Position"),p(id+"Level"),p(id+"Semi"),p(id+"Fine"),p(id+"Unison"),p(id+"Detune"),p(id+"Pan"),p(id+"Mode"),p(id+"Root"),p(id+"Loop"),p(id+"Reverse"),p(id+"GrainSize"),p(id+"GrainDensity"),p(id+"GrainSpray")}; }
    for(size_t n=0;n<envParams.size();++n){
        const auto id=n==0?juce::String("amp"):(n==1?juce::String("env"):"env"+juce::String(static_cast<int>(n)+1));
        envParams[n]={p(id+"Attack"),p(id+"Decay"),p(id+"Sustain"),p(id+"Release"),p(id+"Delay"),p(id+"Hold"),
            p(id+"AttackCurve"),p(id+"DecayCurve"),p(id+"ReleaseCurve"),p(id+"TempoSync"),
            {p(id+"DelayDivision"),p(id+"AttackDivision"),p(id+"HoldDivision"),p(id+"DecayDivision"),p(id+"ReleaseDivision")},p(id+"RetriggerLegato")};
    }
    for(size_t n=0;n<routeParams.size();++n) { auto id="mod"+juce::String(static_cast<int>(n)+1);routeParams[n]={p(id+"Source"),p(id+"Dest"),p(id+"Amount"),p(id+"DestBank"),p(id+"FilterTarget"),p(id+"WarpEnabled"),p(id+"WarpTarget"),p(id+"Enabled"),p(id+"Polarity"),p(id+"Curve"),p(id+"AuxSource"),p(id+"AuxPolarity"),p(id+"AuxCurve"),p(id+"SourceExtra"),p(id+"AuxSourceExtra"),p(id+"LfoPhaseEnabled"),p(id+"LfoPhaseTarget"),p(id+"MacroEnabled"),p(id+"MacroTarget")}; }
    for(size_t osc=0;osc<3;++osc)for(size_t slot=0;slot<2;++slot){
        const auto id="osc"+juce::String(static_cast<int>(osc)+1)+"Warp"+juce::String(static_cast<int>(slot)+1);
        warpModes[osc][slot]=p(id+"Mode");warpAmounts[osc][slot]=p(id+"Amount");
        crossModes[osc][slot]=p(id+"CrossMode");crossSources[osc][slot]=p(id+"CrossSource");
    }
    for(size_t n=0;n<macroParams.size();++n) macroParams[n]=p("macro"+juce::String(static_cast<int>(n)+1));
    sub=p("subLevel");noise=p("noiseLevel");cutoff=p("cutoff");resonance=p("resonance");filterEnv=p("filterEnv");filterType=p("filterType");drive=p("drive");master=p("master");
    filterEnabled=p("filterEnabled");filter2Enabled=p("filter2Enabled");filter2Cutoff=p("filter2Cutoff");filter2Resonance=p("filter2Resonance");filter2Env=p("filter2Env");filter2Type=p("filter2Type");
    filterRouting=p("filterRouting");filterBalance=p("filterBalance");filterSlope=p("filterSlope");filter2Slope=p("filter2Slope");filterKeyTrack=p("filterKeyTrack");filter2KeyTrack=p("filter2KeyTrack");
    const std::array<const char*,5> sourceRoutes{"osc1FilterRoute","osc2FilterRoute","osc3FilterRoute","subFilterRoute","noiseFilterRoute"};
    for(size_t n=0;n<5;++n)filterRoutes[n]=p(sourceRoutes[n]);
    chorus=p("chorusMix");delayMix=p("delayMix");delayTime=p("delayTime");delayFeedback=p("delayFeedback");reverb=p("reverbMix");reverbSize=p("reverbSize");
    arpEnabled=p("arpEnabled");arpRate=p("arpRate");arpOctaves=p("arpOctaves");arpGate=p("arpGate");
    tempo=p("tempo");swingAmount=p("swingAmount");swingDivision=p("swingDivision");
    voiceMode=p("voiceMode");glideTime=p("glideTime");glideAlways=p("glideAlways");
    for(size_t osc=0;osc<3;++osc){const auto prefix="osc"+juce::String(static_cast<int>(osc)+1);oscPhases[osc]=p(prefix+"Phase");oscPhaseRandom[osc]=p(prefix+"PhaseRandom");oscRandomUnison[osc]=p(prefix+"RandomUnison");}
    for(size_t n=0;n<lfoRates.size();++n){
        const auto id="lfo"+juce::String(static_cast<int>(n)+1);
        lfoRates[n]=p(id+"Rate");lfoShapes[n]=p(id+"Shape");
        lfoPhaseOffset[n]=p(id+"Phase");lfoDirection[n]=p(id+"Direction");lfoFollowSwing[n]=p(id+"FollowSwing");
        lfoAudioRateEnabled[n]=p(id+"AudioRateEnabled");lfoAudioRateHz[n]=p(id+"AudioRateHz");
        lfoChaos[n]=p(id+"Chaos");
        lfoSync[n]=p(id+"Sync");lfoDivision[n]=p(id+"Division");lfoMode[n]=p(id+"Mode");lfoInterpolation[n]=p(id+"Interpolation");lfoKind[n]=p(id+"Kind");
        for(int point=0;point<=32;++point)lfoPoints[n][static_cast<size_t>(point)]=p(id+"Point"+juce::String(point));
    }
    formats.registerBasicFormats(); arpMidi.ensureSize(32768);
    juce::AudioBuffer<float> bell(2,96000);
    const double fundamental=juce::MidiMessage::getMidiNoteInHertz(60);
    for(int n=0;n<bell.getNumSamples();++n){
        const double t=n/48000.0;
        for(int c=0;c<2;++c){
            double value=0;
            for(int partial=0;partial<4;++partial){
                const double ratio=std::array<double,4>{1,2.01,3.98,6.02}[static_cast<size_t>(partial)];
                value+=std::sin(juce::MathConstants<double>::twoPi*fundamental*ratio*t+c*partial*.07)*std::exp(-t*(1.9+partial*1.6))/(1+partial*1.7);
            }
            bell.setSample(c,n,static_cast<float>(value*.47*juce::jmin(1.0,t/.002)*juce::jmin(1.0,(2.0-t)/.02)));
        }
    }
    factorySample=anyvoc::SynthEngine::makeSample(bell,48000);
    factorySampleAsset=encodeSample(bell,"Copper Bell (Anyvoc)");
    history=std::make_unique<SoundHistory>(*this);
}

AnyvocAudioProcessor::~AnyvocAudioProcessor() { history.reset(); }
void AnyvocAudioProcessor::beginUserEdit(const juce::String& label) { history->begin(label); }
void AnyvocAudioProcessor::endUserEdit() { history->end(); }
void AnyvocAudioProcessor::setUserParameter(const juce::String& id,float plainValue)
{
    if(!SoundHistory::onMessageThread() || !std::isfinite(plainValue))return;
    if(auto* parameter=apvts.getParameter(id)){
        SoundHistory::Scope edit(history.get(),"Change "+parameter->getName(128));
        history->write(*parameter,parameter->convertTo0to1(plainValue));
    }
}
bool AnyvocAudioProcessor::canUndo() const { return history->available(false); }
bool AnyvocAudioProcessor::canRedo() const { return history->available(true); }
juce::String AnyvocAudioProcessor::getUndoName() const { return history->name(false); }
juce::String AnyvocAudioProcessor::getRedoName() const { return history->name(true); }
juce::Result AnyvocAudioProcessor::undoUserEdit() { return history->replay(false); }
juce::Result AnyvocAudioProcessor::redoUserEdit() { return history->replay(true); }
std::uint64_t AnyvocAudioProcessor::getHistoryRevision() const { history->synchronize();return history->revision.load(); }
int AnyvocAudioProcessor::getUndoCount() const { return history->count(false); }
int AnyvocAudioProcessor::getRedoCount() const { return history->count(true); }
size_t AnyvocAudioProcessor::getHistoryBytes() const { return history->bytes(); }
bool AnyvocAudioProcessor::isUserEditActive() const { return history->active(); }
void AnyvocAudioProcessor::setHistoryLimits(int entries,size_t bytes) { history->setLimits(entries,bytes); }

anyvoc::SynthSettings AnyvocAudioProcessor::readSettings() const
{
    anyvoc::SynthSettings s;
    s.voiceMode=static_cast<int>(v(voiceMode));s.glideTime=v(glideTime);s.glideAlways=v(glideAlways)>.5f;
    for(size_t osc=0;osc<3;++osc){s.osc[osc].phase=v(oscPhases[osc]);s.osc[osc].phaseRandom=v(oscPhaseRandom[osc]);s.osc[osc].randomUnison=v(oscRandomUnison[osc])>.5f;}
    for(size_t n=0;n<3;++n) { const auto& p=oscParams[n];auto& o=s.osc[n];o.position=v(p.position);o.level=v(p.level);o.semitone=v(p.semi);o.fine=v(p.fine);o.unison=static_cast<int>(v(p.unison));o.detune=v(p.detune);o.pan=v(p.pan);o.mode=static_cast<int>(v(p.mode));o.rootNote=static_cast<int>(v(p.root));o.sampleLoop=v(p.loop)>.5f;o.sampleReverse=v(p.reverse)>.5f;o.grainSize=v(p.grainSize);o.grainDensity=v(p.grainDensity);o.grainSpray=v(p.grainSpray); }
    for(size_t osc=0;osc<3;++osc)for(size_t slot=0;slot<2;++slot){
        s.osc[osc].warpMode[slot]=static_cast<int>(v(warpModes[osc][slot]));s.osc[osc].warpAmount[slot]=v(warpAmounts[osc][slot]);
        s.osc[osc].crossMode[slot]=static_cast<int>(v(crossModes[osc][slot]));s.osc[osc].crossSource[slot]=static_cast<int>(v(crossSources[osc][slot]));
    }
    s.sub=v(sub);s.noise=v(noise);s.cutoff=v(cutoff);s.resonance=v(resonance);s.filterEnv=v(filterEnv);s.filterType=static_cast<int>(v(filterType));s.drive=v(drive);s.master=v(master);
    s.filterEnabled=v(filterEnabled)>.5f;s.filter2Enabled=v(filter2Enabled)>.5f;s.filter2Cutoff=v(filter2Cutoff);s.filter2Resonance=v(filter2Resonance);s.filter2Env=v(filter2Env);s.filter2Type=static_cast<int>(v(filter2Type));
    s.filterRouting=static_cast<int>(v(filterRouting));s.filterBalance=v(filterBalance);s.filterSlope=static_cast<int>(v(filterSlope));s.filter2Slope=static_cast<int>(v(filter2Slope));s.filterKeyTrack=v(filterKeyTrack);s.filter2KeyTrack=v(filter2KeyTrack);
    for(size_t n=0;n<5;++n)s.filterRoutes[n]=static_cast<int>(v(filterRoutes[n]));
    for(size_t n=0;n<envParams.size();++n){
        const auto& p=envParams[n];auto& e=n==0?s.amp:(n==1?s.filter:s.extraEnvelopes[n-2]);
        e.attack=v(p.attack);e.decay=v(p.decay);e.sustain=v(p.sustain);e.release=v(p.release);
        e.delay=v(p.delay);e.hold=v(p.hold);e.attackCurve=v(p.attackCurve);e.decayCurve=v(p.decayCurve);e.releaseCurve=v(p.releaseCurve);
        e.tempoSync=v(p.tempoSync)>.5f;e.retriggerLegato=v(p.retriggerLegato)>.5f;
        for(size_t stage=0;stage<e.divisions.size();++stage)e.divisions[stage]=static_cast<int>(v(p.divisions[stage]));
    }
    for(size_t n=0;n<lfoRates.size();++n){
        s.lfoRate[n]=v(lfoRates[n]);s.lfoShape[n]=getLfoShape(static_cast<int>(n));
        s.lfoPhaseOffset[n]=v(lfoPhaseOffset[n]);s.lfoDirection[n]=static_cast<int>(v(lfoDirection[n]));s.lfoFollowSwing[n]=v(lfoFollowSwing[n])>.5f;
        s.lfoAudioRateEnabled[n]=v(lfoAudioRateEnabled[n])>.5f;s.lfoAudioRateHz[n]=v(lfoAudioRateHz[n]);
        s.lfoSync[n]=v(lfoSync[n])>.5f;s.lfoDivision[n]=static_cast<int>(v(lfoDivision[n]));s.lfoMode[n]=static_cast<int>(v(lfoMode[n]));s.lfoInterpolation[n]=static_cast<int>(v(lfoInterpolation[n]));
        for(size_t point=0;point<33;++point)s.lfoPoints[n][point]=v(lfoPoints[n][point]);
    }
    s.swingAmount=v(swingAmount);s.swingDivision=static_cast<int>(v(swingDivision));
    for(size_t n=0;n<macroParams.size();++n)s.macros[n]=v(macroParams[n]);
    for(size_t n=0;n<routeParams.size();++n)s.routes[n]=getModulationRoute(static_cast<int>(n));
    s.chorus=v(chorus);s.delayMix=v(delayMix);s.delayTime=v(delayTime);s.delayFeedback=v(delayFeedback);s.reverb=v(reverb);s.reverbSize=v(reverbSize);
    return s;
}

void AnyvocAudioProcessor::prepareToPlay(double sr,int maxBlock)
{ MacroResetScope reset(macroResetEpoch);sampleRate=sr;engine.prepare(sr,maxBlock);engine.setSettings(readSettings());clearArp();arpWasEnabled=false;wasPlaying=false;outputLevel.store(0); }
void AnyvocAudioProcessor::resetEngine()
{
    MacroResetScope reset(macroResetEpoch);
    engine.reset();
}
void AnyvocAudioProcessor::releaseResources() { resetEngine();clearArp(); }
double AnyvocAudioProcessor::getTailLengthSeconds() const
{
    const double delayTail=v(delayMix)>0.0001f ? v(delayTime)*juce::jmax(1.0,std::log(0.0001)/std::log(juce::jmax(0.001,static_cast<double>(v(delayFeedback))))) : 0.0;
    anyvoc::EnvelopeSettings ampEnvelope;
    ampEnvelope.release=v(envParams[0].release);ampEnvelope.tempoSync=v(envParams[0].tempoSync)>.5f;
    ampEnvelope.divisions[4]=static_cast<int>(v(envParams[0].divisions[4]));
    const double tailTempo=hostTempo.load()?actualTempo.load():v(tempo);
    return anyvoc::envelopeStageSeconds(ampEnvelope,4,tailTempo)+delayTail+(v(reverb)>0.0001f?15.0:0.0)+.1;
}
bool AnyvocAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const {return l.getMainInputChannelSet().isDisabled() && (l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo() || l.getMainOutputChannelSet()==juce::AudioChannelSet::mono());}
void AnyvocAudioProcessor::clearArp() { held.fill(0);keyDown.fill(false);arpNote=-1;arpIndex=0;stepRemaining=0;gateRemaining=0;arpSustain=false; }

void AnyvocAudioProcessor::processArp(juce::MidiBuffer& midi,int samples,double bpm)
{
    const bool enabled=v(arpEnabled)>.5f;
    if(enabled!=arpWasEnabled){resetEngine();clearArp();arpWasEnabled=enabled;}
    if(!enabled)return;
    arpMidi.clear();
    auto it=midi.cbegin(),end=midi.cend();
    const double step=sampleRate*60.0/juce::jlimit(20.0,400.0,bpm)/std::pow(2.0,v(arpRate));
    int count=0;for(auto velocity:held)if(velocity>0)++count;
    for(int pos=0;pos<samples;++pos){
        bool receivedEvent=false;
        while(it!=end && (*it).samplePosition<=pos){
            receivedEvent=true;
            auto m=(*it).getMessage(); ++it;
            if(m.isNoteOn()){held[static_cast<size_t>(m.getNoteNumber())]=m.getFloatVelocity();keyDown[static_cast<size_t>(m.getNoteNumber())]=true;}
            else if(m.isNoteOff()){keyDown[static_cast<size_t>(m.getNoteNumber())]=false;if(!arpSustain)held[static_cast<size_t>(m.getNoteNumber())]=0;}
            else if(m.isAllNotesOff() || m.isAllSoundOff()){clearArp();arpMidi.addEvent(m,pos);arpMidi.addEvent(m.isAllSoundOff()?juce::MidiMessage::allSoundOff(1):juce::MidiMessage::allNotesOff(1),pos);}
            else if(m.isController() && m.getControllerNumber()==64){arpSustain=m.getControllerValue()>=64;if(!arpSustain)for(size_t n=0;n<128;++n)if(!keyDown[n])held[n]=0;}
            else arpMidi.addEvent(m,pos);
        }
        if(receivedEvent){count=0;for(auto velocity:held)if(velocity>0)++count;}
        if(arpNote>=0 && (gateRemaining<=0 || count==0)){arpMidi.addEvent(juce::MidiMessage::noteOff(1,arpNote),pos);arpNote=-1;}
        if(count>0 && stepRemaining<=0){
            int sequence=arpIndex++ % (count*static_cast<int>(v(arpOctaves)));
            int octave=sequence/count,selected=sequence%count,note=0;
            for(int n=0;n<128;++n)if(held[static_cast<size_t>(n)]>0 && selected--==0){note=n;break;}
            if(arpNote>=0)arpMidi.addEvent(juce::MidiMessage::noteOff(1,arpNote),pos);
            arpNote=juce::jmin(127,note+12*octave);
            arpMidi.addEvent(juce::MidiMessage::noteOn(1,arpNote,held[static_cast<size_t>(note)]),pos);
            stepRemaining+=step;gateRemaining=step*v(arpGate);
        }
        if(count==0){stepRemaining=0;arpIndex=0;}else --stepRemaining;
        --gateRemaining;
    }
    midi.swapWith(arpMidi);
}

void AnyvocAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;buffer.clear();
    if(panicRequested.load() || soundResetRequested.load()){
        // Enter the reset epoch before claiming either flag. The UI must never
        // mistake a claimed request for a completed engine reset.
        MacroResetScope reset(macroResetEpoch);
        const bool hardReset=panicRequested.exchange(false);
        if(soundResetRequested.exchange(false) || hardReset){engine.reset();clearArp();if(hardReset)keyboardState.reset();}
    }
    keyboardState.processNextMidiBuffer(midi,0,buffer.getNumSamples(),true);
    double bpm=v(tempo),ppq=0;bool fromHost=false,hasPpq=false,playing=false;
    if(auto* host=getPlayHead())if(auto position=host->getPosition()){
        if(auto hostBpm=position->getBpm())if(std::isfinite(*hostBpm) && *hostBpm>0){bpm=juce::jlimit(1.0,960.0,*hostBpm);fromHost=true;}
        if(auto hostPpq=position->getPpqPosition())if(std::isfinite(*hostPpq)){ppq=*hostPpq;hasPpq=true;}
        playing=position->getIsPlaying();
        if(wasPlaying && !playing){resetEngine();clearArp();}
        wasPlaying=playing;
    }
    actualTempo.store(static_cast<float>(bpm));hostTempo.store(fromHost);
    engine.setSettings(readSettings());processArp(midi,buffer.getNumSamples(),bpm);engine.setTransport(bpm,ppq,playing,hasPpq);engine.render(buffer,midi);
    float peak=0;for(int c=0;c<buffer.getNumChannels();++c)peak=juce::jmax(peak,buffer.getMagnitude(c,0,buffer.getNumSamples()));
    outputLevel.store(juce::jmax(peak,outputLevel.load()*.85f));midi.clear();
}

int AnyvocAudioProcessor::getNumPrograms(){return static_cast<int>(presets.size());}
juce::StringArray AnyvocAudioProcessor::getPresetNames() const {juce::StringArray names;for(const auto& p:presets)names.add(p.name);return names;}
const juce::String AnyvocAudioProcessor::getProgramName(int index){return index>=0 && index<getNumPrograms()?presets[static_cast<size_t>(index)].name:juce::String{};}
juce::String AnyvocAudioProcessor::getLoadedPresetName() const
{
    if (auto identity = std::atomic_load (&presetIdentity)) return identity->name;
    return presets[static_cast<size_t> (juce::jlimit (0, static_cast<int> (presets.size()) - 1, currentProgram.load()))].name;
}
juce::String AnyvocAudioProcessor::getLoadedPresetId() const
{
    if (auto identity = std::atomic_load (&presetIdentity)) return identity->id;
    return anyvoc::PresetLibrary::factoryId (presets[static_cast<size_t> (juce::jlimit (0, static_cast<int> (presets.size()) - 1, currentProgram.load()))].name);
}
void AnyvocAudioProcessor::setCurrentProgram(int index)
{
    applyProgram(index,false);
}
void AnyvocAudioProcessor::selectFactoryPreset(int index)
{
    if(index<0 || index>=getNumPrograms() || !SoundHistory::onMessageThread() || history->isPublishing())return;
    SoundHistory::Scope edit(history.get(),index==0?"Initialize sound":"Load "+getProgramName(index));
    history->fullSound();
    applyProgram(index,true);
}
void AnyvocAudioProcessor::applyProgram(int index,bool userEdit)
{
    if(index<0 || index>=getNumPrograms())return;
    SoundHistory::ExternalPublication publication(history.get(),!userEdit);
    const auto generation=history->generation();
    auto write=[&](juce::RangedAudioParameter& parameter,float normalized){
        if(history->generation()!=generation)return false;
        if(userEdit)history->write(parameter,normalized);else parameter.setValueNotifyingHost(normalized);
        return history->generation()==generation;
    };
    for(auto* base:getParameters())if(auto* parameter=dynamic_cast<juce::RangedAudioParameter*>(base))
        if(!write(*parameter,parameter->getDefaultValue()))return;
    for(const auto& pair:presets[static_cast<size_t>(index)].values)if(auto* parameter=apvts.getParameter(pair.first))
        if(!write(*parameter,parameter->convertTo0to1(pair.second)))return;
    if(index==12)for(int point=0;point<=32;++point){
        constexpr std::array<float,8> pattern{-.8f,.4f,-.3f,.9f,-.6f,.1f,.7f,-.2f};
        if(auto* parameter=apvts.getParameter("lfo1Point"+juce::String(point)))
            if(!write(*parameter,parameter->convertTo0to1(pattern[static_cast<size_t>(point/4)%8])))return;
    }
    if(index==22)for(int point=0;point<=32;++point){
        constexpr std::array<float,5> pattern{-1.0f,-.8f,.9f,.2f,-1.0f};
        const size_t left=static_cast<size_t>(juce::jmin(point/8,3));
        const float fraction=(point-static_cast<int>(left)*8)/8.0f;
        const float value=pattern[left]+fraction*(pattern[left+1]-pattern[left]);
        if(auto* parameter=apvts.getParameter("lfo1Point"+juce::String(point)))
            if(!write(*parameter,parameter->convertTo0to1(value)))return;
    }
    for(int n=0;n<3;++n){
        const auto osc=static_cast<size_t>(n);
        const bool bell=n==0 && (index==10 || index==11);
        std::atomic_store(&assets[osc],std::shared_ptr<const WaveAsset>{});
        std::atomic_store(&preparedTables[osc],std::shared_ptr<const anyvoc::Wavetable>{});
        engine.setUserWavetable(n,nullptr);++wavetableRevisions[osc];
        std::atomic_store(&sampleAssets[osc],bell?factorySampleAsset:std::shared_ptr<const WaveAsset>{});
        std::atomic_store(&preparedSamples[osc],bell?factorySample:std::shared_ptr<const anyvoc::SampleData>{});
        engine.setUserSample(n,bell?factorySample:nullptr);
    }
    currentProgram.store(index);
    const juce::String name = presets[static_cast<size_t> (index)].name;
    std::atomic_store (&presetIdentity, std::make_shared<const PresetIdentity> (PresetIdentity { name, anyvoc::PresetLibrary::factoryId (name) }));
    soundResetRequested.store(true);
}

juce::ValueTree AnyvocAudioProcessor::createState() const
{
    auto state=const_cast<juce::AudioProcessorValueTreeState&>(apvts).copyState();
    state.setProperty("schemaVersion",14,nullptr);state.setProperty("program",currentProgram.load(),nullptr);
    const auto identity = std::atomic_load (&presetIdentity);
    const auto name = identity ? identity->name : juce::String (presets[static_cast<size_t> (juce::jlimit (0, static_cast<int> (presets.size()) - 1, currentProgram.load()))].name);
    state.setProperty ("presetName", name, nullptr);
    state.setProperty ("presetId", identity ? identity->id : anyvoc::PresetLibrary::factoryId (name), nullptr);
    for(int n=0;n<3;++n)if(auto asset=std::atomic_load(&assets[static_cast<size_t>(n)])){
        juce::ValueTree child("WAVETABLE");child.setProperty("osc",n,nullptr);child.setProperty("name",asset->name,nullptr);child.setProperty("wav",asset->data.toBase64Encoding(),nullptr);child.setProperty("normalise",asset->normalise,nullptr);state.addChild(child,-1,nullptr);
    }
    for(int n=0;n<3;++n)if(auto asset=std::atomic_load(&sampleAssets[static_cast<size_t>(n)])){
        juce::ValueTree child("SAMPLE");child.setProperty("osc",n,nullptr);child.setProperty("name",asset->name,nullptr);child.setProperty("wav",asset->data.toBase64Encoding(),nullptr);state.addChild(child,-1,nullptr);
    }
    return state;
}

bool AnyvocAudioProcessor::restoreState(const juce::ValueTree& incoming,bool userEdit)
{
    const bool recordUserEdit=userEdit && SoundHistory::onMessageThread();
    if(recordUserEdit && history->isPublishing())return false;
    const auto initialGeneration=history->generation();
    const int version=static_cast<int>(incoming.getProperty("schemaVersion",0));
    if(!incoming.hasType("ANYVOC") || version<1 || version>14)return false;
    const int incomingProgram = juce::jlimit (0, getNumPrograms() - 1, static_cast<int> (incoming.getProperty ("program", 0)));
    const bool hasName = incoming.hasProperty ("presetName"), hasId = incoming.hasProperty ("presetId");
    if (hasName != hasId) return false;
    juce::String restoredName = presets[static_cast<size_t> (incomingProgram)].name;
    juce::String restoredId = anyvoc::PresetLibrary::factoryId (restoredName);
    if (hasName)
    {
        restoredName = incoming.getProperty ("presetName").toString();
        restoredId = incoming.getProperty ("presetId").toString();
        if (restoredName.trim().isEmpty() || restoredName.length() > 512
            || restoredId.trim().isEmpty() || restoredId.length() > 4096) return false;
    }
    auto cleaned=apvts.copyState();
    for(int n=cleaned.getNumChildren();--n>=0;)if(!cleaned.getChild(n).hasType("PARAM"))cleaned.removeChild(n,nullptr);
    std::array<std::shared_ptr<const WaveAsset>,3> newAssets;
    std::array<std::shared_ptr<const anyvoc::Wavetable>,3> newTables;
    std::array<std::shared_ptr<const WaveAsset>,3> newSampleAssets;
    std::array<std::shared_ptr<const anyvoc::SampleData>,3> newSamples;
    juce::StringArray seenParameters;
    std::array<bool,3> seenTables{};
    std::array<bool,3> seenSamples{};
    for(int n=0;n<incoming.getNumChildren();++n){auto child=incoming.getChild(n);
        if(child.hasType("PARAM")){
            if(!child.hasProperty("id") || !child.hasProperty("value"))return false;
            auto id=child.getProperty("id").toString();auto* param=apvts.getParameter(id);if(param==nullptr || seenParameters.contains(id))return false;
            seenParameters.add(id);
            const auto number=child.getProperty("value").toString().trim();char* tail=nullptr;
            const float value=std::strtof(number.toRawUTF8(),&tail);if(number.isEmpty() || tail==nullptr || *tail!='\0' || !std::isfinite(value))return false;
            auto target=cleaned.getChildWithProperty("id",id);if(target.isValid())target.setProperty("value",param->convertFrom0to1(juce::jlimit(0.0f,1.0f,param->convertTo0to1(value))),nullptr);
        } else if(child.hasType("WAVETABLE")){
            const int osc=static_cast<int>(child.getProperty("osc",-1));if(osc<0 || osc>2)return false;
            if(seenTables[static_cast<size_t>(osc)])return false;seenTables[static_cast<size_t>(osc)]=true;
            auto asset=std::make_shared<WaveAsset>();asset->name=child.getProperty("name").toString().substring(0,120);
            if(version>=4 && child.hasProperty("normalise")){
                const auto flag=child.getProperty("normalise").toString();if(flag!="0" && flag!="1")return false;
                asset->normalise=flag=="1";
            }
            const auto encoded=child.getProperty("wav").toString();
            if(encoded.length()>6*1024*1024 || !asset->data.fromBase64Encoding(encoded) || asset->data.getSize()>4*1024*1024)return false;
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::make_unique<juce::MemoryInputStream>(asset->data,false)));
            if(reader==nullptr || reader->lengthInSamples<32 || reader->lengthInSamples>65536)return false;
            juce::AudioBuffer<float> wave(1,static_cast<int>(reader->lengthInSamples));if(!reader->read(&wave,0,wave.getNumSamples(),0,true,false))return false;
            if(!validWaveSamples(wave) || (!asset->normalise && (reader->numChannels!=1 || wave.getNumSamples()%2048!=0)))return false;
            newTables[static_cast<size_t>(osc)]=anyvoc::SynthEngine::makeWavetable(wave,juce::jmin(2048,wave.getNumSamples()),asset->normalise);
            if(!newTables[static_cast<size_t>(osc)])return false;newAssets[static_cast<size_t>(osc)]=asset;
        } else if(child.hasType("SAMPLE") && version>=2){
            const int osc=static_cast<int>(child.getProperty("osc",-1));if(osc<0 || osc>2 || seenSamples[static_cast<size_t>(osc)])return false;
            seenSamples[static_cast<size_t>(osc)]=true;
            auto asset=std::make_shared<WaveAsset>();asset->name=child.getProperty("name").toString().substring(0,120);
            const auto encoded=child.getProperty("wav").toString();
            if(encoded.length()>8*1024*1024 || !asset->data.fromBase64Encoding(encoded) || asset->data.getSize()>6*1024*1024)return false;
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::make_unique<juce::MemoryInputStream>(asset->data,false)));
            if(!reader || reader->lengthInSamples<2 || reader->lengthInSamples>maxSampleFrames || reader->numChannels<1 || reader->numChannels>2 || std::abs(reader->sampleRate-48000.0)>.01)return false;
            juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels),static_cast<int>(reader->lengthInSamples));
            if(!reader->read(&audio,0,audio.getNumSamples(),0,true,true))return false;
            newSamples[static_cast<size_t>(osc)]=anyvoc::SynthEngine::makeSample(audio,48000);
            if(!newSamples[static_cast<size_t>(osc)])return false;
            newSampleAssets[static_cast<size_t>(osc)]=asset;
        } else return false;
    }
    for(auto* base:getParameters())if(auto* param=dynamic_cast<juce::RangedAudioParameter*>(base)){
        if(seenParameters.contains(param->paramID))continue;
        if(!((version==1 && isSampleParameter(param->paramID)) || (version<3 && isLfoParameter(param->paramID)) || (version<5 && isDualFilterParameter(param->paramID)) || (version<6 && isWarpParameter(param->paramID)) || (version<7 && isVoicingParameter(param->paramID)) || (version<8 && isCrossModulationParameter(param->paramID)) || (version<9 && isAdvancedModulationParameter(param->paramID)) || (version<10 && isExpandedSourceParameter(param->paramID)) || (version<11 && isExtendedEnvelopeParameter(param->paramID)) || (version<12 && isLfoMotionParameter(param->paramID)) || (version<13 && isChaosLfoParameter(param->paramID)) || (version<14 && isMacroDestinationParameter(param->paramID))))return false;
        cleaned.getChildWithProperty("id",param->paramID).setProperty("value",param->convertFrom0to1(param->getDefaultValue()),nullptr);
    }
    if(recordUserEdit && (history->isPublishing() || history->generation()!=initialGeneration))return false;
    SoundHistory::ExternalPublication publication(history.get(),!recordUserEdit);
    if(recordUserEdit){
        history->fullSound();
        const auto generation=history->generation();
        for(auto* base:getParameters())if(auto* parameter=dynamic_cast<juce::RangedAudioParameter*>(base)){
            if(history->generation()!=generation)return false;
            history->write(*parameter,parameter->convertTo0to1(static_cast<float>(cleaned.getChildWithProperty("id",parameter->paramID).getProperty("value"))));
            if(history->generation()!=generation)return false;
        }
    }else {
        const auto generation=history->generation();
        apvts.replaceState(cleaned);
        if(history->generation()!=generation)return false;
        // A host may restore inside an outgoing parameter notification, before
        // APVTS's adapter sees that notification. Its cached raw value can still
        // equal the requested old value while the actual parameter has moved,
        // causing replaceState to skip that parameter. Reconcile the actual
        // values so the restored project wins when the old notification resumes.
        for(auto* base:getParameters())if(auto* parameter=dynamic_cast<juce::RangedAudioParameter*>(base)){
            const auto desired=parameter->convertTo0to1(static_cast<float>(cleaned.getChildWithProperty("id",parameter->paramID).getProperty("value")));
            if(!juce::approximatelyEqual(parameter->getValue(),desired))parameter->setValueNotifyingHost(desired);
            if(history->generation()!=generation)return false;
        }
    }
    for(int n=0;n<3;++n){const auto osc=static_cast<size_t>(n);
        std::atomic_store(&assets[osc],newAssets[osc]);std::atomic_store(&preparedTables[osc],newTables[osc]);engine.setUserWavetable(n,newTables[osc]);++wavetableRevisions[osc];
        std::atomic_store(&sampleAssets[osc],newSampleAssets[osc]);std::atomic_store(&preparedSamples[osc],newSamples[osc]);engine.setUserSample(n,newSamples[osc]);}
    currentProgram.store(incomingProgram);
    std::atomic_store (&presetIdentity, std::make_shared<const PresetIdentity> (PresetIdentity { restoredName, restoredId }));
    soundResetRequested.store(true);return true;
}

void AnyvocAudioProcessor::getStateInformation(juce::MemoryBlock& data){if(auto xml=createState().createXml())copyXmlToBinary(*xml,data);}
void AnyvocAudioProcessor::setStateInformation(const void* data,int size){if(size>0 && size<maxStateBytes)if(auto xml=getXmlFromBinary(data,size))restoreState(juce::ValueTree::fromXml(*xml));}
bool AnyvocAudioProcessor::savePreset(const juce::File& file)
{
    const auto name = file.getFileNameWithoutExtension();
    const auto id = anyvoc::PresetLibrary::userId (file);
    if (name.trim().isEmpty() || name.length() > 512 || id.length() > 4096 || file.isDirectory()) return false;
    auto state = createState();
    state.setProperty ("presetName", name, nullptr);
    state.setProperty ("presetId", id, nullptr);
    auto xml = state.createXml();
    if (! xml) return false;
    // A .tmp suffix keeps an in-progress save out of concurrent library scans.
    juce::TemporaryFile temporary (file, file.getParentDirectory().getNonexistentChildFile ("Anyvoc-save-" + juce::Uuid().toString(), ".tmp", false));
    if (! xml->writeTo (temporary.getFile()) || ! temporary.overwriteTargetFileWithTemporary()) return false;
    std::atomic_store (&presetIdentity, std::make_shared<const PresetIdentity> (PresetIdentity { name, id }));
    updateHostDisplay (ChangeDetails{}.withNonParameterStateChanged (true));
    return true;
}
bool AnyvocAudioProcessor::loadPreset(const juce::File& file)
{
    SoundHistory::Scope edit(history.get(),"Load "+file.getFileNameWithoutExtension());
    if (file.getSize() <= 0 || file.getSize() > maxStateBytes) return false;
    if (auto xml = juce::XmlDocument::parse (file))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid()) return false;
        state.setProperty ("presetName", file.getFileNameWithoutExtension(), nullptr);
        state.setProperty ("presetId", anyvoc::PresetLibrary::userId (file), nullptr);
        if (! restoreState (state,true)) return false;
        updateHostDisplay (ChangeDetails{}.withNonParameterStateChanged (true));
        return true;
    }
    return false;
}

bool AnyvocAudioProcessor::importWavetable(int oscillator,const juce::File& file)
{
    const bool userEdit=SoundHistory::onMessageThread();
    if(userEdit && history->isPublishing())return false;
    const auto initialGeneration=history->generation();
    SoundHistory::Scope edit(history.get(),"Import wavetable");
    if(oscillator<0 || oscillator>2)return false;
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if(!reader || reader->lengthInSamples<32 || reader->lengthInSamples>65536)return false;
    juce::AudioBuffer<float> wave(1,static_cast<int>(reader->lengthInSamples));if(!reader->read(&wave,0,wave.getNumSamples(),0,true,false))return false;
    if(!validWaveSamples(wave))return false;
    const bool normalise=reader->metadataValues.getValue(juce::WavAudioFormat::riffInfoComment,{})!=editableWaveComment;
    if(!normalise && (reader->numChannels!=1 || wave.getNumSamples()%2048!=0))return false;
    auto table=anyvoc::SynthEngine::makeWavetable(wave,juce::jmin(2048,wave.getNumSamples()),normalise);if(!table)return false;
    auto asset=encodeWavetable(wave,file.getFileNameWithoutExtension(),normalise);if(!asset)return false;
    if(userEdit && (history->isPublishing() || history->generation()!=initialGeneration))return false;
    SoundHistory::ExternalPublication publication(history.get(),!userEdit);
    std::atomic_store(&assets[static_cast<size_t>(oscillator)],asset);
    std::atomic_store(&preparedTables[static_cast<size_t>(oscillator)],table);
    engine.setUserWavetable(oscillator,table);++wavetableRevisions[static_cast<size_t>(oscillator)];
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));return true;
}
juce::AudioBuffer<float> AnyvocAudioProcessor::getEditableWavetable(int oscillator) const
{
    if(oscillator<0 || oscillator>2)return {};
    return engine.copyWavetableAudio(oscillator);
}
std::shared_ptr<const AnyvocAudioProcessor::WaveAsset> AnyvocAudioProcessor::encodeWavetable(const juce::AudioBuffer<float>& audio,const juce::String& name,bool normalise)
{
    auto asset=std::make_shared<WaveAsset>();asset->name=name.substring(0,120);asset->normalise=normalise;
    juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> stream=std::make_unique<juce::MemoryOutputStream>(asset->data,false);
    auto options=juce::AudioFormatWriterOptions{}.withSampleRate(44100).withNumChannels(1).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    if(!normalise)options=options.withMetadata(juce::WavAudioFormat::riffInfoComment,editableWaveComment);
    auto writer=wav.createWriterFor(stream,options);
    if(!writer || !writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()))return {};
    writer.reset();return asset;
}
bool AnyvocAudioProcessor::applyEditedWavetable(int oscillator,const juce::AudioBuffer<float>& wave,const juce::String& name)
{
    const bool userEdit=SoundHistory::onMessageThread();
    if(userEdit && history->isPublishing())return false;
    const auto initialGeneration=history->generation();
    SoundHistory::Scope edit(history.get(),"Edit wavetable");
    if(oscillator<0 || oscillator>2 || !validWaveSamples(wave) || wave.getNumSamples()%2048!=0)return false;
    auto table=anyvoc::SynthEngine::makeWavetable(wave,2048,false);if(!table)return false;
    auto asset=encodeWavetable(wave,name.trim().isEmpty()?"Edited Wavetable":name,false);if(!asset)return false;
    if(userEdit && (history->isPublishing() || history->generation()!=initialGeneration))return false;
    SoundHistory::ExternalPublication publication(history.get(),!userEdit);
    std::atomic_store(&assets[static_cast<size_t>(oscillator)],asset);
    std::atomic_store(&preparedTables[static_cast<size_t>(oscillator)],table);
    engine.setUserWavetable(oscillator,table);++wavetableRevisions[static_cast<size_t>(oscillator)];
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));return true;
}
bool AnyvocAudioProcessor::exportWavetable(int oscillator,const juce::File& file) const
{
    const auto wave=getEditableWavetable(oscillator);if(!validWaveSamples(wave))return false;
    const auto asset=encodeWavetable(wave,getWavetableName(oscillator),false);if(!asset)return false;
    juce::TemporaryFile temporary(file);
    return temporary.getFile().replaceWithData(asset->data.getData(),asset->data.getSize()) && temporary.overwriteTargetFileWithTemporary();
}
juce::String AnyvocAudioProcessor::getWavetableName(int oscillator) const {if(oscillator>=0 && oscillator<3)if(auto asset=std::atomic_load(&assets[static_cast<size_t>(oscillator)]))return asset->name;return "Basic Shapes";}
float AnyvocAudioProcessor::getWavetableSample(int oscillator,float phase,float position) const {return engine.getWavetableSample(oscillator,phase,position);}
float AnyvocAudioProcessor::getWarpedWavetableSample(int oscillator,float phase,float position) const
{
    if(oscillator<0 || oscillator>=3)return 0.0f;
    const auto osc=static_cast<size_t>(oscillator);
    std::array<int,2> modes{};std::array<float,2> amounts{};
    // A static cycle cannot represent live cross-oscillator signals. Only
    // classic slots are shown; the UI identifies the omitted live modulation.
    for(size_t slot=0;slot<2;++slot){modes[slot]=v(crossModes[osc][slot])>.5f?0:static_cast<int>(v(warpModes[osc][slot]));amounts[slot]=v(warpAmounts[osc][slot]);}
    return engine.getWarpedWavetableSample(oscillator,phase,position,modes,amounts);
}
int AnyvocAudioProcessor::getWarpMode(int oscillator,int slot) const
{
    if(oscillator<0 || oscillator>=3 || slot<0 || slot>=2)return 0;
    const auto osc=static_cast<size_t>(oscillator),stage=static_cast<size_t>(slot);
    const int cross=static_cast<int>(v(crossModes[osc][stage]));
    return cross>0?6+cross:static_cast<int>(v(warpModes[osc][stage]));
}
void AnyvocAudioProcessor::setWarpMode(int oscillator,int slot,int mode)
{
    if(oscillator<0 || oscillator>=3 || slot<0 || slot>=2 || mode<0 || mode>10)return;
    if(SoundHistory::onMessageThread() && history->isPublishing())return;
    if(getWarpMode(oscillator,slot)==mode)return;
    SoundHistory::Scope edit(history.get(),"Change warp mode");
    const auto generation=history->generation();
    const auto prefix="osc"+juce::String(oscillator+1)+"Warp"+juce::String(slot+1);
    const std::array<juce::RangedAudioParameter*,2> parameters{apvts.getParameter(prefix+"Mode"),apvts.getParameter(prefix+"CrossMode")};
    std::array<float,2> expected{parameters[0]->getValue(),parameters[1]->getValue()};
    auto stillCurrent=[&]{
        return history->generation()==generation
            && juce::approximatelyEqual(parameters[0]->getValue(),expected[0])
            && juce::approximatelyEqual(parameters[1]->getValue(),expected[1]);
    };
    auto set=[&](size_t index,int value){
        if(!stillCurrent())return false;
        auto* parameter=parameters[index];
        const auto next=parameter->convertTo0to1(static_cast<float>(value));
        if(juce::approximatelyEqual(parameter->getValue(),next))return true;
        parameter->beginChangeGesture();
        if(stillCurrent()){
            if(SoundHistory::onMessageThread())history->write(*parameter,next);else parameter->setValueNotifyingHost(next);
            expected[index]=next;
        }
        parameter->endChangeGesture();
        return stillCurrent();
    };
    if(mode<=6){if(set(0,mode))set(1,0);}
    else set(1,mode-6);
}

std::shared_ptr<const AnyvocAudioProcessor::WaveAsset> AnyvocAudioProcessor::encodeSample(const juce::AudioBuffer<float>& audio,const juce::String& name)
{
    auto asset=std::make_shared<WaveAsset>();asset->name=name.substring(0,120);
    juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> stream=std::make_unique<juce::MemoryOutputStream>(asset->data,false);
    auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(audio.getNumChannels()).withBitsPerSample(24));
    if(!writer || !writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()))return {};
    writer.reset();return asset;
}

bool AnyvocAudioProcessor::importSample(int oscillator,const juce::File& file)
{
    const bool userEdit=SoundHistory::onMessageThread();
    if(userEdit && history->isPublishing())return false;
    const auto initialGeneration=history->generation();
    SoundHistory::Scope edit(history.get(),"Import sample");
    if(oscillator<0 || oscillator>2 || file.getSize()>64*1024*1024)return false;
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if(!reader || reader->numChannels<1 || reader->numChannels>2 || reader->sampleRate<8000 || reader->sampleRate>192000 || reader->lengthInSamples<2 || reader->lengthInSamples>reader->sampleRate*20)return false;
    juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels),static_cast<int>(reader->lengthInSamples));
    if(!reader->read(&audio,0,audio.getNumSamples(),0,true,true))return false;
    for(int c=0;c<audio.getNumChannels();++c)for(int n=0;n<audio.getNumSamples();++n){
        const float value=audio.getSample(c,n);if(!std::isfinite(value))return false;
        audio.setSample(c,n,juce::jlimit(-1.0f,1.0f,value));
    }
    auto normalized=toSampleRate48k(audio,reader->sampleRate);
    auto sample=anyvoc::SynthEngine::makeSample(normalized,48000);if(!sample)return false;
    auto asset=encodeSample(normalized,file.getFileNameWithoutExtension());if(!asset)return false;
    if(userEdit && (history->isPublishing() || history->generation()!=initialGeneration))return false;
    SoundHistory::ExternalPublication publication(history.get(),!userEdit);
    std::atomic_store(&sampleAssets[static_cast<size_t>(oscillator)],asset);
    std::atomic_store(&preparedSamples[static_cast<size_t>(oscillator)],sample);
    engine.setUserSample(oscillator,sample);
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));return true;
}
juce::String AnyvocAudioProcessor::getSampleName(int oscillator) const
{
    if(oscillator>=0 && oscillator<3)if(auto asset=std::atomic_load(&sampleAssets[static_cast<size_t>(oscillator)]))return asset->name;
    return "Drop a WAV sample";
}
float AnyvocAudioProcessor::getSamplePreview(int oscillator,float position) const {return engine.getSamplePreview(oscillator,position);}
std::array<float,3> AnyvocAudioProcessor::getLfoChaosPoint(int index, int expectedShape, int expectedMode) const noexcept
{
    if(index<0 || index>=static_cast<int>(lfoRates.size()))return {};
    // The UI can observe a host edit before the next audio block applies it.
    // Do not label a snapshot from the preceding algorithm or mode as current.
    // A caller displaying captured labels must filter against those same labels.
    // Reading APVTS again here could otherwise admit a newer, differently tagged point.
    const int shape = expectedShape < 0 ? getLfoShape(index) : expectedShape;
    const int mode = expectedMode < 0 ? static_cast<int>(v(lfoMode[static_cast<size_t>(index)])) : expectedMode;
    return engine.getLfoChaosPoint(index, shape, mode);
}
int AnyvocAudioProcessor::getLfoShape(int index) const
{
    if(index<0 || index>=static_cast<int>(lfoRates.size()))return 0;
    const auto n=static_cast<size_t>(index);const int kind=static_cast<int>(v(lfoKind[n]));
    const int chaos=static_cast<int>(v(lfoChaos[n]));
    if(chaos>0)return chaos+5;
    return kind==0?static_cast<int>(v(lfoShapes[n])):kind+3;
}
void AnyvocAudioProcessor::setLfoShape(int index,int shape,const std::function<bool()>& isCurrent)
{
    if(index<0 || index>=static_cast<int>(lfoRates.size()) || shape<0 || shape>7)return;
    const auto id="lfo"+juce::String(index+1);
    auto* basic=apvts.getParameter(id+"Shape");auto* kind=apvts.getParameter(id+"Kind");auto* chaos=apvts.getParameter(id+"Chaos");
    applyModulationEdits("Change LFO shape",{
        {basic,shape<4?basic->convertTo0to1(static_cast<float>(shape)):basic->getValue()},
        {kind,shape<6?kind->convertTo0to1(static_cast<float>(shape<4?0:shape-3)):kind->getValue()},
        {chaos,chaos->convertTo0to1(static_cast<float>(shape>=6?shape-5:0))}
    },isCurrent);
}
int AnyvocAudioProcessor::getLfoRateMode(int index) const
{
    if(index<0 || index>=static_cast<int>(lfoRates.size()))return 0;
    const auto n=static_cast<size_t>(index);
    return v(lfoSync[n])>.5f?1:(v(lfoAudioRateEnabled[n])>.5f?2:0);
}
void AnyvocAudioProcessor::setLfoRateMode(int index,int mode,const std::function<bool()>& isCurrent)
{
    if(index<0 || index>=static_cast<int>(lfoRates.size()) || mode<0 || mode>2)return;
    const auto id="lfo"+juce::String(index+1);
    auto* sync=apvts.getParameter(id+"Sync");auto* audio=apvts.getParameter(id+"AudioRateEnabled");
    // Sync has precedence, including when an older host automates it directly.
    // Publish the destination flag before releasing the previous rate mode.
    if(mode==1)applyModulationEdits("Change LFO rate mode",{{sync,1.0f},{audio,0.0f}},isCurrent);
    else applyModulationEdits("Change LFO rate mode",{{audio,mode==2?1.0f:0.0f},{sync,0.0f}},isCurrent);
}
int AnyvocAudioProcessor::getModulationSource(int route,bool auxiliary) const
{
    if(route<0 || route>=static_cast<int>(routeParams.size()))return 0;
    const auto& p=routeParams[static_cast<size_t>(route)];
    const auto extra=static_cast<int>(v(auxiliary?p.auxSourceExtra:p.sourceExtra));
    return extra>0?extra+9:static_cast<int>(v(auxiliary?p.auxSource:p.source));
}
void AnyvocAudioProcessor::setModulationSource(int route,int source,bool auxiliary,const std::function<bool()>& isCurrent)
{
    if(route<0 || route>=static_cast<int>(routeParams.size()) || source<0 || source>=static_cast<int>(anyvoc::modulationSourceCount))return;
    const auto id="mod"+juce::String(route+1)+(auxiliary?"AuxSource":"Source");
    auto* legacy=apvts.getParameter(id);auto* extra=apvts.getParameter(id+"Extra");
    // Both selectors participate in stale-write checks, even if the remembered
    // legacy value is inactive. Switch the effective bank after writing its value.
    applyModulationEdits(auxiliary?"Change auxiliary source":"Change modulation source",{
        {legacy,source<10?legacy->convertTo0to1(static_cast<float>(source)):legacy->getValue()},
        {extra,extra->convertTo0to1(static_cast<float>(source<10?0:source-9))}
    },isCurrent);
}
int AnyvocAudioProcessor::getModulationDestination(int route) const
{
    if(route<0 || route>=static_cast<int>(routeParams.size()))return 0;
    const auto& p=routeParams[static_cast<size_t>(route)];
    if(v(p.macroEnabled)>.5f)return anyvoc::macroDestinationId(static_cast<int>(v(p.macroTarget)));
    if(v(p.lfoPhaseEnabled)>.5f)return 18+static_cast<int>(v(p.lfoPhaseTarget));
    if(v(p.warpEnabled)>.5f)return 12+static_cast<int>(v(p.warpTarget));
    return v(p.bank)>.5f?8+static_cast<int>(v(p.filterTarget)):static_cast<int>(v(p.dest));
}
void AnyvocAudioProcessor::setModulationDestination(int route,int destination,const std::function<bool()>& isCurrent)
{
    if(route<0 || route>=static_cast<int>(routeParams.size()) || destination<0 || destination>=static_cast<int>(anyvoc::modulationDestinationCount))return;
    const auto id="mod"+juce::String(route+1);
    std::vector<std::pair<juce::RangedAudioParameter*,float>> edits;
    // Guard all destination selectors, including the remembered inactive ones.
    for(const auto* suffix:{"Dest","FilterTarget","DestBank","WarpTarget","WarpEnabled","LfoPhaseTarget","LfoPhaseEnabled","MacroTarget","MacroEnabled"}){
        auto* parameter=apvts.getParameter(id+suffix);edits.emplace_back(parameter,parameter->getValue());
    }
    auto set=[&](size_t index,int plain){
        edits[index].second=edits[index].first->convertTo0to1(static_cast<float>(plain));
    };
    const int macro=anyvoc::macroIndexForDestination(destination);
    if(macro>=0){set(7,macro);set(8,1);}
    else {
        if(destination>=18){set(5,destination-18);set(6,1);}
        else {
            if(destination>=12){set(3,destination-12);set(4,1);}
            else {
                if(destination<8){set(0,destination);set(2,0);}
                else {set(1,destination-8);set(2,1);}
                set(4,0);
            }
            set(6,0);
        }
        set(8,0);
    }
    applyModulationEdits("Change modulation destination",edits,isCurrent);
}

anyvoc::ModRoute AnyvocAudioProcessor::getModulationRoute(int route) const
{
    anyvoc::ModRoute result;
    if(route<0 || route>=static_cast<int>(routeParams.size()))return result;
    const auto& p=routeParams[static_cast<size_t>(route)];
    result.source=getModulationSource(route);result.destination=getModulationDestination(route);result.amount=v(p.amount);
    result.enabled=v(p.enabled)>.5f;result.polarity=static_cast<int>(v(p.polarity));result.curve=v(p.curve);
    result.auxSource=getModulationSource(route,true);result.auxPolarity=static_cast<int>(v(p.auxPolarity));result.auxCurve=v(p.auxCurve);
    return result;
}

bool AnyvocAudioProcessor::applyModulationEdits(const juce::String& label,
    const std::vector<std::pair<juce::RangedAudioParameter*,float>>& edits,const std::function<bool()>& isCurrent)
{
    if((isCurrent && !isCurrent()) || history->isPublishing())return false;
    const auto generation=history->generation();
    std::vector<float> expected;expected.reserve(edits.size());
    bool changed=false;
    for(const auto& edit:edits){
        if(edit.first==nullptr || !std::isfinite(edit.second) || edit.second<0 || edit.second>1)return false;
        expected.push_back(edit.first->getValue());changed=changed || expected.back()!=edit.second;
    }
    if(!changed)return true;
    SoundHistory::Scope transaction(history.get(),label);
    auto valid=[&]{
        if((isCurrent && !isCurrent()) || history->generation()!=generation || history->isPublishing())return false;
        for(size_t i=0;i<edits.size();++i)if(edits[i].first->getValue()!=expected[i])return false;
        return true;
    };
    for(size_t i=0;i<edits.size();++i){
        if(!valid())return false;
        if(expected[i]==edits[i].second)continue;
        auto& parameter=*edits[i].first;
        parameter.beginChangeGesture();
        if(valid()){
            expected[i]=edits[i].second;
            if(SoundHistory::onMessageThread())history->write(parameter,expected[i]);
            else parameter.setValueNotifyingHost(expected[i]);
        }
        parameter.endChangeGesture();
        if(!valid())return false;
    }
    return true;
}

void AnyvocAudioProcessor::resetModulationRoute(int route)
{
    if(route<0 || route>=static_cast<int>(routeParams.size()))return;
    const auto prefix="mod"+juce::String(route+1);
    std::vector<std::pair<juce::RangedAudioParameter*,float>> edits;
    for(const auto* suffix:modulationRouteSuffixes){
        auto* parameter=apvts.getParameter(prefix+suffix);edits.emplace_back(parameter,parameter->getDefaultValue());
    }
    applyModulationEdits("Remove modulation route",edits);
}

void AnyvocAudioProcessor::moveModulationRoute(int from,int to)
{
    const auto count=static_cast<int>(routeParams.size());
    if(from<0 || from>=count || to<0 || to>=count || from==to)return;
    std::vector<std::pair<juce::RangedAudioParameter*,float>> edits;
    for(int target=juce::jmin(from,to);target<=juce::jmax(from,to);++target){
        const int source=target==to?from:(from<to?target+1:target-1);
        for(const auto* suffix:modulationRouteSuffixes){
            auto* parameter=apvts.getParameter("mod"+juce::String(target+1)+suffix);
            auto* sourceParameter=apvts.getParameter("mod"+juce::String(source+1)+suffix);
            edits.emplace_back(parameter,sourceParameter->getValue());
        }
    }
    applyModulationEdits("Move modulation route",edits);
}
bool AnyvocAudioProcessor::assignModulation(int source,int destination,float amount,const std::function<bool()>& isCurrent)
{
    if(source<1 || source>=static_cast<int>(anyvoc::modulationSourceCount) || destination<1 || destination>=static_cast<int>(anyvoc::modulationDestinationCount) || !std::isfinite(amount))return false;
    if(history->isPublishing())return false;
    int free=-1;
    for(int n=0;n<static_cast<int>(routeParams.size());++n){
        const int s=getModulationSource(n),d=getModulationDestination(n);
        if(s==source && d==destination){
            // A second drop re-enables a bypassed match without replacing its shape.
            std::vector<std::pair<juce::RangedAudioParameter*,float>> edits;
            for(const auto* suffix:modulationRouteSuffixes){
                auto* parameter=apvts.getParameter("mod"+juce::String(n+1)+suffix);
                edits.emplace_back(parameter,juce::String(suffix)=="Enabled"?1.0f:parameter->getValue());
            }
            return applyModulationEdits("Enable modulation route",edits,isCurrent);
        }
        if(free<0 && (s==0 || d==0))free=n;
    }
    if(free<0)return false;
    const auto prefix="mod"+juce::String(free+1);
    std::vector<std::pair<juce::RangedAudioParameter*,float>> edits;
    // Initialise the entire vacant row; inactive curves/aux settings must not leak
    // from a previous route. Publish the selected source selector last.
    for(size_t i=1;i<modulationRouteSuffixes.size();++i){
        const juce::String suffix=modulationRouteSuffixes[i];
        if(suffix=="SourceExtra")continue;
        auto* parameter=apvts.getParameter(prefix+suffix);
        float plain=parameter->convertFrom0to1(parameter->getDefaultValue());
        if(suffix=="Amount")plain=juce::jlimit(-1.0f,1.0f,amount);
        else if(suffix=="Dest" && destination<8)plain=static_cast<float>(destination);
        else if(suffix=="DestBank")plain=destination>=8 && destination<12?1.0f:0.0f;
        else if(suffix=="FilterTarget" && destination>=8 && destination<12)plain=static_cast<float>(destination-8);
        else if(suffix=="WarpEnabled")plain=destination>=12 && destination<18?1.0f:0.0f;
        else if(suffix=="WarpTarget" && destination>=12 && destination<18)plain=static_cast<float>(destination-12);
        else if(suffix=="LfoPhaseEnabled")plain=anyvoc::lfoIndexForPhaseDestination(destination)>=0?1.0f:0.0f;
        else if(suffix=="LfoPhaseTarget" && anyvoc::lfoIndexForPhaseDestination(destination)>=0)plain=static_cast<float>(destination-18);
        else if(suffix=="MacroEnabled")plain=anyvoc::macroIndexForDestination(destination)>=0?1.0f:0.0f;
        else if(suffix=="MacroTarget" && anyvoc::macroIndexForDestination(destination)>=0)plain=static_cast<float>(anyvoc::macroIndexForDestination(destination));
        edits.emplace_back(parameter,parameter->convertTo0to1(plain));
    }
    auto* sourceParameter=apvts.getParameter(prefix+"Source");
    auto* extraParameter=apvts.getParameter(prefix+"SourceExtra");
    if(source<10){
        edits.emplace_back(extraParameter,extraParameter->getDefaultValue());
        edits.emplace_back(sourceParameter,sourceParameter->convertTo0to1(static_cast<float>(source)));
    }else{
        edits.emplace_back(sourceParameter,sourceParameter->getDefaultValue());
        edits.emplace_back(extraParameter,extraParameter->convertTo0to1(static_cast<float>(source-9)));
    }
    return applyModulationEdits("Assign modulation",edits,isCurrent);
}
anyvoc::MacroModulationSnapshot AnyvocAudioProcessor::getMacroModulationSnapshot() const noexcept
{
    const auto idleBase=[this]{
        anyvoc::MacroModulationSnapshot result;
        for(size_t i=0;i<macroParams.size();++i)
            result.values[i]=anyvoc::finiteModulationValue(v(macroParams[i]),0.0f,1.0f);
        return result;
    };
    const auto pending=[this]{return panicRequested.load() || soundResetRequested.load();};
    // Epoch comparison covers reset's flag-claim window and an entire reset
    // occurring between the two reads. This UI fallback never changes audio.
    const auto before=macroResetEpoch.load();
    if((before & 1u)!=0 || pending())return idleBase();
    const auto snapshot=engine.getMacroModulationSnapshot();
    if(before!=macroResetEpoch.load() || pending())return idleBase();
    return snapshot;
}
juce::AudioProcessorEditor* AnyvocAudioProcessor::createEditor(){return new AnyvocAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new AnyvocAudioProcessor();}
