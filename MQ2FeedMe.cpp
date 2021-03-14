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

PreSetup("MQ2FeedMe");
PLUGIN_VERSION(4.2);

bool         Loaded=false;             // List Loaded?

long         FeedAt=0;                 // Feed Level
long         DrnkAt=0;                 // Drink Level

char         FindName[ITEM_NAME_LEN];  // Find Food/Drink Name
char         Buffer[16]={0};
std::list<std::string> Hunger;         // Hunger Fix List
std::list<std::string> Thirst;         // Thirst Fix List

bool          bAnnLevels = true;       // Announce Levels
bool          bAnnConsume = true;      // Announce Consumption
bool          bFoodWarn = false;       // Announce No Food
bool          bDrinkWarn = false;      // Announce No Drink

bool IAmCamping = false;               // Defining if we are camping out or not

const char* PLUGIN_NAME = "MQ2FeedMe";

bool WindowOpen(PCHAR WindowName)
{
	const auto pWnd = FindMQ2Window(WindowName);
	return  pWnd != nullptr && pWnd->IsVisible();
}

bool IsCasting()
{
	return (pCharSpawn && ((PSPAWNINFO)pCharSpawn)->CastingData.SpellID != -1);
}

bool AbilityInUse()
{
	if (pCharSpawn && ((PSPAWNINFO)pCharSpawn)->CastingData.SpellETA == 0) return false;
	return true;
}

bool CursorHasItem()
{
	return GetPcProfile() && GetPcProfile()->GetInventorySlot(InvSlot_Cursor) != nullptr;
}

void ReadList(std::list<std::string> *MyList, PCHAR fSec)
{
	char Buffer[MAX_STRING*10];
	MyList->clear();
	if(GetPrivateProfileString(fSec,NULL,"",Buffer,MAX_STRING*10,INIFileName)) {
		char  szTemp[MAX_STRING];
		PCHAR pBuffer=Buffer;
		while (pBuffer[0]!=0) {
			GetPrivateProfileString(fSec,pBuffer,"",szTemp,MAX_STRING,INIFileName);
			if(szTemp[0]!=0) MyList->push_back(std::string(szTemp));
			pBuffer+=strlen(pBuffer)+1;
		}
	}
}

bool GoodToFeed()
{
	auto pChar = GetCharInfo();
	auto pChar2 = GetPcProfile();
	
	if(GetGameState() == GAMESTATE_INGAME &&                  // currently ingame
	   pChar &&                                               // have Charinfo
	   pChar->pSpawn &&                                       // have a Spawn
	   pChar2 &&                                              // have PcProfile
	   !CursorHasItem() &&                                    // nothing on cursor
	   (!IsCasting() || pChar2->Class == Bard) &&             // not casting unless bard
	   (!AbilityInUse() || pChar2->Class == Bard) &&          // not using abilities unless bard
	   !WindowOpen("SpellBookWnd") &&                         // not looking at the book
	   !WindowOpen("MerchantWnd") &&                          // not interacting with vendor
	   !WindowOpen("TradeWnd") &&                             // not trading with someone
	   !WindowOpen("BigBankWnd") && !WindowOpen("BankWnd") && // not banking
	   !WindowOpen("LootWnd") &&                              // not looting
	   pChar->pSpawn->StandState != STANDSTATE_FEIGN &&       // not Feigned
	   !IAmCamping) {                                         // not camping
		return true;
	}
	return false;
}

void ListTypes(std::list<std::string> fTempList)
{
	std::list<std::string>::iterator pTempList;
	int i = 1;
	pTempList = fTempList.begin();
	while (pTempList != fTempList.end()) {
		WriteChatf("\ag - %d. \aw%s", i, pTempList->c_str());
		i++;
		pTempList++;
	}
}

void Execute(PCHAR zFormat, ...)
{
	char zOutput[MAX_STRING]={0}; va_list vaList; va_start(vaList,zFormat);
	vsprintf_s(zOutput,zFormat,vaList); if(!zOutput[0]) return;
	DoCommand(GetCharInfo()->pSpawn,zOutput);
}

