//****************************************************************//
// MQ2FeedMe.cpp
// based on snippet FeedMe 2.3 from A_Druid_00
// by s0rCieR 2005.12.10
//****************************************************************//
// 2010 pms - updates to work with HoT
// 2011 pms - updates to add announce and list
// 3.00 edited by: woobs 01/19/2014
//    Removed MQ2BagWindow dependency.
//    Removed the swapping of Food/Drink to consume.
//    Restructured to scan food/drink list, then check inventory
//    for match (was the reverse).
//    Add Food/Drink Warning toggles/displays.
// 3.01 edited by: MacQ 02/28/2014
//    Incorporated IsCasting(), AbilityInUse(), and CursorHasItem()
//    from moveitem.h.  Now moveitem.h no longer required.
// 4.0 edited by: Eqmule 07/26/2016 to add string safety
// 4.1 edited by: Sic 10/9/2019 to add a campcheck, so we don't interrupt camping to eat
// 4.2 edited by: Sic 12/31/2019 to update CursorHasItem() (with consultation from CWTN, and cleanup from Brainiac)
//****************************************************************//
// It will eat food and drink from lists you specify if your hunger
// or thirst levels fall below the thresholds you specifiy.  These
// lists/thresholds are set in the ini (MQ2FeedMe.ini).
//
// Lots of code stolen, credits goes to the author of those.
//****************************************************************//
// Usage: /autodrink       -> Force manual drinking
//        /autodrink 0     -> Turn off autodrinking
//        /autodrink 3500  -> Set Level where plugin should drink
//        /autodrink warn  -> Toggle Drink warnings on/off
//        /autodrink list  -> Display current drink list and levels
//
//        /autofeed        -> Force manual feeding
//        /autofeed 0      -> Turn off autofeeding
//        /autofeed 3500   -> Set Level where plugin should eat
//        /autofeed warn   -> Togle Food warnings on/off
//        /autofeed list   -> Display current food list and levels
//****************************************************************//

#include "mq/Plugin.h"
#include "mq/imgui/ImGuiUtils.h"

PreSetup("MQ2FeedMe");
PLUGIN_VERSION(4.2);
#define PLUGINMSG "\ar[\a-tFeedMe\ar]\ao:: "

bool         Loaded = false;                // List Loaded?

int          iFeedAt = 0;                   // Feed Level
int          iDrinkAt = 0;                  // Drink Level
std::vector<std::string> vFoodList;         // Hunger Fix List
std::vector<std::string> vDrinkList;        // Thirst Fix List

bool          bAnnounceLevels = true;            // Announce Levels
bool          bAnnounceConsume = true;           // Announce Consumption
bool          bFoodWarn = false;            // Announce No Food
bool          bDrinkWarn = false;           // Announce No Drink
bool          bIAmCamping = false;          // Defining if we are camping out or not
bool          bIgnoreSafeZones = false;     // Don't consume in "safe zones"

const char* PLUGIN_NAME = "MQ2FeedMe";

class MQ2FeedMeType : public MQ2Type
{
public:
	enum class FeedMeMembers
	{
		FeedAt,
		DrinkAt,
		Announce,
		FoodWarn,
		DrinkWarn,
		IgnoreSafeZones,
	};

	MQ2FeedMeType() : MQ2Type("FeedMe")
	{
		ScopedTypeMember(FeedMeMembers, FeedAt);
		ScopedTypeMember(FeedMeMembers, DrinkAt);
		ScopedTypeMember(FeedMeMembers, Announce);
		ScopedTypeMember(FeedMeMembers, FoodWarn);
		ScopedTypeMember(FeedMeMembers, DrinkWarn);
		ScopedTypeMember(FeedMeMembers, IgnoreSafeZones);
	};

