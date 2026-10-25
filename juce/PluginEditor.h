#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

// A interface é a página web/tm.html (a mesma da versão MPC), com fetch ligado direto ao núcleo.
class TmEditor : public juce::AudioProcessorEditor
{
public:
    explicit TmEditor (TmProcessor&);
    void resized() override { web.setBounds (getLocalBounds()); }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xfff4f1ea)); }

private:
    TmProcessor& proc;
    juce::WebBrowserComponent web;
    static juce::String page();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TmEditor)
};
