#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>
#include <functional>
#include <string>

namespace {
enum class FxType { Gain, LowPass, HighPass, Air, Bass, Saturation, Limiter, Delay, Width, Modulation, LoFi, Gate, Tilt, Color };
FxType classify(const std::string& id) {
    auto has = [&](const char* s) { return id.find(s) != std::string::npos; };
    if (has("hpf") || has("highpass") || has("polishhp") || has("clean")) return FxType::HighPass;
    if (has("lpf") || has("lowpass") || has("haze") || has("fog") || has("underwater") || has("under")) return FxType::LowPass;
    if (has("air") || has("sheen") || has("sparkle") || has("wire") || has("presence") || has("edge")) return FxType::Air;
    if (has("bass") || has("sub") || has("lowend") || has("body") || has("punch") || has("mass")) return FxType::Bass;
    if (has("delay") || has("echo") || has("reverb") || has("verb") || has("room") || has("hall") || has("plate") || has("halo") || has("bloom") || has("ripple") || has("ping") || has("stage") || has("orbit") || has("seance") || has("choir") || has("comb") || has("slap") || has("predelay") || has("shimmer")) return FxType::Delay;
    if (has("width") || has("widen") || has("mono") || has("pan") || has("image") || has("balance") || has("stereo")) return FxType::Width;
    if (has("chorus") || has("flanger") || has("phaser") || has("vibrato") || has("trem") || has("pulse") || has("worm") || has("drift") || has("swirl") || has("rotary") || has("vortex") || has("wriggle")) return FxType::Modulation;
    if (has("crush") || has("lofi") || has("radio") || has("phone") || has("telephone") || has("bit")) return FxType::LoFi;
    if (has("gate") || has("stutter") || has("chop") || has("duck")) return FxType::Gate;
    if (has("tilt") || has("curve") || has("mud") || has("debox") || has("notch") || has("silk") || has("carve") || has("shelf") || has("formant")) return FxType::Tilt;
    if (has("drive") || has("grit") || has("sat") || has("clip") || has("rot") || has("melt") || has("cream") || has("velvet") || has("warm") || has("tape") || has("ink") || has("infection") || has("devil") || has("grind")) return FxType::Saturation;
    if (has("limit") || has("ceiling") || has("loud") || has("glue") || has("compress") || has("polish")) return FxType::Limiter;
    if (has("trim") || has("fade") || has("gain") || has("final")) return FxType::Gain;
    return FxType::Color;
}
}

DreamMasterLiteProcessor::DreamMasterLiteProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "PARAMETERS", createParams()) {}

juce::AudioProcessorValueTreeState::ParameterLayout DreamMasterLiteProcessor::createParams() {
    juce::AudioProcessorValueTreeState::ParameterLayout params;
    for (int i = 0; i < 200; ++i) {
        const auto idx = juce::String(i);
        params.add(std::make_unique<juce::AudioParameterBool>("fx" + idx, juce::String::fromUTF8(dm::featureNames[(size_t)i].data(), static_cast<int>(dm::featureNames[(size_t)i].size())), false));
        params.add(std::make_unique<juce::AudioParameterFloat>("amt" + idx, juce::String::fromUTF8(dm::featureNames[(size_t)i].data(), static_cast<int>(dm::featureNames[(size_t)i].size())) + " Amount", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.22f));
    }
    return params;
}

void DreamMasterLiteProcessor::prepareToPlay(double sampleRate, int) {
    currentSampleRate = sampleRate > 1000.0 ? sampleRate : 44100.0;
    filterState = {}; previousState = {}; delayPositions = {};
    for (int i = 0; i < 200; ++i) {
        const auto id = std::string(dm::featureIds[(size_t)i]);
        const auto type = classify(id);
        if (type == FxType::Delay) delayBuffers[(size_t)i].assign((size_t)juce::jlimit(2048, 24000, (int)(currentSampleRate * 0.24)), {0.0f, 0.0f});
        else delayBuffers[(size_t)i].clear();
    }
}
void DreamMasterLiteProcessor::releaseResources() {}

