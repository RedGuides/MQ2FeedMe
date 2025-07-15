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

#include <mq/Plugin.h>
#include <mq/imgui/ImGuiUtils.h>

PreSetup("MQ2FeedMe");
PLUGIN_VERSION(4.2);

bool         Loaded = false;				// List Loaded?

int			 iFeedAt = 0;					// Feed Level
int			 iDrinkAt = 0;					// Drink Level
char         Buffer[16] = {0};
std::list<std::string> Hunger;				// Hunger Fix List
std::list<std::string> Thirst;				// Thirst Fix List

bool          bAnnLevels = true;			// Announce Levels
bool          bAnnConsume = true;			// Announce Consumption
bool          bFoodWarn = false;			// Announce No Food
bool          bDrinkWarn = false;			// Announce No Drink
bool		  bIAmCamping = false;			// Defining if we are camping out or not

const char* PLUGIN_NAME = "MQ2FeedMe";

using namespace mq::datatypes;

class MQ2FeedMeType : public MQ2Type {
public:
	enum FeedMeMembers {
		FeedAt,
		DrinkAt,
		Announce,
		FoodWarn,
		DrinkWarn,
	};

	MQ2FeedMeType() : MQ2Type("FeedMe") {
		TypeMember(FeedAt);
		TypeMember(DrinkAt);
		TypeMember(Announce);
		TypeMember(FoodWarn);
		TypeMember(DrinkWarn);
	};

	//~MQ2FeedMeType()
	//{
	//}

	//virtual bool GetMember(MQVarPtr VarPtr, const char* Member, char* Index, MQTypeVar& Dest) override
	bool MQ2FeedMeType::GetMember(MQVarPtr VarPtr, const char* Member, char* Index, MQTypeVar& Dest) {
		MQTypeMember* pMember = MQ2FeedMeType::FindMember(Member);
		if (!pMember) {
			return false;
		}

		if (!pLocalPlayer) {
			return false;
		}

		switch ((FeedMeMembers)pMember->ID) {
			case FeedAt:
				Dest.Int = iFeedAt;
				Dest.Type = pIntType;
				return true;
			case DrinkAt:
				Dest.Int = iDrinkAt;
				Dest.Type = pIntType;
				return true;
			case Announce:
				Dest.DWord = bAnnConsume;
				Dest.Type = pBoolType;
				return true;
			case FoodWarn:
				Dest.DWord = bFoodWarn;
				Dest.Type = pBoolType;
				return true;
			case DrinkWarn:
				Dest.DWord = bDrinkWarn;
				Dest.Type = pBoolType;
				return true;


			default:
				break;
			}
		return false;
	}

	bool MQ2FeedMeType::ToString(MQVarPtr VarPtr, char* Destination) {
		return true;
	}

	bool MQ2FeedMeType::FromData(const MQVarPtr& VarPtr, MQTypeVar& Source) {
		return false;
	}

	bool MQ2FeedMeType::FromString(MQVarPtr& VarPtr, const char* Source) {
		return false;
	}

};

MQ2FeedMeType* pFeedMeType = nullptr;

//bool TrophyData(const char* szIndex, MQTypeVar& Dest)
bool dataFeedMe(const char* szIndex, MQTypeVar& Dest) {
	Dest.DWord = 1;
	Dest.Type = pFeedMeType;
	return true;
}

bool WindowOpen(PCHAR WindowName)
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

void ReadList(std::list<std::string>* MyList, PCHAR fSec)
{
	char Buffer[MAX_STRING*10];
	MyList->clear();

	if(GetPrivateProfileString(fSec, NULL, "", Buffer, MAX_STRING*10 , INIFileName))
	{
		char  szTemp[MAX_STRING];
		char* pBuffer = Buffer;

		while (pBuffer[0] !=0 )

		{
			GetPrivateProfileString(fSec, pBuffer, "", szTemp, MAX_STRING, INIFileName);
			if (szTemp[0] != 0)
			{
				MyList->push_back(std::string(szTemp));
			}

			pBuffer += strlen(pBuffer) + 1;
		}
	}
}