	virtual bool GetMember(MQVarPtr VarPtr, const char* Member, char* Index, MQTypeVar& Dest) override
	{
		MQTypeMember* pMember = MQ2FeedMeType::FindMember(Member);
		if (!pMember)
		{
			return false;
		}

		Dest.Type = mq::datatypes::pBoolType;

		switch ((FeedMeMembers)pMember->ID)
		{
			case FeedMeMembers::FeedAt:
				Dest.Set(iFeedAt);
				Dest.Type = mq::datatypes::pIntType;
				return true;
			case FeedMeMembers::DrinkAt:
				Dest.Set(iDrinkAt);
				Dest.Type = mq::datatypes::pIntType;
				return true;
			case FeedMeMembers::Announce:
				Dest.Set(bAnnounceConsume);
				return true;
			case FeedMeMembers::FoodWarn:
				Dest.Set(bFoodWarn);
				return true;
			case FeedMeMembers::DrinkWarn:
				Dest.Set(bDrinkWarn);
				return true;
			case FeedMeMembers::IgnoreSafeZones:
				Dest.Set(bIgnoreSafeZones);
				return true;
			default:
				break;
		}
		return false;
	}
};

MQ2FeedMeType* pFeedMeType = nullptr;

bool dataFeedMe(const char* szIndex, MQTypeVar& Dest)
{
	Dest.DWord = 1;
	Dest.Type = pFeedMeType;
	return true;
}

bool InSafeZone()
{
	// if bufftimers are on hold we're assumign we're in a safe zone
	// this has the additional beneficial side affect of us not needing to maintain a list of zones to ignore
	return pLocalPlayer && pLocalPlayer->bBuffTimersOnHold;
}

bool WindowOpen(const char* WindowName)
{
	const auto pWnd = FindMQ2Window(WindowName);
	return  pWnd != nullptr && pWnd->IsVisible();
}

bool IsCasting()
{
	return pLocalPlayer && pLocalPlayer->CastingData.SpellID != -1;
}

bool AbilityInUse()
{
	return pLocalPlayer && pLocalPlayer->CastingData.SpellETA != 0;
}

void PopulateVectorFromINISection(std::vector<std::string>&vVector, const char* section)
{
	vVector.clear(); // clear the vector to ensure we're not duplicating

	char keys[64] = { 0 };
	GetPrivateProfileStringA(
		section,      // section name
		NULL,         // get all keys
		"",           // default
		keys,         // buffer
		sizeof(keys), // buffer size
		INIFileName
	);

	// Walk through null-delimited list of keys
	for (const char* key = keys; *key; key += strlen(key) + 1)
	{
		char value[64];
		GetPrivateProfileStringA(
			section,
			key,
			"",
			value,
			sizeof(value),
			INIFileName
		);
		vVector.push_back(value);
	}
}

bool GoodToConsume()
{
	auto pChar2 = GetPcProfile();

	if (!pLocalPlayer)
	{
		return false;
	}

	int iZoneID = EQWorldData::GetZoneBaseId(pLocalPlayer->GetZoneID());

	if(GetGameState() == GAMESTATE_INGAME                       // currently ingame
		&& pLocalPC                                             // have Charinfo
		&& pLocalPC->pSpawn                                     // have a Spawn
		&& pChar2                                               // have PcProfile
		&& !ItemOnCursor()                                      // nothing on cursor
		&& (!IsCasting() || pChar2->Class == Bard)              // not casting unless bard
		&& (!AbilityInUse() || pChar2->Class == Bard)           // not using abilities unless bard
		&& !WindowOpen("SpellBookWnd")                          // not looking at the book
		&& !WindowOpen("MerchantWnd")                           // not interacting with vendor
		&& !WindowOpen("TradeWnd")                              // not trading with someone
		&& !WindowOpen("BigBankWnd") && !WindowOpen("BankWnd")  // not banking
		&& !WindowOpen("LootWnd")                               // not looting
		&& pLocalPC->pSpawn->StandState != STANDSTATE_FEIGN     // not Feigned
		&& (!bIgnoreSafeZones || !InSafeZone())                 // if we are ignoring safe zones, make sure we're not in one
		&& !bIAmCamping)                                        // not camping
	{
		return true;
	}

	return false;
}

