#pragma once

std::string strRank[] = {
    "Warrior III * 1",
    "Warrior III * 2",
    "Warrior III * 3",
    "Warrior II * 0",
    "Warrior II * 1",
    "Warrior II * 2",
    "Warrior II * 3",
    "Warrior I * 0",
    "Warrior I * 1",
    "Warrior I * 2",
    "Warrior I * 3",
    "Elite III * 0",
    "Elite III * 1",
    "Elite III * 2",
    "Elite III * 3",
    "Elite III * 4",
    "Elite II * 0",
    "Elite II * 1",
    "Elite II * 2",
    "Elite II * 3",
    "Elite II * 4",
    "Elite I * 0",
    "Elite I * 1",
    "Elite I * 2",
    "Elite I * 3",
    "Elite I * 4",
    "Master IV * 0",
    "Master IV * 1",
    "Master IV * 2",
    "Master IV * 3",
    "Master IV * 4",
    "Master III * 0",
    "Master III * 1",
    "Master III * 2",
    "Master III * 3",
    "Master III * 4",
    "Master II * 0",
    "Master II * 1",
    "Master II * 2",
    "Master II * 3",
    "Master II * 4",
    "Master I * 0",
    "Master I * 1",
    "Master I * 2",
    "Master I * 3",
    "Master I * 4",
    "Grandmaster V * 0",
    "Grandmaster V * 1",
    "Grandmaster V * 2",
    "Grandmaster V * 3",
    "Grandmaster V * 4",
    "Grandmaster V * 5",
    "Grandmaster IV * 0",
    "Grandmaster IV * 1",
    "Grandmaster IV * 2",
    "Grandmaster IV * 3",
    "Grandmaster IV * 4",
    "Grandmaster IV * 5",
    "Grandmaster III * 0",
    "Grandmaster III * 1",
    "Grandmaster III * 2",
    "Grandmaster III * 3",
    "Grandmaster III * 4",
    "Grandmaster III * 5",
    "Grandmaster II * 0",
    "Grandmaster II * 1",
    "Grandmaster II * 2",
    "Grandmaster II * 3",
    "Grandmaster II * 4",
    "Grandmaster II * 5",
    "Grandmaster I * 0",
    "Grandmaster I * 1",
    "Grandmaster I * 2",
    "Grandmaster I * 3",
    "Grandmaster I * 4",
    "Grandmaster I * 5",
    "Epic V * 0",
    "Epic V * 1",
    "Epic V * 2",
    "Epic V * 3",
    "Epic V * 4",
    "Epic V * 5",
    "Epic IV * 0",
    "Epic IV * 1",
    "Epic IV * 2",
    "Epic IV * 3",
    "Epic IV * 4",
    "Epic IV * 5",
    "Epic III * 0",
    "Epic III * 1",
    "Epic III * 2",
    "Epic III * 3",
    "Epic III * 4",
    "Epic III * 5",
    "Epic II * 0",
    "Epic II * 1",
    "Epic II * 2",
    "Epic II * 3",
    "Epic II * 4",
    "Epic II * 5",
    "Epic I * 0",
    "Epic I * 1",
    "Epic I * 2",
    "Epic I * 3",
    "Epic I * 4",
    "Epic I * 5",
    "Legend V * 0",
    "Legend V * 1",
    "Legend V * 2",
    "Legend V * 3",
    "Legend V * 4",
    "Legend V * 5",
    "Legend IV * 0",
    "Legend IV * 1",
    "Legend IV * 2",
    "Legend IV * 3",
    "Legend IV * 4",
    "Legend IV * 5",
    "Legend III * 0",
    "Legend III * 1",
    "Legend III * 2",
    "Legend III * 3",
    "Legend III * 4",
    "Legend III * 5",
    "Legend II * 0",
    "Legend II * 1",
    "Legend II * 2",
    "Legend II * 3",
    "Legend II * 4",
    "Legend II * 5",
    "Legend I * 0",
    "Legend I * 1",
    "Legend I * 2",
    "Legend I * 3",
    "Legend I * 4",
    "Legend I * 5"
};