bool GoodToConsume()
{
	auto pChar2 = GetPcProfile();

	if(GetGameState() == GAMESTATE_INGAME						// currently ingame
		&& pLocalPC												// have Charinfo
		&& pLocalPC->pSpawn										// have a Spawn
		&& pChar2												// have PcProfile
		&& !ItemOnCursor()										// nothing on cursor
		&& (!IsCasting() || pChar2->Class == Bard)				// not casting unless bard
		&& (!AbilityInUse() || pChar2->Class == Bard)			// not using abilities unless bard
		&& !WindowOpen("SpellBookWnd")							// not looking at the book
		&& !WindowOpen("MerchantWnd")							// not interacting with vendor
		&& !WindowOpen("TradeWnd")								// not trading with someone
		&& !WindowOpen("BigBankWnd") && !WindowOpen("BankWnd")	// not banking
		&& !WindowOpen("LootWnd")								// not looting
		&& pLocalPC->pSpawn->StandState != STANDSTATE_FEIGN		// not Feigned
		&& !bIAmCamping)										// not camping
	{                                         
		return true;
	}

	return false;
}

void ListTypes(const std::list<std::string>& list)
{
	int i = 1;
	for (const std::string& value : list)
	{
		WriteChatf("\ag - %d. \aw%s", i++, value.c_str());
	}
}

void Execute(PCHAR zFormat, ...)
{
	char zOutput[MAX_STRING]={0}; va_list vaList; va_start(vaList,zFormat);
	vsprintf_s(zOutput, zFormat, vaList); 

	if (!zOutput[0])
	{
		return;
	}

	DoCommand(zOutput);
}

void Consume(uint8_t itemClass, const std::list<std::string>& fLIST)
{
	for (const std::string& name : fLIST) {
		ItemPtr pItem = FindItemByName(name.c_str(), true);
		if (pItem && pItem->GetItemClass() == itemClass)
		{
			if (bAnnConsume)
			{
				WriteChatf("\ay%s\aw:: Consuming -> \ag%s.", PLUGIN_NAME, name.c_str());
			}

			DoCommandf("/useitem %d %d", pItem->GetItemLocation().GetSlot(0), pItem->GetItemLocation().GetSlot(1));
			return;
		}
	}

	if (itemClass == ItemClass_Food) {
		if (bFoodWarn)
		{
			WriteChatf("\ay%s\aw:: No Food to Consume", PLUGIN_NAME);
		}
	} 
	else 
	{
		if (bDrinkWarn)
		{
			WriteChatf("\ay%s\aw:: No Drink to Consume", PLUGIN_NAME);
		}
	}
}

void HandleFoodDrinkItem()
{
	if (!ItemOnCursor())
	{
		WriteChatf("%s:: \arNeed to have a food/drink item on your cursor to do this.", PLUGIN_NAME);
		return;
	}

	ItemPtr pItem = GetPcProfile()->GetInventorySlot(InvSlot_Cursor);
	if (pItem)
	{
		auto pItemClass = pItem->GetItemClass();
		if (!pItem->GetItemDefinition()->FoodDuration)
		{
			WriteChatf("%s:: \arThat's not food or drink. Don't be rediculous", PLUGIN_NAME);
			return;
		}

		WriteChatf("%s:: \ayFound Item: \ap%s", PLUGIN_NAME, pItem->GetName());

		if (pItemClass == ItemClass_Food)
		{
			int FoodIndex = static_cast<int>(Hunger.size()) + 1;

			for (const std::string& itemName : Hunger)
			{
				if (ci_equals(itemName, pItem->GetName()))
				{
					WriteChatf("%s:: \ap%s \aris already on the list", PLUGIN_NAME, pItem->GetName());
					return;
				}
			}

			WritePrivateProfileString("Food", fmt::format("Food{}", FoodIndex), pItem->GetName(), INIFileName);
			Hunger.push_back(pItem->GetName());
		}
		else if (pItemClass == ItemClass_Drink)
		{
			int DrinkIndex = static_cast<int>(Thirst.size()) + 1;

			for (const std::string& itemName : Thirst)
			{
				if (ci_equals(itemName, pItem->GetName()))
				{
					WriteChatf("%s:: \ap%s \aris already on the list", PLUGIN_NAME, pItem->GetName());
					return;
				}
			}

			WritePrivateProfileString("Drink", fmt::format("Drink{}", DrinkIndex), pItem->GetName(), INIFileName);
			Thirst.push_back(pItem->GetName());
		}
		else if (!pItem->GetItemDefinition()->FoodDuration)
		{
			WriteChatf("%s:: \ar%s does not have a food duration.", PLUGIN_NAME, pItem->GetName());
			return;
		}
		else
		{
			WriteChatf("%s:: \arWe don't know what to do with %s.", PLUGIN_NAME, pItem->GetName());
			return;
		}

		DoCommand("/autoinv");
		WriteChatf("%s \agAdded\aw: \ap%s \ayto your auto%s list", PLUGIN_NAME, pItem->GetName(), (pItemClass == ItemClass_Food ? "feed" : "drink"));
	}
}