void ListTypes(const std::vector<std::string>& vVector)
{
	for (int i = 0; i < vVector.size(); ++i)
	{
		WriteChatf("\ag - %d. \aw%s", i + 1, vVector[i].c_str());
	}
}

void Consume(uint8_t itemClass, std::vector<std::string> &vVector)
{
	for (const std::string& name : vVector)
	{
		ItemPtr pItem = FindItemByName(name.c_str(), true);
		if (pItem && pItem->GetItemClass() == itemClass)
		{
			if (bAnnounceConsume)
			{
				WriteChatf(PLUGINMSG "\agConsuming \aw-> \ag%s.", pItem->GetName());
			}

			DoCommandf("/useitem \"%s\"", pItem->GetName());
			return;
		}
	}

	if (itemClass == ItemClass_Food)
	{
		if (bFoodWarn)
		{
			WriteChatf(PLUGINMSG "\arNo Food to Consume");
		}
	}
	else
	{
		if (bDrinkWarn)
		{
			WriteChatf(PLUGINMSG "\arNo Drink to Consume");
		}
	}
}

void HandleAddFoodDrinkItem()
{
	if (!ItemOnCursor())
	{
		WriteChatf(PLUGINMSG "\arNeed to have a food/drink item on your cursor to do this.");
		return;
	}

	ItemPtr pItem = GetPcProfile()->GetInventorySlot(InvSlot_Cursor);
	if (pItem)
	{
		if (!pItem->GetItemDefinition()->FoodDuration)
		{
			WriteChatf(PLUGINMSG "\arThat's not food or drink. Don't be rediculous");
			return;
		}

		WriteChatf(PLUGINMSG "\ayFound Item: \ap%s", pItem->GetName());

		auto pItemClass = pItem->GetItemClass();
		if (pItemClass == ItemClass_Food)
		{
			int FoodIndex = static_cast<int>(vFoodList.size()) + 1;

			for (const std::string& itemName : vFoodList)
			{
				if (ci_equals(itemName, pItem->GetName()))
				{
					WriteChatf(PLUGINMSG "\ap%s \aris already on the list", pItem->GetName());
					return;
				}
			}

			WritePrivateProfileString("Food", fmt::format("Food{}", FoodIndex), pItem->GetName(), INIFileName);
			vFoodList.push_back(pItem->GetName());
		}
		else if (pItemClass == ItemClass_Drink)
		{
			int DrinkIndex = static_cast<int>(vDrinkList.size()) + 1;

			for (const std::string& itemName : vDrinkList)
			{
				if (ci_equals(itemName, pItem->GetName()))
				{
					WriteChatf(PLUGINMSG "\ap%s \aris already on the list", pItem->GetName());
					return;
				}
			}

			WritePrivateProfileString("Drink", fmt::format("Drink{}", DrinkIndex), pItem->GetName(), INIFileName);
			vDrinkList.push_back(pItem->GetName());
		}
		else
		{
			WriteChatf(PLUGINMSG "\arWe don't know what to do with %s.", pItem->GetName());
			return;
		}

		DoCommand("/autoinv");
		WriteChatf(PLUGINMSG "\agAdded\aw: \ap%s \ayto your auto%s list", pItem->GetName(), (pItemClass == ItemClass_Food ? "feed" : "drink"));
	}
}

