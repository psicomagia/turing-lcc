#include "PluginProcessor.h"
#include "PluginEditor.h"

TmProcessor::TmProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Saida", juce::AudioChannelSet::stereo(), true))
{
    core = tm_new (44100.0, juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName().toRawUTF8());
    tm_set_page_info (core, "janela do plugin");
    in.reserve (1024);
    out.resize (1024);
    scratch.resize (8192);
}

TmProcessor::~TmProcessor() { tm_free (core); }

void TmProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    buffer.clear();

    TmHost h { 0, 0, 0.0, 120.0 };
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto ppq = pos->getPpqPosition()) { h.hasHost = 1; h.ppq = *ppq; h.playing = pos->getIsPlaying() ? 1 : 0; }
            if (auto bpm = pos->getBpm()) h.bpm = *bpm;
        }

    in.clear();
    for (const auto meta : midi)
    {
        if (meta.numBytes < 2 || meta.numBytes > 3 || in.size() >= 1024) continue;
        TmMidi m {};
        m.frame = meta.samplePosition;
        for (int i = 0; i < meta.numBytes; ++i) m.m[i] = meta.data[i];
        in.push_back (m);
    }
    if (! in.empty()) tm_midi_in (core, in.data(), (int) in.size());

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (R == nullptr)
    {
        if ((int) scratch.size() < n) return;   // nunca acontece com blocos normais
        std::fill (scratch.begin(), scratch.begin() + n, 0.0f);
        R = scratch.data();
    }
    int no = 0;
    tm_process (core, n, &h, L, R, out.data(), &no, (int) out.size());
    if (R == scratch.data()) juce::FloatVectorOperations::add (L, R, n);

    midi.clear();
    if (tm_midi_out_enabled (core))
        for (int i = 0; i < no; ++i)
            midi.addEvent (out[(size_t) i].m, 3, juce::jlimit (0, juce::jmax (0, n - 1), out[(size_t) i].frame));
}

juce::AudioProcessorEditor* TmProcessor::createEditor() { return new TmEditor (*this); }

juce::var TmProcessor::http (const juce::String& json)
{
    const auto v = juce::JSON::parse (json);
    TmResp r {};
    tm_http (core, v["m"].toString().toRawUTF8(), v["path"].toString().toRawUTF8(), v["q"].toString().toRawUTF8(),
             v["body"].toString().toRawUTF8(), &r);
    auto* o = new juce::DynamicObject();
    o->setProperty ("status", r.status);
    o->setProperty ("type", juce::String (r.type));
    o->setProperty ("body", juce::String::fromUTF8 (r.body, (int) r.len));
    free (r.body);
    return juce::var (o);
}

void TmProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    size_t n = 0;
    if (char* s = tm_state_save (core, &n)) { dest.append (s, n); free (s); }
}

void TmProcessor::setStateInformation (const void* data, int size)
{
    if (data != nullptr && size > 0) tm_state_load (core, static_cast<const char*> (data), (size_t) size);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TmProcessor(); }