void GenericCommand(const char* szLine)
{
	char Arg[MAX_STRING] = { 0 };
	GetArg(Arg, szLine, 1);

	if (ci_equals(Arg, "reload"))
	{
		ReadList(&Hunger, "FOOD");
		ReadList(&Thirst, "DRINK");
	}
	else if (ci_equals(Arg, "announceConsume"))
	{
		if (bAnnConsume)
		{
			bAnnConsume = false;
			WriteChatf("\ay%s\aw::Consumption Notification Off", PLUGIN_NAME);
		}
		else
		{
			bAnnConsume = true;
			WriteChatf("\ay%s\aw::Consumption Notification On", PLUGIN_NAME);
		}
	}
	else if (!_stricmp("ui", Arg) || !_stricmp("gui", Arg))
	{
		DoCommand("/mqsettings plugins/feedme");
	}
}

void AutoFeedCmd(PlayerClient* pLPlayer, char* szLine)
{
	if (!strlen(szLine))
	{
		if (GoodToConsume())
		{
			Consume(ItemClass_Food, Hunger);
		}
		return;
	}

	char Arg[MAX_STRING] = { 0 };
	GetArg(Arg, szLine, 1);

	if (ci_equals(Arg, "list")) {
		WriteChatf("\ay%s\aw:: Listing Food:", PLUGIN_NAME);
		ListTypes(Hunger);

		if (bAnnLevels)
		{
			sprintf_s(Buffer, "%d", iFeedAt);
			WriteChatf("\ay%s\aw:: AutoFeed (\ag%s\ax).", PLUGIN_NAME, (iFeedAt) ? Buffer : "\aroff");
			WriteChatf("\ay%s\aw:: Current Hunger (\ag%d\ax)", PLUGIN_NAME, GetPcProfile()->hungerlevel);
		}
	} 
	else if (ci_equals(Arg, "warn"))
	{
		if (bFoodWarn)
		{
			bFoodWarn = 0;
			WriteChatf("\ay%s\aw:: Food Warning Off", PLUGIN_NAME);
		} else
		{
			bFoodWarn = 1;
			WriteChatf("\ay%s\aw:: Food Warning On", PLUGIN_NAME);
		}
	}
	else if (ci_equals(Arg, "add"))
	{
		HandleFoodDrinkItem();
	}
	else if (IsNumber(Arg))
	{
		iFeedAt = GetIntFromString(Arg, 5000);
		if (iFeedAt < 0)
		{
			iFeedAt = 0;
		}
		else if (iFeedAt > 5000)
		{
			iFeedAt = 5000;
		}

		sprintf_s(Buffer, "%d", iFeedAt);
		WritePrivateProfileString(GetCharInfo()->Name, "AutoFeed", Buffer, INIFileName);
		WriteChatf("\ay%s\aw:: AutoFeed (\ag%s\ax).", PLUGIN_NAME, (iFeedAt) ? Buffer : "\aroff");

		if (bAnnLevels)
		{
			WriteChatf("\ay%s\aw:: Current Thirst (\ag%d\ax) Hunger (\ag%d\ax)", PLUGIN_NAME, GetPcProfile()->thirstlevel, GetPcProfile()->hungerlevel);
		}
	}
	
	GenericCommand(szLine);
}