void HandleRemoveFoodDrinkItem(const char* type, const char* item)
{
	std::vector<std::string>* targetVector = nullptr;
	std::string sectionName;

	if (ci_equals(type, "drink"))
	{
		targetVector = &vDrinkList;
		sectionName = "Drink";
	}
	else if (ci_equals(type, "food"))
	{
		targetVector = &vFoodList;
		sectionName = "Food";
	}

	auto it = std::find_if(targetVector->begin(), targetVector->end(),
		[item](const std::string& vectorItem)
		{
			return ci_equals(item, vectorItem);
		});

	if (it != targetVector->end())
	{
		targetVector->erase(it);
	}

	// update ini with our vector of drinks
	if (targetVector)
	{
		for (size_t i = 0; i < (targetVector->size()); ++i)
		{
			WritePrivateProfileString(sectionName.c_str(),
				fmt::format("{}{}",
					sectionName,
					i).c_str(),
				(*targetVector)[i].c_str(),
				INIFileName);
		}

		// Delete any additional keys that exist
		// this does an arbitrary check 10 past the vector size
		// adding a function to count ini section keys seems a bit excessive
		for (size_t i = targetVector->size(); i < targetVector->size() + 10; ++i)
		{
			std::string keyName = fmt::format("{}{}", sectionName, i);
			DeletePrivateProfileKey(sectionName, keyName, INIFileName);
		}
	}
}

void GenericCommand(const char* szLine)
{
	char Arg[MAX_STRING] = { 0 };
	GetArg(Arg, szLine, 1);

	if (ci_equals(Arg, "reload"))
	{
		PopulateVectorFromINISection(vFoodList, "Food");
		PopulateVectorFromINISection(vDrinkList, "Drink");
	}
	else if (ci_equals(Arg, "announceConsume"))
	{
		if (bAnnounceConsume)
		{
			bAnnounceConsume = false;
			WriteChatf(PLUGINMSG "\agConsumption Notification Off");
		}
		else
		{
			bAnnounceConsume = true;
			WriteChatf(PLUGINMSG "\agConsumption Notification On");
		}
	}
	else if (ci_equals(Arg, "IgnoreSafeZones"))
	{

		char NextArg[MAX_STRING] = { 0 };
		strcpy_s(NextArg, GetNextArg(szLine));

		if (NextArg[0] != '\0')
		{
			if (ci_equals(NextArg, "on"))
			{
				bIgnoreSafeZones = true;
			}
			else if (ci_equals(NextArg, "off"))
			{
				bIgnoreSafeZones = false;
			}
			else {
				WriteChatf(PLUGINMSG "\ar%s\ax is an invalid option.", NextArg);
			}
			WritePrivateProfileBool("Settings", "IgnoreSafeZones", bIgnoreSafeZones, INIFileName);
		}
		WriteChatf(PLUGINMSG "\agIgnoreSafeZones (\ag%s\ax).", bIgnoreSafeZones ? "\agOn" : "\arOff");
	}
	else if (ci_equals("ui", Arg) || ci_equals("gui", Arg))
	{
		DoCommand("/mqsettings plugins/feedme");
	}
}

void AutoFeedCmd(PlayerClient* pPlayer, const char* szLine)
{
	if (szLine[0] == '\0')
	{
		if (GoodToConsume())
		{
			Consume(ItemClass_Food, vFoodList);
		}
		return;
	}

	bool bReport = false;
	char Arg[MAX_STRING] = { 0 };
	GetArg(Arg, szLine, 1);

	if (ci_equals(Arg, "list"))
	{
		WriteChatf(PLUGINMSG "\agListing Food:");
		ListTypes(vFoodList);

		bReport = true;
	}
	else if (ci_equals(Arg, "warn"))
	{
		if (bFoodWarn)
		{
			bFoodWarn = false;
			WriteChatf(PLUGINMSG "\agFood Warning \arOff");
		}
		else
		{
			bFoodWarn = true;
			WriteChatf(PLUGINMSG "\agFood Warning On");
		}
	}
	else if (ci_equals(Arg, "add"))
	{
		HandleAddFoodDrinkItem();
	}
	else if (ci_equals(Arg, "remove"))
	{
		const char* item = GetNextArg(szLine);
		if (item[0] == '\0')
		{
			return;
		}

		HandleRemoveFoodDrinkItem("food", item);
	}
	else if (IsNumber(Arg))
	{
		iFeedAt = GetIntFromString(Arg, 5000);
		iFeedAt = std::clamp(iFeedAt, 0, 5000);

		WritePrivateProfileInt(pLocalPC->Name, "AutoFeed", iFeedAt, INIFileName);
		bReport = true;
	}

	if (bReport && bAnnounceLevels)
	{
		if (iFeedAt)
		{
			WriteChatf(PLUGINMSG "\agAutoFeed (\ag%d\ax).", iFeedAt);
		}
		else
		{
			WriteChatf(PLUGINMSG "\agAutoFeed (\aroff\ax).");
		}
		WriteChatf(PLUGINMSG "\agCurrent Thirst\aw: (\ag%d\ag) Hunger\aw: (\ag%d\ax)", GetPcProfile()->thirstlevel, GetPcProfile()->hungerlevel);
	}

	GenericCommand(szLine);
}