void Consume(BYTE fTYPE, std::list<std::string> fLIST)
{
	std::list<std::string>::iterator pTempList;
	pTempList = fLIST.begin();
	while (pTempList != fLIST.end()) {
		strcpy_s(FindName, pTempList->c_str());
		if (PCONTENTS pItem = FindItemByName(FindName,true)) {
			if (GetItemFromContents(pItem)->ItemType == fTYPE) {
				if (bAnnConsume) WriteChatf("\ay%s\aw:: Consuming -> \ag%s.", PLUGIN_NAME, FindName);
				Execute("/useitem %d %d", pItem->Contents.ItemSlot, pItem->Contents.ItemSlot2);
				return;
			}
		}
		pTempList++;
	}
	if (fTYPE == ItemClass_Food) {
		if (bFoodWarn) WriteChatf("\ay%s\aw:: No Food to Consume", PLUGIN_NAME);
	} else {
		if (bDrinkWarn) WriteChatf("\ay%s\aw:: No Drink to Consume", PLUGIN_NAME);
	}
}

void AutoFeedCmd(PSPAWNINFO pLPlayer, char* szLine)
{
	if (!strlen(szLine)) {
		if (GoodToFeed()) Consume(ItemClass_Food, Hunger);
		return;
	}
	CHAR Arg[MAX_STRING] = { 0 };
	GetArg(Arg, szLine, 1);
	if (!_stricmp(Arg, "list")) {
		WriteChatf("\ay%s\aw:: Listing Food:", PLUGIN_NAME);
		ListTypes(Hunger);
		if (bAnnLevels) {
			sprintf_s(Buffer, "%d", FeedAt);
			WriteChatf("\ay%s\aw:: AutoFeed(\ag%s\ax).", PLUGIN_NAME, (FeedAt) ? Buffer : "\aroff");
			WriteChatf("\ay%s\aw:: Current Hunger(\ag%d\ax)", PLUGIN_NAME, GetPcProfile()->hungerlevel);
		}
	} else if (!_stricmp(Arg, "warn")) {
		if (bFoodWarn) {
			bFoodWarn = 0;
			WriteChatf("\ay%s\aw:: Food Warning Off", PLUGIN_NAME);
		} else {
			bFoodWarn = 1;
			WriteChatf("\ay%s\aw:: Food Warning On", PLUGIN_NAME);
		}
	}
	else if (!_stricmp(Arg, "reload")) {
		ReadList(&Hunger, "FOOD");
		ReadList(&Thirst, "DRINK");
	}
	else if (!_stricmp(Arg, "add")) {
		if (!ItemOnCursor()) {
			WriteChatf("%s:: \arNeed to have a food item on your cursor to do this.", PLUGIN_NAME);
			return;
		}
		if (PCONTENTS item = GetPcProfile()->GetInventorySlot(InvSlot_Cursor)) {
			if (PITEMINFO pIteminf = GetItemFromContents(item)) {
				if (pIteminf->FoodDuration) {
					WriteChatf("%s:: \ayFound Item: \ap%s", PLUGIN_NAME, pIteminf->Name);
					char temp[MAX_STRING] = "";
					char ItemtoAdd[MAX_STRING] = "";
					int FoodIndex = Hunger.size() + 1;
					sprintf_s(temp, "Food%i", FoodIndex);
					sprintf_s(ItemtoAdd, "%s", pIteminf->Name);
					std::list<std::string>::iterator pTempList;
					pTempList = Hunger.begin();
					while (pTempList != Hunger.end()) {
						strcpy_s(FindName, pTempList->c_str());
						if (FindItemByName(FindName, true)) {
							if (!_stricmp(FindName, ItemtoAdd)) {
								WriteChatf("%s:: \ap%s \aris already on the list", PLUGIN_NAME, ItemtoAdd);
								return;
							}
						}
						pTempList++;
					}
					WritePrivateProfileString("Food", temp, ItemtoAdd, INIFileName);
					EzCommand("/autoinv");
					WriteChatf("%s \agAdded\aw: \ap%s \ayto your autofeed list", PLUGIN_NAME, ItemtoAdd);
					ReadList(&Hunger, "FOOD");
				}
				else {
					WriteChatf("%s:: \arThat's not Food. Don't be rediculous", PLUGIN_NAME);
				}
			}
		}
	}
	else if (IsNumber(Arg)) {
		FeedAt = atoi(Arg);
		if (FeedAt < 0) FeedAt = 0;
		else if (FeedAt > 5000) FeedAt = 5000;
		sprintf_s(Buffer, "%d", FeedAt);
		WritePrivateProfileString(GetCharInfo()->Name, "AutoFeed", Buffer, INIFileName);
		WriteChatf("\ay%s\aw:: AutoFeed(\ag%s\ax).", PLUGIN_NAME, (FeedAt) ? Buffer : "\aroff");
		if (bAnnLevels) WriteChatf("\ay%s\aw:: Current Thirst(\ag%d\ax) Hunger(\ag%d\ax)", PLUGIN_NAME, GetPcProfile()->thirstlevel, GetPcProfile()->hungerlevel);
	}
	else if (!_stricmp(Arg, "announceConsume")) {
		if (bAnnConsume) {
			bAnnConsume = false;
			WriteChatf("\ay%s\aw::Consumption Notification Off", PLUGIN_NAME);
		}
		else {
			bAnnConsume = true;
			WriteChatf("\ay%s\aw::Consumption Notification On", PLUGIN_NAME);
		}
	}
}

