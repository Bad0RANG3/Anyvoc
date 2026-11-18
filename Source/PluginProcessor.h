#pragma once
#include <JuceHeader.h>
#include "SynthEngine.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

class AnyvocAudioProcessor : public juce::AudioProcessor
{
public:
    AnyvocAudioProcessor();
    ~AnyvocAudioProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Anyvoc"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::StringArray getPresetNames() const;
    juce::String getLoadedPresetName() const;
    juce::String getLoadedPresetId() const;
    // Sound history and user setters are message-thread APIs. Host setters remain separate.
    void beginUserEdit(const juce::String& label);
    void endUserEdit();
    void setUserParameter(const juce::String& id, float plainValue);
    void selectFactoryPreset(int index);
    bool canUndo() const;
    bool canRedo() const;
    juce::String getUndoName() const;
    juce::String getRedoName() const;
    juce::Result undoUserEdit();
    juce::Result redoUserEdit();
    std::uint64_t getHistoryRevision() const;
    int getUndoCount() const;
    int getRedoCount() const;
    size_t getHistoryBytes() const;
    bool isUserEditActive() const;
    void setHistoryLimits(int maximumEntries, size_t maximumBytes);
    bool savePreset(const juce::File&);
    bool loadPreset(const juce::File&);
    bool importWavetable(int, const juce::File&);
    juce::AudioBuffer<float> getEditableWavetable(int oscillator) const;
    bool applyEditedWavetable(int oscillator, const juce::AudioBuffer<float>&, const juce::String& name);
    bool exportWavetable(int oscillator, const juce::File&) const;
    std::uint64_t getWavetableRevision(int oscillator) const { return oscillator>=0 && oscillator<3 ? wavetableRevisions[static_cast<size_t>(oscillator)].load() : 0; }
    juce::String getWavetableName(int) const;
    float getOutputLevel() const { return outputLevel.load(); }
    float getWavetableSample(int oscillator, float phase, float position) const;
    float getWarpedWavetableSample(int oscillator, float phase, float position) const;
    // Unified editor choice: classic 0..6, FM/PM/AM/RM 7..10. Indices are zero-based.
    int getWarpMode(int oscillator, int slot) const;
    void setWarpMode(int oscillator, int slot, int mode);
    bool importSample(int, const juce::File&);
    juce::String getSampleName(int) const;
    float getSamplePreview(int oscillator, float position) const;
    bool assignModulation(int source, int destination, float amount = .25f,
                          const std::function<bool()>& isCurrent = {});
    anyvoc::ModRoute getModulationRoute(int route) const;
    void resetModulationRoute(int route);
    // Clears the menu's routes (non-Off source, including bypassed rows) in one
    // publication/Undo. Off drafts are preserved. False means unavailable or
    // interrupted, not rollback; no matching routes is a successful no-op.
    bool resetModulationRoutesToDestination(int destination, const std::function<bool()>& isCurrent = {});
    // Moves the complete route and shifts intervening rows. Host IDs stay tied to rows.
    void moveModulationRoute(int from, int to);
    enum class ModulationCopyStatus { copied, emptySource, noEmptySlot, busy, cancelled, invalidRoute };
    struct ModulationCopyResult { ModulationCopyStatus status; int destination = -1; };
    // A row is empty only when all 19 stored fields equal their defaults.
    // Copies to the first such row without shifting or merging existing routes.
    bool isModulationRouteEmpty(int route) const;
    ModulationCopyResult copyModulationRoute(int source, const std::function<bool()>& isCurrent = {});
    int getModulationDestination(int route) const;
    void setModulationDestination(int route, int destination, const std::function<bool()>& isCurrent = {});
    // Effective source combines the frozen legacy choice and the appended selector.
    int getModulationSource(int route, bool auxiliary = false) const;
    void setModulationSource(int route, int source, bool auxiliary = false,
                             const std::function<bool()>& isCurrent = {});
    float getLfoPhase(int index) const { return engine.getLfoPhase(index); }
    std::array<float,3> getLfoChaosPoint(int index, int expectedShape = -1, int expectedMode = -1) const noexcept;
    int getLfoShape(int index) const;
    void setLfoShape(int index, int shape, const std::function<bool()>& isCurrent = {});
    int getLfoRateMode(int index) const;
    void setLfoRateMode(int index, int mode, const std::function<bool()>& isCurrent = {});
    anyvoc::MacroModulationSnapshot getMacroModulationSnapshot() const noexcept;
    float getTempo() const { return actualTempo.load(); }
    bool isTempoFromHost() const { return hostTempo.load(); }
    void panic() { panicRequested.store(true); }
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    anyvoc::SynthSettings readSettings();
    void readAudioModulationRoutes(std::array<anyvoc::ModRoute, anyvoc::modulationRouteCount>&);
    void publishModulationParameter(int parameterIndex) noexcept;
    class RoutePublicationScope;
    void processArp(juce::MidiBuffer&, int, double);
    void clearArp();
    void resetEngine();
    juce::ValueTree createState() const;
    bool restoreState(const juce::ValueTree&, bool userEdit = false);
    bool applyModulationEdits(const juce::String& label,
                             const std::vector<std::pair<juce::RangedAudioParameter*, float>>& normalizedEdits,
                             const std::function<bool()>& isCurrent = {},
                             const std::vector<float>* requiredBefore = nullptr);
    void applyProgram(int index, bool userEdit);
    class SoundHistory;
    std::unique_ptr<SoundHistory> history;
    struct PresetIdentity { juce::String name, id; };
    std::shared_ptr<const PresetIdentity> presetIdentity;
    struct WaveAsset { juce::String name; juce::MemoryBlock data; bool normalise=true; };
    std::array<std::shared_ptr<const WaveAsset>,3> assets;
    std::array<std::atomic<std::uint64_t>,3> wavetableRevisions{};
    std::array<std::shared_ptr<const WaveAsset>,3> sampleAssets;
    std::array<std::shared_ptr<const anyvoc::Wavetable>,3> preparedTables;
    std::array<std::shared_ptr<const anyvoc::SampleData>,3> preparedSamples;
    std::shared_ptr<const WaveAsset> factorySampleAsset;
    std::shared_ptr<const anyvoc::SampleData> factorySample;
    static std::shared_ptr<const WaveAsset> encodeSample(const juce::AudioBuffer<float>&, const juce::String&);
    static std::shared_ptr<const WaveAsset> encodeWavetable(const juce::AudioBuffer<float>&, const juce::String&, bool normalise);
    struct OscParams { std::atomic<float> *position,*level,*semi,*fine,*unison,*detune,*pan,*mode,*root,*loop,*reverse,*grainSize,*grainDensity,*grainSpray; };
    struct EnvParams {
        std::atomic<float> *attack,*decay,*sustain,*release,*delay,*hold,*attackCurve,*decayCurve,*releaseCurve,*tempoSync;
        std::array<std::atomic<float>*,5> divisions;
        std::atomic<float>* retriggerLegato;
    };
    struct RouteParams { std::atomic<float> *source,*dest,*amount,*bank,*filterTarget,*warpEnabled,*warpTarget,
        *enabled,*polarity,*curve,*auxSource,*auxPolarity,*auxCurve,*sourceExtra,*auxSourceExtra,*lfoPhaseEnabled,*lfoPhaseTarget,*macroEnabled,*macroTarget; };
    std::array<OscParams,3> oscParams;
    std::array<std::array<std::atomic<float>*,2>,3> warpModes,warpAmounts;
    std::array<std::array<std::atomic<float>*,2>,3> crossModes,crossSources;
    std::array<std::atomic<float>*,3> oscPhases,oscPhaseRandom,oscRandomUnison;
    std::atomic<float> *voiceMode,*glideTime,*glideAlways;
    std::array<EnvParams,4> envParams;
    std::array<RouteParams,anyvoc::modulationRouteCount> routeParams;
    // Fixed storage owned only by prepareToPlay/processBlock. A bounded read
    // keeps the last complete graph while a compound editor/state edit runs.
    std::array<anyvoc::ModRoute,anyvoc::modulationRouteCount> audioRoutes {};
    static constexpr size_t storedRouteFieldCount = 19;
    std::array<std::atomic<float>,anyvoc::modulationRouteCount * storedRouteFieldCount> publishedRouteFields {};
    std::vector<int> routeFieldForParameter; // Constructed once, never resized after listeners attach.
    std::atomic<std::uint32_t> routePublications { 0 };
    std::atomic<std::uint64_t> routePublicationRevision { 0 };
    std::array<std::atomic<float>*,anyvoc::lfoCount> lfoRates,lfoShapes;
    std::array<std::atomic<float>*,anyvoc::lfoCount> lfoSync,lfoDivision,lfoMode,lfoInterpolation,lfoKind;
    std::array<std::atomic<float>*,anyvoc::lfoCount> lfoPhaseOffset,lfoDirection,lfoFollowSwing,lfoAudioRateEnabled,lfoAudioRateHz;
    std::array<std::atomic<float>*,anyvoc::lfoCount> lfoChaos;
    std::array<std::array<std::atomic<float>*,33>,anyvoc::lfoCount> lfoPoints;
    std::atomic<float> *tempo,*swingAmount,*swingDivision;
    std::array<std::atomic<float>*,anyvoc::macroCount> macroParams;
    std::atomic<float> *sub,*noise,*cutoff,*resonance,*filterEnv,*filterType,*drive,*master;
    std::atomic<float> *filterEnabled,*filter2Enabled,*filter2Cutoff,*filter2Resonance,*filter2Env,*filter2Type;
    std::atomic<float> *filterRouting,*filterBalance,*filterSlope,*filter2Slope,*filterKeyTrack,*filter2KeyTrack;
    std::array<std::atomic<float>*,5> filterRoutes;
    std::atomic<float> *chorus,*delayMix,*delayTime,*delayFeedback,*reverb,*reverbSize;
    std::atomic<float> *arpEnabled,*arpRate,*arpOctaves,*arpGate;
    anyvoc::SynthEngine engine;
    juce::AudioFormatManager formats;
    juce::MidiBuffer arpMidi;
    std::array<float,128> held{};
    std::array<bool,128> keyDown{};
    bool arpWasEnabled=false,arpSustain=false,wasPlaying=false;
    int arpNote=-1,arpIndex=0;
    double stepRemaining=0,gateRemaining=0,sampleRate=44100;
    std::atomic<int> currentProgram{0};
    std::atomic<float> outputLevel{0};
    std::atomic<float> actualTempo{120};
    std::atomic<bool> hostTempo{false};
    std::atomic<bool> panicRequested{false};
    std::atomic<bool> soundResetRequested{false};
    std::atomic<std::uint64_t> macroResetEpoch{0};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnyvocAudioProcessor)
};
