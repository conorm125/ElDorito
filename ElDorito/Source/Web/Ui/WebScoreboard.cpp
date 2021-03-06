#include "WebScoreboard.hpp"
#include "ScreenLayer.hpp"
#include "../../Blam/BlamNetwork.hpp"
#include "../../Blam/BlamEvents.hpp"
#include "../../Blam/Tags/Objects/Damage.hpp"
#include "../../Patches/Events.hpp"
#include "../../Patches/Scoreboard.hpp"
#include "../../Patches/Input.hpp"
#include "../../Pointer.hpp"
#include "../../Modules/ModuleServer.hpp"
#include "../../ThirdParty/rapidjson/writer.h"
#include "../../ThirdParty/rapidjson/stringbuffer.h"
#include "../../Utils/String.hpp"
#include "../../ElDorito.hpp"

#include <iomanip>
#include "../../Blam/BlamObjects.hpp"
#include "../../Blam/Tags/Items/Weapon.hpp"

using namespace Blam::Input;
using namespace Blam::Events;

namespace
{
	bool locked = false;
	bool postgame = false;
	bool postgameScoreShown = false;
	time_t postgameDisplayed;
	const time_t postgameDisplayTime = 5;
	const time_t postgameDelayTime = 4.7;

	void OnEvent(Blam::DatumIndex player, const Event *event, const EventDefinition *definition);
	void OnGameInputUpdated();
	void OnScoreUpdate();
}

namespace Web
{
	namespace Ui
	{
		namespace WebScoreboard
		{
			void Init()
			{
				Patches::Events::OnEvent(OnEvent);
				Patches::Input::RegisterDefaultInputHandler(OnGameInputUpdated);
				Patches::Scoreboard::OnScoreUpdate(OnScoreUpdate);
			}

			void Show(bool locked, bool postgame)
			{
				rapidjson::StringBuffer jsonBuffer;
				rapidjson::Writer<rapidjson::StringBuffer> jsonWriter(jsonBuffer);

				jsonWriter.StartObject();
				jsonWriter.Key("locked");
				jsonWriter.Bool(locked);
				jsonWriter.Key("postgame");
				jsonWriter.Bool(postgame);
				jsonWriter.EndObject();

				ScreenLayer::Show("scoreboard", jsonBuffer.GetString());
			}

			void Hide()
			{
				ScreenLayer::Hide("scoreboard");
			}

			//Used to skip the 40 second wait at the end of a round.
			//It would probably be better to find the code that is causing the wait and fix it there.
			//This also fixes the black screen of death that happens sometimes at the end of a round. \o/
			void Tick()
			{
				if (postgame)
				{
					time_t curTime;
					time(&curTime);

					if (!postgameScoreShown && ((curTime - postgameDisplayed) > postgameDelayTime))
					{
						Web::Ui::WebScoreboard::Show(locked, postgame);
						postgameScoreShown = true;
					}

					if ((curTime - postgameDisplayed) > (postgameDisplayTime + postgameDelayTime))
					{
						auto session = Blam::Network::GetActiveSession();

						if (session)
							session->Parameters.SetSessionMode(1);

						postgame = false;
						postgameScoreShown = false;
					}
				}
			}