void AutoDrinkCmd(PSPAWNINFO pLPlayer, char* szLine)
{
	if (!strlen(szLine)) {
		if (GoodToFeed()) Consume(ItemClass_Drink, Thirst);
		return;
	}
	CHAR Arg[MAX_STRING] = { 0 };
	GetArg(Arg, szLine, 1);
	if (!_stricmp(Arg, "list")) {
		WriteChatf("\ay%s\aw:: Listing Drink:", PLUGIN_NAME);
		ListTypes(Thirst);
		if (bAnnLevels) {
			sprintf_s(Buffer, "%d", DrnkAt);
			WriteChatf("\ay%s\aw:: AutoDrink(\ag%s\ax).", PLUGIN_NAME, (DrnkAt) ? Buffer : "\aroff");
			WriteChatf("\ay%s\aw:: Current Thirst(\ag%d\ax)", PLUGIN_NAME, GetPcProfile()->thirstlevel);
		}
	} else if (!_stricmp(Arg, "warn")) {
		if (bDrinkWarn) {
			bDrinkWarn = 0;
			WriteChatf("\ay%s\aw:: Drink Warning Off", PLUGIN_NAME);
		} else {
			bDrinkWarn = 1;
			WriteChatf("\ay%s\aw:: Drink Warning On", PLUGIN_NAME);
		}
	}
	else if (!_stricmp(Arg, "reload")) {
			ReadList(&Hunger, "FOOD");
			ReadList(&Thirst, "DRINK");
	}
	else if (!_stricmp(Arg, "add")) {
		if (!ItemOnCursor()) {
			WriteChatf("%s:: \arNeed to have a food item on your cursor to do this.", PLUGIN_NAME);
			return;
		}
		if (CONTENTS* item = GetPcProfile()->GetInventorySlot(InvSlot_Cursor)) {
			if (PITEMINFO pIteminf = GetItemFromContents(item)) {
				if (pIteminf->FoodDuration) {
					WriteChatf("%s:: \ayFound Item: \ap%s", PLUGIN_NAME, pIteminf->Name);
					char temp[MAX_STRING] = "";
					char ItemtoAdd[MAX_STRING] = "";
					int FoodIndex = Thirst.size() + 1;
					sprintf_s(temp, "Drink%i", FoodIndex);
					sprintf_s(ItemtoAdd, "%s", pIteminf->Name);
					std::list<std::string>::iterator pTempList;
					pTempList = Thirst.begin();
					while (pTempList != Thirst.end()) {
						strcpy_s(FindName, pTempList->c_str());
						if (FindItemByName(FindName, true)) {
							if (!_stricmp(FindName, ItemtoAdd)) {
								WriteChatf("%s:: \ap%s \aris already on the list", PLUGIN_NAME, ItemtoAdd);
								return;
							}
						}
						pTempList++;
					}
					WritePrivateProfileString("Drink", temp, ItemtoAdd, INIFileName);
					EzCommand("/autoinv");
					WriteChatf("%s \agAdded\aw: \ap%s \ayto your autodrink list", PLUGIN_NAME, ItemtoAdd);
					ReadList(&Thirst, "DRINK");
				}
				else {
					WriteChatf("%s:: \arThat's not a drink. Don't be rediculous", PLUGIN_NAME);
				}
			}
		}
	}
	else if (IsNumber(Arg)) {
		DrnkAt = atoi(Arg);
		if (DrnkAt < 0) DrnkAt = 0;
		else if (DrnkAt > 5000) DrnkAt = 5000;
		sprintf_s(Buffer, "%d", DrnkAt);
		WritePrivateProfileString(GetCharInfo()->Name, "AutoDrink", Buffer, INIFileName);
		WriteChatf("\ay%s\aw:: AutoDrink(\ag%s\ax).", PLUGIN_NAME, (DrnkAt) ? Buffer :"\aroff");
		if (bAnnLevels) WriteChatf("\ay%s\aw:: Current Thirst(\ag%d\ax) Hunger(\ag%d\ax)", PLUGIN_NAME, GetPcProfile()->thirstlevel, GetPcProfile()->hungerlevel);
	}
	else if (!_stricmp(Arg, "announceConsume")) {
		if (bAnnConsume) {
			bAnnConsume = false;
			WriteChatf("\ay%s\aw::Consumption Notification Off", PLUGIN_NAME);
		}
		else {
			bAnnConsume = true;
			WriteChatf("\ay%s\aw::Consumption Notification On", PLUGIN_NAME);
		}
	}
}

