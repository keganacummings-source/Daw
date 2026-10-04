#include "PluginProcessor.h"
#include "PluginEditor.h"

DreamShareProcessor::DreamShareProcessor()
#if DS_AUDIO_PASSTHROUGH
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
#else
    : AudioProcessor (BusesProperties())      // no audio buses at all
#endif
{
}

bool DreamShareProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
#if DS_AUDIO_PASSTHROUGH
    return l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
#else
    return l.inputBuses.isEmpty() && l.outputBuses.isEmpty();
#endif
}

juce::AudioProcessorEditor* DreamShareProcessor::createEditor() { return new DreamShareEditor (*this); }

// Only the window size is saved in the project. The login token is NOT stored in project files.
void DreamShareProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream out (dest, false);
    out.writeInt (editorW);
    out.writeInt (editorH);
}

void DreamShareProcessor::setStateInformation (const void* data, int size)
{
    if (size < 8) return;
    juce::MemoryInputStream in (data, (size_t) size, false);
    editorW = juce::jlimit (300, 1000, in.readInt());
    editorH = juce::jlimit (340, 1200, in.readInt());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DreamShareProcessor(); }