			std::string getScoreboard()
			{
				rapidjson::StringBuffer buffer;
				rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

				writer.StartObject();

				auto session = Blam::Network::GetActiveSession();
				if (!session || !session->IsEstablished())
				{
					writer.EndObject();
					return buffer.GetString();
				}
				writer.Key("playersInfo");
				writer.String(Modules::ModuleServer::Instance().VarPlayersInfoClient->ValueString.c_str());

				writer.Key("hasTeams");
				writer.Bool(session->HasTeams());
				writer.Key("teamScores");
				writer.StartArray();

				auto engineGlobalsPtr = ElDorito::GetMainTls(0x48);
				if (engineGlobalsPtr)
				{
					auto engineGobals = engineGlobalsPtr[0](0x101F4);
					for (int t = 0; t < 10; t++)
					{
						auto teamscore = engineGobals(t * 0x1A).Read<Blam::TEAM_SCORE>();
						writer.Int(teamscore.Score);
					}
				}
				writer.EndArray();

				int32_t variantType = Pointer(0x023DAF18).Read<int32_t>();

				if (variantType >= 0 && variantType < Blam::GameTypeCount)
				{
					writer.Key("gameType");
					writer.String(Blam::GameTypeNames[variantType].c_str());
				}

				auto& playersGlobal = ElDorito::GetMainTls(0x40)[0];
				uint32_t playerStatusBase = 0x2161808;

				writer.Key("players");
				writer.StartArray();
				int playerIdx = session->MembershipInfo.FindFirstPlayer();
				while (playerIdx != -1)
				{
					auto player = session->MembershipInfo.PlayerSessions[playerIdx];
					auto playerStats = Blam::Players::GetStats(playerIdx);
					int16_t score = playersGlobal(0x54 + 0x4046C + (playerIdx * 0x34)).Read<int16_t>();
					int16_t kills = playersGlobal(0x54 + 0x40470 + (playerIdx * 0x34)).Read<int16_t>();
					writer.StartObject();
					// Player information
					writer.Key("name");
					writer.String(Utils::String::ThinString(player.Properties.DisplayName).c_str());
					writer.Key("team");
					writer.Int(player.Properties.TeamIndex);
					std::stringstream color;
					color << "#" << std::setw(6) << std::setfill('0') << std::hex << player.Properties.Customization.Colors[Blam::Players::ColorIndices::Primary];
					writer.Key("color");
					writer.String(color.str().c_str());
					std::string uidStr;
					Utils::String::BytesToHexString(&player.Properties.Uid, sizeof(uint64_t), uidStr);
					writer.Key("UID");
					writer.String(uidStr.c_str());
					writer.Key("isHost");
					writer.Bool(playerIdx == session->MembershipInfo.HostPeerIndex);
					uint8_t alive = Pointer(playerStatusBase + (176 * playerIdx)).Read<uint8_t>();
					writer.Key("isAlive");
					writer.Bool(alive == 1);
					// Generic score information
					writer.Key("kills");
					writer.Int(kills);
					writer.Key("assists");
					writer.Int(playerStats.Assists);
					writer.Key("deaths");
					writer.Int(playerStats.Deaths);
					writer.Key("score");
					writer.Int(score);
					writer.Key("playerIndex");
					writer.Int(playerIdx);
					writer.Key("timeSpentAlive");
					writer.Int(playerStats.TimeSpentAlive);
					writer.Key("bestStreak");
					writer.Int(playerStats.BestStreak);

					bool hasObjective = false;
					auto playerDatum = &Blam::Players::GetPlayers()[playerIdx];
					auto playerObject = Pointer(Blam::Objects::GetObjects()[playerDatum->SlaveUnit].Data);
					if(playerObject)
					{
						auto equippedWeaponIndex = playerObject(0x2CA).Read<uint8_t>();
						if (equippedWeaponIndex != -1)
						{
							auto equippedWeaponObjectIndex = playerObject(0x2D0 + 4 * equippedWeaponIndex).Read<uint32_t>();
							auto equippedWeaponObjectPtr = Pointer(Blam::Objects::GetObjects()[equippedWeaponObjectIndex].Data);
							if (equippedWeaponObjectPtr)
							{
								auto weap = Blam::Tags::TagInstance(Pointer(equippedWeaponObjectPtr).Read<uint32_t>()).GetDefinition<Blam::Tags::Items::Weapon>();
								hasObjective = weap->MultiplayerWeaponType != Blam::Tags::Items::Weapon::MultiplayerType::None;
							}
						}
					}

					writer.Key("hasObjective");
					writer.Bool(hasObjective);

					//gametype specific stats
					writer.Key("flagKills");
					writer.Int(playerStats.WeaponStats[Blam::Tags::Objects::DamageReportingType::Flag].Kills);
					writer.Key("ballKills");
					writer.Int(playerStats.WeaponStats[Blam::Tags::Objects::DamageReportingType::Ball].Kills);

					writer.Key("kingsKilled");
					writer.Int(playerStats.KingsKilled);
					writer.Key("timeInHill");
					writer.Int(playerStats.TimeInHill);
					writer.Key("timeControllingHill");
					writer.Int(playerStats.TimeControllingHill);

					writer.Key("humansInfected");
					writer.Int(playerStats.HumansInfected);
					writer.Key("zombiesKilled");
					writer.Int(playerStats.ZombiesKilled);

					writer.EndObject();
					playerIdx = session->MembershipInfo.FindNextPlayer(playerIdx);
				}
				writer.EndArray();
				writer.EndObject();

				return buffer.GetString();
			}
		}
	}
}

namespace
{
	void OnEvent(Blam::DatumIndex player, const Event *event, const EventDefinition *definition)
	{
		//Update the scoreboard whenever an event occurs
		Web::Ui::ScreenLayer::Notify("scoreboard", Web::Ui::WebScoreboard::getScoreboard(), true);

		if (event->NameStringId == 0x4004D)// "general_event_game_over"
		{
			time(&postgameDisplayed);
			locked = true;
			postgame = true;
		}
	}

	void OnScoreUpdate()
	{
		//Update the scoreboard whenever an event occurs
		Web::Ui::ScreenLayer::Notify("scoreboard", Web::Ui::WebScoreboard::getScoreboard(), true);
	}

	void OnGameInputUpdated()
	{
		BindingsTable bindings;
		GetBindings(0, &bindings);

		if (GetKeyTicks(bindings.PrimaryKeys[eGameActionUiSelect], eInputTypeUi) == 1 || GetActionState(eGameActionUiSelect)->Ticks == 1 || GetKeyTicks(bindings.SecondaryKeys[eGameActionUiSelect], eInputTypeUi) == 1)
		{
			if (strcmp((char*)Pointer(0x22AB018)(0x1A4), "mainmenu") != 0)
			{
				//If shift is held down then lock the scoreboard
				if (GetKeyTicks(eKeyCodeShift, eInputTypeUi) == 0)
					locked = false;
				else
					locked = true;
				Web::Ui::WebScoreboard::Show(locked, postgame);
			}
		}

		if (!locked && !postgame)
		{
			//Hide the scoreboard when you release tab. Only check when the scoreboard isn't locked.
			if (GetKeyTicks(bindings.PrimaryKeys[eGameActionUiSelect], eInputTypeUi) == 0 && GetActionState(eGameActionUiSelect)->Ticks == 0 && GetKeyTicks(bindings.SecondaryKeys[eGameActionUiSelect], eInputTypeUi) == 0)
			{
				Web::Ui::WebScoreboard::Hide();
				locked = false;
			}
		}
	}
}