void AddDrinkItem()
{
	if (!ItemOnCursor())
	{
		WriteChatf("%s:: \arNeed to have a food item on your cursor to do this.", PLUGIN_NAME);
		return;
	}

	ItemPtr pItem = GetPcProfile()->GetInventorySlot(InvSlot_Cursor);
	if (pItem)
	{
		if (!pItem->GetItemDefinition()->FoodDuration || pItem->GetItemClass() != ItemClass_Drink)
		{
			WriteChatf("%s:: \arThat's not a drink. Don't be ridiculous", PLUGIN_NAME);
			return;
		}

		WriteChatf("%s:: \ayFound Item: \ap%s", PLUGIN_NAME, pItem->GetName());
		int FoodIndex = static_cast<int>(Thirst.size()) + 1;

		for (const std::string& itemName : Thirst)
		{
			if (ci_equals(itemName, pItem->GetName()))
			{
				WriteChatf("%s:: \ap%s \aris already on the list", PLUGIN_NAME, pItem->GetName());
				return;
			}
		}
		WritePrivateProfileString("Drink", fmt::format("Drink{}", FoodIndex), pItem->GetName(), INIFileName);
		Thirst.push_back(pItem->GetName());
		DoCommand("/autoinv");
		WriteChatf("%s \agAdded\aw: \ap%s \ayto your autodrink list", PLUGIN_NAME, pItem->GetName());
	}
}

void AutoDrinkCmd(PlayerClient* pLPlayer, const char* szLine)
{
	if (!strlen(szLine))
	{
		if (GoodToConsume())
		{
			Consume(ItemClass_Drink, Thirst);
		}
		return;
	}

	char Arg[MAX_STRING] = { 0 };
	GetArg(Arg, szLine, 1);

	if (ci_equals(Arg, "list"))
	{
		WriteChatf("\ay%s\aw:: Listing Drink:", PLUGIN_NAME);
		ListTypes(Thirst);
		if (bAnnLevels)
		{
			sprintf_s(Buffer, "%d", iDrinkAt);
			WriteChatf("\ay%s\aw:: AutoDrink (\ag%s\ax).", PLUGIN_NAME, (iDrinkAt) ? Buffer : "\aroff");
			WriteChatf("\ay%s\aw:: Current Thirst (\ag%d\ax)", PLUGIN_NAME, GetPcProfile()->thirstlevel);
		}
	} 
	else if (ci_equals(Arg, "warn"))
	{
		if (bDrinkWarn)
		{
			bDrinkWarn = false;
			WriteChatf("\ay%s\aw:: Drink Warning Off", PLUGIN_NAME);
		} 
		else {
			bDrinkWarn = true;
			WriteChatf("\ay%s\aw:: Drink Warning On", PLUGIN_NAME);
		}
	}
	else if (ci_equals(Arg, "add"))
	{
		AddDrinkItem();
	}
	else if (IsNumber(Arg))
	{
		iDrinkAt = GetIntFromString(Arg, 5000);
		if (iDrinkAt < 0)
		{
			iDrinkAt = 0;
		}
		else if (iDrinkAt > 5000)
		{
			iDrinkAt = 5000;
		}
		sprintf_s(Buffer, "%d", iDrinkAt);
		WritePrivateProfileString(GetCharInfo()->Name, "AutoDrink", Buffer, INIFileName);
		WriteChatf("\ay%s\aw:: AutoDrink (\ag%s\ax).", PLUGIN_NAME, iDrinkAt ? Buffer :"\aroff");

		if (bAnnLevels)
		{
			WriteChatf("\ay%s\aw:: Current Thirst (\ag%d\ax) Hunger (\ag%d\ax)", PLUGIN_NAME, GetPcProfile()->thirstlevel, GetPcProfile()->hungerlevel);
		}
	}
	else
	{
		GenericCommand(szLine);
	}
}

PLUGIN_API void OnPulse()
{
	static int Pulses = 0;
	if (++Pulses < 50)
	{
		return;
	}

	Pulses = 0;

	if (!GoodToConsume())
	{
		return;
	}

	if (iDrinkAt && GetPcProfile()->thirstlevel < iDrinkAt)
	{
		Consume(ItemClass_Drink, Thirst);
	}

	if (iFeedAt && GetPcProfile()->hungerlevel < iFeedAt)
	{
		Consume(ItemClass_Food, Hunger);
	}
}

PLUGIN_API DWORD OnIncomingChat(const char* Line, const unsigned int Color)
{
	if (!bIAmCamping && ci_find_substr(Line, "It will take you about 30 seconds to prepare your camp.") != -1)
	{
		bIAmCamping = true;
	}
	else if (bIAmCamping && ci_find_substr(Line, "You abandon your preparations to camp.") != -1)
	{
		bIAmCamping = false;
	}
	return 0;
}