void AutoDrinkCmd(PlayerClient* pPlayer, const char* szLine)
{
	if (szLine[0] == '\0')
	{
		if (GoodToConsume())
		{
			Consume(ItemClass_Drink, vDrinkList);
		}
		return;
	}

	bool bReport = false;
	char Arg[MAX_STRING] = { 0 };
	GetArg(Arg, szLine, 1);

	if (ci_equals(Arg, "list"))
	{
		WriteChatf(PLUGINMSG "\agListing Drink:");
		ListTypes(vDrinkList);
		bReport = true;
	}
	else if (ci_equals(Arg, "warn"))
	{
		if (bDrinkWarn)
		{
			bDrinkWarn = false;
			WriteChatf(PLUGINMSG "\agDrink Warning \arOff");
		}
		else {
			bDrinkWarn = true;
			WriteChatf(PLUGINMSG "\agDrink Warning On");
		}
	}
	else if (ci_equals(Arg, "add"))
	{
		HandleAddFoodDrinkItem();
	}
	else if (ci_equals(Arg, "remove"))
	{
		const char* item = GetNextArg(szLine);
		if (item[0] == '\0')
		{
			return;
		}
		// check both vectors for the item
		// and remove
		HandleRemoveFoodDrinkItem("drink", item);
	}
	else if (IsNumber(Arg))
	{
		iDrinkAt = GetIntFromString(Arg, 5000);
		iDrinkAt = std::clamp(iDrinkAt, 0, 5000);

		WritePrivateProfileInt(pLocalPC->Name, "AutoDrink", iDrinkAt, INIFileName);
		bReport = true;
	}

	if (bReport && bAnnounceLevels)
	{
		if (iFeedAt)
		{
			WriteChatf(PLUGINMSG "\agAutoDrink (\ag%d\ax).", iDrinkAt);
		}
		else
		{
			WriteChatf(PLUGINMSG "\agAutoDrink (\aroff\ax).");
		}
		WriteChatf(PLUGINMSG "\agCurrent Thirst\aw: (\ag%d\ag) Hunger\aw: (\ag%d\ax)", GetPcProfile()->thirstlevel, GetPcProfile()->hungerlevel);
		return;
	}

	GenericCommand(szLine);
}

PLUGIN_API void OnPulse()
{
	static std::chrono::steady_clock::time_point PulseTimer = std::chrono::steady_clock::now();
	// let's slow it down and only check every second
	if (std::chrono::steady_clock::now() > PulseTimer)
	{

		if (!GoodToConsume())
		{
			return;
		}

		if (iDrinkAt && GetPcProfile()->thirstlevel < iDrinkAt)
		{
			Consume(ItemClass_Drink, vDrinkList);
		}

		if (iFeedAt && GetPcProfile()->hungerlevel < iFeedAt)
		{
			Consume(ItemClass_Food, vFoodList);
		}

		// Wait 1 second before running again
		PulseTimer = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	}
}

