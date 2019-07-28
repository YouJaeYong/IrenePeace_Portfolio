#pragma once


#define CS_LOGIN 1
#define CS_MOVE 2
#define CS_ATTACK 3
#define CS_CHAT 4

#define SC_LOGIN_OK 1
#define SC_LOGIN_FAIL 2
#define SC_POSITION_INFO 3
#define SC_CHAT 4
#define SC_STAT_CHANGE 5
#define SC_REMOVE_OBJECT 6
#define SC_ADD_OBJECT 7
#define SC_ATTACK 8
#define SC_PACKET_NPC_DIED 9
#define SC_NPC_ATTACK 10
#define SC_PLAYER_DIED 11
#define SC_HEALING 12
#define SC_LOGIN_OK_EX 13


#define MAX_STR_SIZE 50
enum DIRECTION
{
	UP,
	DOWN,
	LEFT,
	RIGHT,
};


#define BOARD_HEIGHT 300
#define BOARD_WIDTH 300

#define MAX_BUFFER      1024
#define SERVER_PORT      3500
#define MAX_USER      10
#define NPC_START 10000
#define MAX_NPC 17000

#define SIGHT_ARRANGE 18 // 반경 4일 때 가장 멀 때 거리는 루트18임.
#define RADIUS 7

#define EVT_RECV 0
#define EVT_SEND 1
#define EVT_NPC_MOVE 2

#pragma pack(push,1)
struct cs_packet_request_login { //CS_LOGIN 1
	unsigned char size;
	unsigned char type;
	wchar_t name[10];
};
struct cs_packet_move {//CS_MOVE 2
	unsigned char size;
	unsigned char type;
	BYTE DIR;//packet[2]
};
struct cs_attack_packet// CS_ATTACK 3
{
	unsigned char size;
	unsigned char type;
};
struct cs_chat { //CS_CHAR 4
	unsigned char size;
	unsigned char type;
	wchar_t message[100];
};

/////////////////////////SERVER->CLIENT///////////////////////

struct sc_packet_login_success {// SC_LOGIN_OK 1
	unsigned char size;
	unsigned char type;
	WORD id;
	WORD X_POS;
	WORD Y_POS;
	WORD HP;
	WORD LEVEL;
	DWORD EXP;
};
struct sc_packet_login_failed { //SC_LOGIN_FAIL 2
	unsigned char size;
	unsigned char type;
};
struct sc_packet_pos {//SC_POSITION_INFO 3 
	unsigned char size;
	unsigned char type;
	WORD ID;
	WORD X_POS;
	WORD Y_POS;
};
struct sc_chat {//SC_CHAT 4
	unsigned char size;
	unsigned char type;
	WORD CHAT_ID;
	wchar_t message[100];
};
struct sc_stat_change {//SC_STAT_CHANGE 5
	unsigned char size;
	unsigned char type;
	WORD HP;
	WORD LEVEL;
	DWORD EXP;
};
struct sc_packet_remove_player { //SC_REMOVE_OBJECT 6
	unsigned char size;
	unsigned char type;
	WORD id;
};
struct sc_packet_put_player {//SC_ADD_OBJECT 7 
	unsigned char size;
	unsigned char type;
	WORD ID;
	BYTE TYPE;
};







struct sc_npc_died_packet {
	unsigned char size;
	unsigned char type;
	unsigned int id;
	unsigned int slayer_id;
	unsigned int exp;
};



struct sc_packet_login_success_ex {// SC_LOGIN_OK_EX 13
	unsigned char size;
	unsigned char type;
	WORD power;
	wchar_t name[10];
	wchar_t weapon[20];
};

struct sc_healing_packet {
	unsigned char size;
	unsigned char type;
	unsigned short hp;
};

struct sc_packet_npc_attack
{
	unsigned char size;
	unsigned char type;
	unsigned char damage;
};

struct sc_attack_packet
{
	unsigned char size;
	unsigned char type;
	unsigned short id;
	unsigned short other_id;
	unsigned char damage;
};
struct sc_respawn_packet
{
	unsigned char size;
	unsigned char type;
	unsigned short id;
	unsigned short x;
	unsigned short y;
};
struct sc_packet_player_died
{
	unsigned char size;
	unsigned char type;
	unsigned short id;
	unsigned short other_id;
	unsigned short exp;
};
#pragma pack(pop)