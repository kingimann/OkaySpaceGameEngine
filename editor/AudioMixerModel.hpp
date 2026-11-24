#pragma once
// The Audio Mixer screen's model (okayui/screens/OkayUIPanels.hpp) over the engine's okay::AudioMixer: pointers into the mixer's own
// values, which the screen edits in place, and the output meters read now. One fill for both hosts of the screen: the editor's
// Audio Mixer window (editor/OkayPanels.cpp) and window mode (editor/OkayUIHostMode.cpp). Header only, so the window-mode tests that
// link OkayUIHostMode.cpp without the rest of the editor get it too.
#include "OkayUIPanels.hpp"
#include "okay/Audio/AudioMixer.hpp"

namespace oked {

inline void FillAudioMixerModel(okayuiscreens::AudioMixerModel& m) {
    using okay::AudioMixer;
    using okay::AudioGroup;
    m = okayuiscreens::AudioMixerModel();
    m.peakLeft = AudioMixer::peak[0]; m.peakRight = AudioMixer::peak[1];
    m.masterVolume = &AudioMixer::masterVolume; m.masterMuted = &AudioMixer::muted;
    for (int i = 0; i < (int)AudioGroup::Count; ++i) {
        m.groupNames.push_back(AudioMixer::GroupName((AudioGroup)i));
        m.groupVolume.push_back(&AudioMixer::groupVolume[i]);
        m.groupMuted.push_back(&AudioMixer::groupMuted[i]);
    }
    m.duckMusic = &AudioMixer::duckMusicUnderVoice; m.duckLevel = &AudioMixer::duckLevel;
    m.duckAttack = &AudioMixer::duckAttack; m.duckRelease = &AudioMixer::duckRelease; m.duckGain = AudioMixer::duckGain;
}

} // namespace oked
