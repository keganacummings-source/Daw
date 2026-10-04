#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DreamClient.h"

// The compatibility build provides stereo I/O and leaves audio untouched.
#ifndef DS_AUDIO_PASSTHROUGH
 #define DS_AUDIO_PASSTHROUGH 1
#endif

class DreamShareProcessor : public juce::AudioProcessor
{
public:
    DreamShareProcessor();
    ~DreamShareProcessor() override = default;

    // --- deliberately empty: this plugin never touches audio or MIDI ---
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    const juce::String getName() const override { return "DreamShare Lite"; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    ds::Client& getClient() { return *client; }

    int editorW = 400, editorH = 580;

private:
    juce::SharedResourcePointer<ds::Client> client;   // one network thread for the whole process
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DreamShareProcessor)
};