std::string RankToString(int uiRankLevel, int iMythPoint) {
	if (uiRankLevel > 135) {
		if (iMythPoint >= 600) {
            return "Mythical Glory: " + to_string(iMythPoint);
        } else {
            return "Mythic: " + to_string(iMythPoint);
        }
	} else {
		return strRank[uiRankLevel];
	}
}

std::string strHero[] = {
    "-",
    "Miya",
    "Balmond",
    "Saber",
    "Alice",
    "Nana",
    "Tigreal",
    "Alucard",
    "Karina",
    "Akai",
    "Franco",
    "Bane",
    "Bruno",
    "Clint",
    "Rafaela",
    "Eudora",
    "Zilong",
    "Fanny",
    "Layla",
    "Minotaur",
    "Lolita",
    "Hayabusa",
    "Freya",
    "Gord",
    "Natalia",
    "Kagura",
    "Chou",
    "Sun",
    "Alpha",
    "Ruby",
    "Yi Sun-shin",
    "Moskov",
    "Johnson",
    "Cyclops",
    "Estes",
    "Hilda",
    "Aurora",
    "Lapu-Lapu",
    "Vexana",
    "Roger",
    "Karrie",
    "Gatotkaca",
    "Harley",
    "Irithel",
    "Grock",
    "Argus",
    "Odette",
    "Lancelot",
    "Diggie",
    "Hylos",
    "Zhask",
    "Helcurt",
    "Pharsa",
    "Lesley",
    "Jawhead",
    "Angela",
    "Gusion",
    "Valir",
    "Martis",
    "Uranus",
    "Hanabi",
    "Chang'e",
    "Kaja",
    "Selena",
    "Aldous",
    "Claude",
    "Vale",
    "Leomord",
    "Lunox",
    "Hanzo",
    "Belerick",
    "Kimmy",
    "Thamuz",
    "Harith",
    "Minsitthar",
    "Kadita",
    "Faramis",
    "Badang",
    "Khufra",
    "Granger",
    "Guinevere",
    "Esmeralda",
    "Terizla",
    "X.Borg",
    "Ling",
    "Dyrroth",
    "Lylia",
    "Baxia",
    "Masha",
    "Wanwan",
    "Silvanna",
    "Cecilion",
    "Carmilla",
    "Atlas",
    "Popol & Kupa",
    "Yu Zhong",
    "Luo Yi",
    "Benedetta",
    "Khaleed",
    "Barats",
    "Brody",
    "Yve",
    "Mathilda",
    "Paquito",
    "Gloo",
    "Beatrix",
    "Phoveus",
    "Natan",
    "Aulus",
    "Aamon",
    "Valentina",
    "Edith",
    "Floryn",
    "Yin",
    "Melissa",
    "Xavier",
    "Julian",
	"Fredrinn",
	"Joy",
	"Novaria",
	"Arlott",
	"Ixia",
	"Nolan",
	"Cici",
	"Chip",
	"Zhuxin",
	"Suryou",
	"Lukas",
};

std::string HeroToString(int heroid) {
	if (strHero[heroid] != "") {
		return strHero[heroid];
	} else {
		return "Hero ID: " + to_string(heroid); 
	}
}

std::string SpellToString(int summonSkillId) {
    std::string strSpell;
    switch(summonSkillId) {
	case 0:
		strSpell += "-";
        break;
    case 20150:
        strSpell += "Execute";
        break;
	case 20020:
        strSpell += "Retribution";
        break;
	case 20030:
        strSpell += "Inspire";
        break;
	case 20040:
        strSpell += "Sprint";
        break;
	case 20050:
        strSpell += "Revitalize";
        break;
	case 20060:
        strSpell += "Aegis";
        break;
	case 20070:
        strSpell += "Petrify";
        break;
	case 20080:
        strSpell += "Purify";
        break;
	case 20140:
        strSpell += "Flameshot";
        break;
	case 20100:
        strSpell += "Flicker";
        break;
	case 20160:
        strSpell += "Arrival";
        break;
	case 20190:
        strSpell += "Vengeance";
        break;
	default:
	    strSpell += to_string(summonSkillId);
	}
	return strSpell;
}

