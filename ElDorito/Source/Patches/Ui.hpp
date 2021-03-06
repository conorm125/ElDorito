#pragma once
#include <cstdint>
#include <functional>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "../Blam/Text/StringID.hpp"

namespace Patches
{
	namespace Ui
	{
		void ApplyAll();
		void ApplyMapNameFixes();
		void ApplyUIResolution();

		typedef std::function<void(HWND window)> CreateWindowCallback;

		// Registers a function to be called when the game window is created.
		void OnCreateWindow(CreateWindowCallback callback);

		void *ShowDialog(const Blam::Text::StringID p_DialogID, const int32_t p_Arg1 = 0, const int32_t p_Flags = 4, const Blam::Text::StringID p_ParentID = 0);

		enum VoiceChatIcon
		{
			None,
			Speaking,
			Available,
			Unavailable,
			PushToTalk
		};

		void ToggleSpeakingPlayer(bool newSomeoneSpeaking);
		void SetSpeakingPlayer(std::string speakingPlayer);

		void SetVoiceChatIcon(VoiceChatIcon newIcon);
		void UpdateVoiceChatHUD();
		void ApplyAfterTagsLoaded();
	}
}