bool DreamMasterLiteProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return input == output && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void DreamMasterLiteProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    for (int i = 0; i < 200; ++i) {
        auto* enabled = state.getRawParameterValue("fx" + juce::String(i));
        auto* amountParam = state.getRawParameterValue("amt" + juce::String(i));
        if (enabled == nullptr || amountParam == nullptr || enabled->load() < 0.5f) continue;
        const float amount = juce::jlimit(0.0f, 1.0f, amountParam->load());
        const auto id = std::string(dm::featureIds[(size_t)i]);
        const FxType type = classify(id);
        const float unique = float((std::hash<std::string>{}(id) % 97) + 3) / 100.0f;
        auto& st = filterState[(size_t)i];
        auto& prev = previousState[(size_t)i];
        auto& ring = delayBuffers[(size_t)i];
        const float cutoff = 0.006f + amount * (0.06f + unique * 0.08f);
        for (int s = 0; s < samples; ++s) {
            float l = buffer.getSample(0, s);
            float r = channels > 1 ? buffer.getSample(1, s) : l;
            const float inL = l, inR = r;
            switch (type) {
                case FxType::Gain: { const float g = 1.0f + (amount - 0.22f) * 0.7f; l *= g; r *= g; break; }
                case FxType::LowPass:
                    st[0] += cutoff * (l - st[0]); st[1] += cutoff * (r - st[1]); l = l * (1.0f - amount * 0.55f) + st[0] * amount * 0.55f; r = r * (1.0f - amount * 0.55f) + st[1] * amount * 0.55f; break;
                case FxType::HighPass:
                    st[0] += cutoff * (l - st[0]); st[1] += cutoff * (r - st[1]); l -= st[0] * amount * 0.6f; r -= st[1] * amount * 0.6f; break;
                case FxType::Air: {
                    st[0] += 0.22f * (l - st[0]); st[1] += 0.22f * (r - st[1]);
                    l += (l - st[0]) * amount * (0.08f + unique * 0.12f); r += (r - st[1]) * amount * (0.08f + unique * 0.12f); break;
                }
                case FxType::Bass:
                    st[0] += 0.025f * (l - st[0]); st[1] += 0.025f * (r - st[1]);
                    l += st[0] * amount * 0.45f; r += st[1] * amount * 0.45f; break;
                case FxType::Saturation: {
                    const float drive = 1.0f + amount * (1.5f + unique * 3.0f);
                    const float norm = std::tanh(drive);
                    l = (1.0f - amount * 0.3f) * l + amount * 0.3f * (std::tanh(l * drive) / norm);
                    r = (1.0f - amount * 0.3f) * r + amount * 0.3f * (std::tanh(r * drive) / norm); break;
                }
                case FxType::Limiter: {
                    const float threshold = 0.96f - amount * 0.22f;
                    l = std::tanh(l / threshold) * threshold; r = std::tanh(r / threshold) * threshold; break;
                }
                case FxType::Delay: {
                    if (!ring.empty()) {
                        const size_t p = delayPositions[(size_t)i];
                        const size_t lag = (size_t)juce::jlimit(1, (int)ring.size() - 1, (int)(ring.size() * (0.12f + unique * 0.42f)));
                        const size_t rp = (p + ring.size() - lag) % ring.size();
                        const float dl = ring[rp][0], dr = ring[rp][1];
                        ring[p] = {inL + dl * amount * 0.25f, inR + dr * amount * 0.25f};
                        delayPositions[(size_t)i] = (p + 1) % ring.size();
                        l = inL * (1.0f - amount * 0.22f) + dl * amount * 0.22f;
                        r = inR * (1.0f - amount * 0.22f) + dr * amount * 0.22f;
                    } break;
                }
                case FxType::Width: {
                    const float mid = (l + r) * 0.5f, side = (l - r) * 0.5f * (1.0f + amount * (0.8f + unique * 0.4f));
                    l = mid + side; r = mid - side; break;
                }
                case FxType::Modulation: {
                    const float mod = 1.0f - amount * (0.08f + unique * 0.15f) * (0.5f + 0.5f * std::sin(float(s) * 0.035f + unique * 6.28f));
                    l *= mod; r *= mod; break;
                }
                case FxType::LoFi: {
                    const float steps = 16.0f + (1.0f - amount) * 240.0f;
                    l = std::round(l * steps) / steps; r = std::round(r * steps) / steps; break;
                }
                case FxType::Gate: {
                    const float threshold = 0.004f + amount * 0.055f;
                    if (std::abs(l) < threshold) l *= 1.0f - amount * 0.8f;
                    if (std::abs(r) < threshold) r *= 1.0f - amount * 0.8f; break;
                }
                case FxType::Tilt: {
                    st[0] += 0.015f * (l - st[0]); st[1] += 0.015f * (r - st[1]);
                    l += (st[0] - (l - st[0])) * amount * 0.12f; r += (st[1] - (r - st[1])) * amount * 0.12f; break;
                }
                case FxType::Color:
                default: {
                    st[0] += cutoff * (l - st[0]); st[1] += cutoff * (r - st[1]);
                    const float blend = amount * (0.08f + unique * 0.12f);
                    l = l * (1.0f - blend) + std::tanh(st[0] * (1.0f + unique * 2.0f)) * blend;
                    r = r * (1.0f - blend) + std::tanh(st[1] * (1.0f + unique * 2.0f)) * blend; break;
                }
            }
            // Conservative per-stage guard prevents runaway peaks when several effects are stacked.
            buffer.setSample(0, s, juce::jlimit(-1.25f, 1.25f, l));
            if (channels > 1) buffer.setSample(1, s, juce::jlimit(-1.25f, 1.25f, r));
            prev[0] = inL; prev[1] = inR;
        }
    }
    // Final safety ceiling. This prevents accidental digital overs, not loudness mastering.
    for (int ch = 0; ch < channels; ++ch) {
        auto* data = buffer.getWritePointer(ch);
        for (int s = 0; s < samples; ++s) data[s] = juce::jlimit(-0.98f, 0.98f, data[s]);
    }
}

void DreamMasterLiteProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = state.copyState().createXml()) copyXmlToBinary(*xml, dest);
}
void DreamMasterLiteProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(state.state.getType())) state.replaceState(juce::ValueTree::fromXml(*xml));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DreamMasterLiteProcessor(); }
essor(); }