std::string MonsterToString(int hero_id) {
    std::string strMonster;
    switch(hero_id) {
	case 2002:
        strMonster += "Lord";
        break;
	case 2003:
        strMonster += "Turtle";
        break;
	case 2004:
        strMonster += "Fiend";
        break;
    case 2005:
        strMonster += "Serpent";
        break;
	case 2006:
        strMonster += "Scaled Lizard";
        break;
	case 2008:
        strMonster += "Crammer";
        break;
	case 2009:
        strMonster += "Rockursa";
        break;
	case 2011:
		strMonster += "Crab";
		break;
    case 2012:
        strMonster += "Serpent kids";
        break;
    case 2013:
        strMonster += "Crab";
        break;
    case 2056:
        strMonster += "Lithowanderer";
        break;
	case 2059:
		strMonster += "Crammer";
		break;
	case 2072:
		strMonster += "Lithowanderer";
		break;
    default:
        strMonster += ""/*to_string(m_id)*/;
    }
    return strMonster;
}

int ListSummonSkillId[] = {
    20150,
    20020,
    20030,
    20040,
    20050,
    20060,
    20070,
    20080,
    20140,
    20100,
    20160,
    20190
};

bool SpellIdExist(int iValue) {
	return std::find(std::begin(ListSummonSkillId), std::end(ListSummonSkillId), iValue) != std::end(ListSummonSkillId);
}

enum SPELL_CAST_RESULT {
	SPELL_CAST_NOCARE = -100,
	SPELL_CAST_NO_BASE_SPELL = -99,
	SPELL_CAST_NO_OWNER = -98,
	SPELL_CAST_IN_COOLDOWN = -97,
	SPELL_CAST_ALLOC_MEM = -96,
	SPELL_CAST_BULLET_NO_SPEED = -95,
	SPELL_CAST_NO_ATT_SPEED = -94,
	SPELL_CAST_NO_BASE_EFFECT = -93,
	SPELL_CAST_WRONG_STATUS = -92,
	SPELL_CAST_WRONG_STATUS_NOT_ATTACK = -91,
	SPELL_CAST_OUT_DIS = -90,
	SPELL_CAST_DEATH = -89,
	SPELL_CAST_TRANSFER_ERROR = -88,
	SPELL_CAST_CONTROL_NO_ADDTION = -87,
	SPELL_CAST_TRANSER_SINGLE = -86,
	SPELL_CAST_NO_ENOUGH_HP = -85,
	SPELL_CAST_NO_ENOUGH_MP = -84,
	SPELL_CAST_NO_ENOUGH_XP = -83,
	SPELL_CAST_NO_UNIT = -82,
	SPELL_CAST_WRONG_POS = -81,
	SPELL_CAST_LOOP_CAST = -80,
	SPELL_CAST_NO_SPELL = -79,
	SPELL_CAST_NO_ENOUGH_LEVEL = -78,
	SPELL_CAST_NO_LOCKTARGET = -77,
	SPELL_LOCK_CATCHMOVE = -76,
	SPELL_SEVER_BUSY = -75,
	SPELL_MOVE_NO_LOCKTARGET = -74,
	SPELL_CAST_WRONG_BEHAVIOR = -73,
	SPELL_CAST_WRONG_LOCKSKILLID = -72,
	SPELL_CAST_WRONG_LOCKSKILLBUFFID = -71,
	SPELL_Cheack_Target_Inevitable = -70,
	SPELL_Cheack_OneFrameSkillMaxNum = -69,
	SPELL_IN_AUTO_ATTACK_AI = -68,
	SPELL_Cheack_TranSkillID_Inevitable = -67,
	SPELL_NO_FIGHTER = -66,
	SPELL_FAIL_NOT_QUICK_MOVE = -65,
	SPELL_CAST_ALREADY_LEARNED = -64,
	SPELL_Cheack_Xuli_Inevitable = -50,
	SPELL_Cheack_SkillOvelap = -51,
	SPELL_IsStoreSkill = -52,
	SPELL_IsStoreSkill_Inevitable = -53,
	SPELL_CAST_SUCCESS = 0,
};