PLUGIN_API void OnPulse()
{
	static int Pulses = 0;
	if (++Pulses < 50) return;
	Pulses = 0;
	if (!GoodToFeed()) return;
	if (DrnkAt && (LONG)GetPcProfile()->thirstlevel < DrnkAt) Consume(ItemClass_Drink,Thirst);
	if (FeedAt && (LONG)GetPcProfile()->hungerlevel < FeedAt) Consume(ItemClass_Food,Hunger);
}

PLUGIN_API DWORD OnIncomingChat(PCHAR Line, DWORD Color)
{
	if (!IAmCamping && strstr(Line, "It will take you about 30 seconds to prepare your camp.")) {
		IAmCamping = true;
	}
	else if (IAmCamping && strstr(Line, "You abandon your preparations to camp.")) {
		IAmCamping = false;
	}
	return 0;
}

PLUGIN_API VOID OnZoned()
{
	//If I switch characters and IAmCamping is still true and I finish zoning, and the gamestate is ingame...
	if (IAmCamping && GetGameState() == GAMESTATE_INGAME)
		IAmCamping = false;
}

PLUGIN_API void SetGameState(DWORD GameState)
{
	if (GameState == GAMESTATE_INGAME) {
		if (GetPcProfile()) {
			DrnkAt     = GetPrivateProfileInt(GetCharInfo()->Name, "AutoDrink", 0, INIFileName);
			FeedAt     = GetPrivateProfileInt(GetCharInfo()->Name, "AutoFeed",  0, INIFileName);
			bAnnLevels = GetPrivateProfileInt("Settings",          "Announce",  1, INIFileName) != 0;
			bAnnConsume = GetPrivateProfileInt("Settings",         "Announce", 1, INIFileName) != 0;
			bFoodWarn  = GetPrivateProfileInt("Settings",          "FoodWarn",  0, INIFileName) != 0;
			bDrinkWarn = GetPrivateProfileInt("Settings",          "DrinkWarn",  0, INIFileName) != 0;

			WritePrivateProfileString("Settings", "Announce", bAnnLevels ? "1" : "0", INIFileName);
			WritePrivateProfileString("Settings", "FoodWarn", bFoodWarn ? "1" : "0", INIFileName);
			WritePrivateProfileString("Settings", "DrinkWarn", bDrinkWarn ? "1" : "0", INIFileName);
			if (!Loaded) {
				ReadList(&Hunger,"FOOD");
				ReadList(&Thirst,"DRINK");
				Loaded = true;
			}
		}
	}
}

PLUGIN_API void InitializePlugin()
{
	AddCommand("/autofeed",  AutoFeedCmd);
	AddCommand("/autodrink", AutoDrinkCmd);
}

PLUGIN_API void ShutdownPlugin()
{
	RemoveCommand("/autofeed");
	RemoveCommand("/autodrink");
}