PLUGIN_API DWORD OnIncomingChat(const char* Line, const unsigned int Color)
{
	if (Color == USERCOLOR_DEFAULT)
	{
		if (!bIAmCamping)
		{
			if (find_substr(Line, "It will take you about 30 seconds to prepare your camp.") != -1)
			{
				bIAmCamping = true;
			}
		}
		else
		{
			if (find_substr(Line, "You abandon your preparations to camp.") != -1)
			{
				bIAmCamping = false;
			}
		}
	}
	return 0;
}

PLUGIN_API VOID OnZoned()
{
	if (gGameState == GAMESTATE_CHARSELECT)
	{
		bIAmCamping = false;
	}
}

PLUGIN_API void SetGameState(const int GameState)
{
	if (GameState == GAMESTATE_INGAME)
	{
		if (pLocalPC)
		{
			iDrinkAt = GetPrivateProfileInt(pLocalPC->Name, "AutoDrink", 0, INIFileName);
			iFeedAt = GetPrivateProfileInt(pLocalPC->Name, "AutoFeed",  0, INIFileName);
			bAnnounceLevels = GetPrivateProfileBool("Settings", "Announce",  true, INIFileName);
			bAnnounceConsume = GetPrivateProfileBool("Settings", "Announce", true, INIFileName);
			bFoodWarn = GetPrivateProfileBool("Settings", "FoodWarn", false, INIFileName);
			bDrinkWarn = GetPrivateProfileBool("Settings", "DrinkWarn", false, INIFileName);

			WritePrivateProfileBool("Settings", "Announce", bAnnounceLevels, INIFileName);
			WritePrivateProfileBool("Settings", "FoodWarn", bFoodWarn, INIFileName);
			WritePrivateProfileBool("Settings", "DrinkWarn", bDrinkWarn, INIFileName);
			WritePrivateProfileBool("Settings", "IgnoreSafeZones", bIgnoreSafeZones, INIFileName);

			if (!Loaded)
			{
				PopulateVectorFromINISection(vFoodList, "FOOD");
				PopulateVectorFromINISection(vDrinkList, "DRINK");
				Loaded = true;
			}
		}
	}
}