PLUGIN_API VOID OnZoned()
{
	//If I switch characters and IAmCamping is still true and I finish zoning, and the gamestate is ingame...
	if (bIAmCamping && GetGameState() == GAMESTATE_INGAME)
	{
		bIAmCamping = false;
	}
}

PLUGIN_API void SetGameState(const int GameState)
{
	if (GameState == GAMESTATE_INGAME) {
		if (GetPcProfile())
		{
			iDrinkAt     = GetPrivateProfileInt(GetCharInfo()->Name, "AutoDrink", 0, INIFileName);
			iFeedAt     = GetPrivateProfileInt(GetCharInfo()->Name, "AutoFeed",  0, INIFileName);
			bAnnLevels = GetPrivateProfileInt("Settings",          "Announce",  1, INIFileName) != 0;
			bAnnConsume = GetPrivateProfileInt("Settings",         "Announce", 1, INIFileName) != 0;
			bFoodWarn  = GetPrivateProfileInt("Settings",          "FoodWarn",  0, INIFileName) != 0;
			bDrinkWarn = GetPrivateProfileInt("Settings",          "DrinkWarn",  0, INIFileName) != 0;

			WritePrivateProfileString("Settings", "Announce", bAnnLevels ? "1" : "0", INIFileName);
			WritePrivateProfileString("Settings", "FoodWarn", bFoodWarn ? "1" : "0", INIFileName);
			WritePrivateProfileString("Settings", "DrinkWarn", bDrinkWarn ? "1" : "0", INIFileName);

			if (!Loaded)
			{
				ReadList(&Hunger, "FOOD");
				ReadList(&Thirst, "DRINK");
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
	mq::imgui::HelpMarker("Announce Levels and Consumption.");
	
	if (ImGui::Checkbox("Food Warn", &bFoodWarn))
	{
		WritePrivateProfileBool("Settings", "FoodWarn", bFoodWarn, INIFileName);
	}
	ImGui::SameLine();
	mq::imgui::HelpMarker("Warn when no food.");
	
	if (ImGui::Checkbox("Drink Warn", &bFoodWarn))
	{
		WritePrivateProfileBool("Settings", "DrinkWarn", bFoodWarn, INIFileName);
	}
	ImGui::SameLine();
	mq::imgui::HelpMarker("Warn when no drink.");

	if (ImGui::InputInt("DrinkAt", &iDrinkAt)) {
		iDrinkAt = std::clamp(iDrinkAt, 0, 5000);

		WritePrivateProfileInt("Settings", "AutoDrink", iDrinkAt, INIFileName);
	}
	ImGui::SameLine();
	mq::imgui::HelpMarker("Thirst level to drink.");

	if (ImGui::InputInt("FeedAt", &iFeedAt)) {
		iFeedAt = std::clamp(iFeedAt, 0, 5000);

		WritePrivateProfileInt("Settings", "AutoFeed", iFeedAt, INIFileName);
	}
	ImGui::SameLine();
	mq::imgui::HelpMarker("Hunger level to eat.");

	const float buttonsize = ImGui::GetWindowSize().x * 0.45f;
	if (ImGui::Button("Add Food From Cursor", ImVec2(buttonsize, 0)))
	{
		HandleFoodDrinkItem();
	}
	ImGui::SameLine();

	if (ImGui::Button("Add Drink On Cursor", ImVec2(buttonsize, 0)))
	{
		HandleFoodDrinkItem();
	}

	ImGui::BeginChild("FoodChild", ImVec2(0, 150), true);
	if (ImGui::BeginTable("##FeedMeFoodList", 1, ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableSetupColumn("Food");
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();
		size_t index = 0;
		for (const auto& food : Hunger)
		{
			ImGui::PushID(static_cast<int>(index));
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text(food.c_str());
			ImGui::PopID();
			index++;
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
		size_t index = 0;
		for (const auto& drink : Thirst)
		{
			ImGui::PushID(static_cast<int>(index));
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text(drink.c_str());
			ImGui::PopID();
			index++;
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
}

PLUGIN_API void ShutdownPlugin()
{
	RemoveCommand("/autofeed");
	RemoveCommand("/autodrink");

	RemoveSettingsPanel("plugins/FeedMe");
}
