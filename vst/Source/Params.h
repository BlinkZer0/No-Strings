#pragma once
// ============================================================================
//  JUCE side of the parameter table (core/ParamTable.h): builds the APVTS
//  layout. The UI's params.js is generated from the same table.
// ============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamTable.h"

namespace nostrings
{
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& d : paramTable())
    {
        const juce::ParameterID pid { d.id, 1 };
        switch (d.kind)
        {
            case ParamKind::Choice:
            {
                juce::StringArray choices;
                for (const auto& c : d.choices) choices.add (c);
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, d.name, choices, (int) d.def));
                break;
            }
            case ParamKind::Int:
                layout.add (std::make_unique<juce::AudioParameterInt> (pid, d.name, (int) d.min, (int) d.max, (int) d.def));
                break;
            case ParamKind::Float:
            {
                juce::NormalisableRange<float> range (d.min, d.max);
                if (d.logSkew) range.setSkewForCentre (std::sqrt (d.min * d.max));
                layout.add (std::make_unique<juce::AudioParameterFloat> (pid, d.name, range, d.def,
                    juce::AudioParameterFloatAttributes().withLabel (d.unit == "%" ? "" : d.unit)));
                break;
            }
        }
    }
    return layout;
}
} // namespace nostrings