void FeedMeImGuiSettingsPanel()
{
	if (ImGui::Checkbox("Announce", &bAnnConsume))
	{
		WritePrivateProfileBool("Settings", "Announce", bAnnConsume, INIFileName);
	}
	ImGui::SameLine();
	mq::imgui::HelpMarker("Announce Levels and Consumption.\n\nINI Setting: Announce");

	if (ImGui::Checkbox("Don't Consume In Safe Zones", &bIgnoreSafeZones))
	{
		WritePrivateProfileBool("Settings", "IgnoreSafeZones", bIgnoreSafeZones, INIFileName);
	}
	ImGui::SameLine();
	mq::imgui::HelpMarker("Ignore Safe Zones like \"poknowledge\" or \"guildlobby\" for auto consumption.\nThis does *NOT* move food around in your bags.\n\nINI Setting: IgnoreSafeZones");

	if (ImGui::Checkbox("Warning: No Food", &bFoodWarn))
	{
		WritePrivateProfileBool("Settings", "FoodWarn", bFoodWarn, INIFileName);
	}
	ImGui::SameLine();
	mq::imgui::HelpMarker("Warn when no food.\n\nINI Setting: FoodWarn");

	if (ImGui::Checkbox("Warning: No Drink", &bFoodWarn))
	{
		WritePrivateProfileBool("Settings", "DrinkWarn", bFoodWarn, INIFileName);
	}
	ImGui::SameLine();
	mq::imgui::HelpMarker("Warn when no drink.\n\nINI Setting: DrinkWarn");

	constexpr int iInputWidth = 150;
	// so we get consistent alignment we should put these two in a table
	if (ImGui::BeginTable("AutoConsumeTable", 2, ImGuiTableFlags_SizingFixedFit))
	{
		// Drink
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted("Consume Drink at thirst level:");
		ImGui::TableNextColumn();
		ImGui::SetNextItemWidth(iInputWidth);
		if (ImGui::InputInt("##iDrinkAt", &iDrinkAt)) {
			iDrinkAt = std::clamp(iDrinkAt, 0, 5000);

			WritePrivateProfileInt("Settings", "AutoDrink", iDrinkAt, INIFileName);
		}
		ImGui::SameLine();
		mq::imgui::HelpMarker("When we reach this thirst value we will consume a drink on your drink list until we are at/above the AutoDrink value.\n\nINI Setting: DrinkAt");

		// Food
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted("Consume Food at hunter level:");
		ImGui::TableNextColumn();
		ImGui::SetNextItemWidth(iInputWidth);
		if (ImGui::InputInt("##iFeedAt", &iFeedAt)) {
			iFeedAt = std::clamp(iFeedAt, 0, 5000);

			WritePrivateProfileInt("Settings", "AutoFeed", iFeedAt, INIFileName);
		}
		ImGui::SameLine();
		mq::imgui::HelpMarker("When we reach this hunger value we will consume food on your drink list until we are at/above the AutoFeed value.\n\nINI Setting: FeedAt");

		ImGui::EndTable();
	}

	const float buttonsize = ImGui::GetWindowSize().x * 0.45f;
	if (ImGui::Button("Add Food Currently on Cursor", ImVec2(buttonsize, 0)))
	{
		HandleAddFoodDrinkItem();
	}
	ImGui::SameLine();

	if (ImGui::Button("Add Drink Currently on Cursor", ImVec2(buttonsize, 0)))
	{
		HandleAddFoodDrinkItem();
	}

	ImGui::SetWindowFontScale(0.80f); // make it small
	float columnWidth = ImGui::GetColumnWidth();
	const char* text = "Double click to delete an item from the list.";
	ImVec2 textSize = ImGui::CalcTextSize(text);

	float offset = (columnWidth - textSize.x) * 0.5f;
	if (offset > 0.0f)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
	}
	ImGui::TextUnformatted(text);
	ImGui::SetWindowFontScale(1.0f); // set it back

	ImGui::BeginChild("FoodChild", ImVec2(0, 150), true);

	if (ImGui::BeginTable("##FeedMeFoodList", 1, ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableSetupColumn("Food");
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();
		for (int i = 0; i < static_cast<int>(vFoodList.size()); i++)
		{
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(vFoodList[i].c_str());

			// double click to remove
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
			{
				const std::string& chosenFood = vFoodList[i];
				HandleRemoveFoodDrinkItem("food", chosenFood.c_str());
			}

			ImGui::PopID();
		}

		ImGui::EndTable();
	}
	ImGui::EndChild();

	ImGui::BeginChild("DrinkChild", ImVec2(0, 150), true);
	if (ImGui::BeginTable("##FeedMeDrinkList", 1, ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableSetupColumn("Drink");
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();
		for (int i = 0; i < static_cast<int>(vDrinkList.size()); i++)
		{
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(vDrinkList[i].c_str());

			// double click to remove
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
			{
				const std::string& chosenFood = vDrinkList[i];
				HandleRemoveFoodDrinkItem("drink", chosenFood.c_str());
			}

			ImGui::PopID();
		}

		ImGui::EndTable();
	}
	ImGui::EndChild();
}

PLUGIN_API void InitializePlugin()
{
	AddCommand("/autofeed",  AutoFeedCmd);
	AddCommand("/autodrink", AutoDrinkCmd);

	AddSettingsPanel("plugins/FeedMe", FeedMeImGuiSettingsPanel);
	pFeedMeType = new MQ2FeedMeType;
	AddMQ2Data("FeedMe", dataFeedMe);
}

PLUGIN_API void ShutdownPlugin()
{
	RemoveCommand("/autofeed");
	RemoveCommand("/autodrink");

	RemoveSettingsPanel("plugins/FeedMe");
	delete pFeedMeType;
	RemoveMQ2Data("FeedMe");
}
