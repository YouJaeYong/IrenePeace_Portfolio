#define _CRT_NON_CONFORMING_SWPRINTFS
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <vector>
#include <thread>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <queue>
#include <winsock2.h>
#include <windows.h>  
#include <random>
#include <sqlext.h>  
#include <fstream>
#include <time.h>
#include<stdio.h>
#include <stdlib.h>
#include "protocol.h"
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "lua53.lib")

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

int CAPI_send_chat_packet(lua_State *L);
int CAPI_get_x_position(lua_State *L);
int CAPI_get_y_position(lua_State *L);

using namespace std;
using namespace chrono;


#define UNICODE  
#define MAX_BUFFER		1024
#define NAME_LEN 50
char g_map_buffer[BOARD_WIDTH][BOARD_HEIGHT];
const int OP_RECV = 1;
const int OP_SEND = 2;
const int OP_MOVE = 3;
const int OP_ATTACK_COOLTIME = 4;
const int OP_NPC_DIED = 5;
const int OP_RESPAWN = 6;
const int OP_LEVEL_UP = 7;
const int OP_MOVE_COOLTIME = 8;
const int OP_NPC_ATTACK_COOLTIME = 9;
const int OP_PLAYER_DIED = 10;
const int OP_HEALING = 11;

const int NORMAL_MOB = 1;
const int AGGRESIVE_MOB = 2;
const int SLAYER_MOB = 3;


const int FIXING_MODE = 0;
const int LOAMING_MODE = 1;
const int CHASE_MODE = 2;
struct OverEx {
	WSAOVERLAPPED	m_wsa_over;
	WSABUF			m_wsa_buf;
	unsigned char	IOCPbuf[MAX_BUFFER];
	unsigned char	m_todo;
	unsigned int m_other_id;
};

class ClientObject {
public:

	wchar_t m_name[10];
	int		id;
	unsigned int level;
	unsigned int power;
	unsigned int fhp;
	int hp;
	unsigned int exp;
	wchar_t weapon[20];
	bool	m_connected;
	bool	m_moving;
	bool	m_attack;
	bool	m_alive;
	bool	m_battle;
	int		x, y;
	int npc_type;
	int m_movetype;
	int target;
	unordered_set <int>	v_list;
	mutex v_l;
	lua_State *L;
	SOCKET m_socket;
	OverEx m_over_ex;
	unsigned char m_packet_buf[MAX_BUFFER];
	unsigned short  m_prev_size;
};

HANDLE	g_hIOCP;
ClientObject g_clients[MAX_NPC];

const int job_move = 0;
const int job_attack_cooltime = 1;
const int job_move_player = 2;
const int npc_died = 2;
const int npc_respawn = 3;
const int player_died = 4;
const int player_respawn = 5;
const int job_npc_attack = 6;
const int job_respawn = 7;
const int job_healing = 8;
void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)//DB 오류 출력
{
	SQLSMALLINT iRec = 0;
	SQLINTEGER iError;
	WCHAR wszMessage[1000];
	WCHAR wszState[SQL_SQLSTATE_SIZE + 1];
	if (RetCode == SQL_INVALID_HANDLE) {
		fwprintf(stderr, L"Invalid handle!\n");
		return;
	}
	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
		(SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT *)NULL) == SQL_SUCCESS) {
		// Hide data truncated..
		if (wcsncmp(wszState, L"01004", 5)) {
			fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
		}
	}
}

class time_event {
public:
	int id;
	high_resolution_clock::time_point wake_time;
	int job;
};

class event_comp
{
public:
	event_comp() {}
	bool operator() (const time_event lhs, const time_event rhs) const
	{
		return (lhs.wake_time > rhs.wake_time);
	}
};

priority_queue <time_event, vector<time_event>, event_comp> timer_queue;
mutex tq_l;


void error_display(const char *msg, int err_no)
{
	WCHAR *lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	cout << msg;
	wcout << L"에러 " << lpMsgBuf << endl;
	while (true);
	LocalFree(lpMsgBuf);
}




void send_packet(int id, void *packet)
{
	OverEx *ex = new OverEx;
	memcpy(ex->IOCPbuf, packet,
		reinterpret_cast<unsigned char *>(packet)[0]);
	ex->m_todo = OP_SEND;
	ex->m_wsa_buf.buf = (char *)ex->IOCPbuf;
	ex->m_wsa_buf.len = ex->IOCPbuf[0];
	ZeroMemory(&ex->m_wsa_over, sizeof(WSAOVERLAPPED));

	int ret = WSASend(g_clients[id].m_socket, &ex->m_wsa_buf, 1, NULL, 0,
		&ex->m_wsa_over, 0);
	if (0 != ret) {
		int err_no = WSAGetLastError();
		if (WSA_IO_PENDING != err_no)
			error_display("SEND : ", err_no);
	}
}


void SendChatPacket(int client, int speaker, WCHAR *mess)
{
	wchar_t target[10];
	wsprintf(target, L"by %d!!", speaker);
	sc_chat p;
	p.size = sizeof(sc_chat);
	p.type = SC_CHAT;
	p.CHAT_ID = client;
	wcscpy_s(p.message, mess);
	wcscat_s(p.message, target);
	send_packet(client, &p);
}




bool is_player(int id)
{
	return id < MAX_USER;
}

bool Can_See(int a, int b)
{
	int dist = 0;
	dist += (g_clients[a].x - g_clients[b].x) * (g_clients[a].x - g_clients[b].x);
	dist += (g_clients[a].y - g_clients[b].y) * (g_clients[a].y - g_clients[b].y);

	if (dist <= RADIUS * RADIUS) return true;
	return false;
}

void add_timer(int id, int job_type, high_resolution_clock::time_point wt)
{
	timer_queue.push(time_event{ id, wt, job_type });
}

void wake_up_npc(int id)
{
	if (g_clients[id].m_alive == false) return;
	if (true == g_clients[id].m_moving) return;
	g_clients[id].m_moving = true;
	add_timer(id, job_move, high_resolution_clock::now() + 1s);
}
void attack_start_npc(int id)
{
	if (g_clients[id].m_alive == false) return;
	if (true == g_clients[id].m_attack) return;
	g_clients[id].m_attack = true;
	add_timer(id, job_npc_attack, high_resolution_clock::now() + 2s);
}
void move_cooltime(int id)
{
	if (g_clients[id].m_alive == false) return;
	if (true == g_clients[id].m_moving) return;
	g_clients[id].m_moving = true;
	add_timer(id, job_move_player, high_resolution_clock::now() + 200ms);
}
void attack_cooltime(int id)
{
	if (true == g_clients[id].m_attack) return;
	if (g_clients[id].m_alive == false) return;
	g_clients[id].m_attack = true;
	int critical = rand() % 10;
	g_clients[id].v_l.lock();
	unordered_set <int> vl = g_clients[id].v_list;
	g_clients[id].v_l.unlock();
	for (auto npc : vl)
	{
		if (is_player(npc))continue;
		int dist = 0;
		dist += (g_clients[id].x - g_clients[npc].x) * (g_clients[id].x - g_clients[npc].x);
		dist += (g_clients[id].y - g_clients[npc].y) * (g_clients[id].y - g_clients[npc].y);


		if (dist < 2)
		{
			sc_attack_packet packet;
			packet.size = sizeof(sc_attack_packet);
			packet.type = SC_ATTACK;
			packet.id = id;
			packet.other_id = npc;
			if (critical == 0)
			{
				packet.damage = g_clients[id].power * 2;
				g_clients[npc].hp -= g_clients[id].power;
				printf("CRITICAL! %d Player가 %d NPC에게 %d피해를 줌\n", id, npc, packet.damage);
			}
			else
			{
				packet.damage = g_clients[id].power;
				g_clients[npc].hp -= g_clients[id].power;
				printf("%d Player가 %d NPC에게 %d피해를 줌\n", id, npc, packet.damage);
			}
			if (g_clients[npc].m_movetype == FIXING_MODE)
			{
				int player_id = id;
				lua_State *L = g_clients[npc].L;
				lua_getglobal(L, "player_notify");
				lua_pushnumber(L, player_id);
				lua_pushnumber(L, g_clients[id].x);
				lua_pushnumber(L, g_clients[id].y);
				lua_pcall(L, 3, 0, 0);
				attack_start_npc(npc);
				
			}
			if (g_clients[npc].hp <= 0)
			{
				g_clients[npc].target = -1;
				g_clients[id].exp += g_clients[npc].exp;
				if (g_clients[npc].npc_type == NORMAL_MOB)
					g_clients[npc].m_movetype = FIXING_MODE;

				else if (g_clients[npc].npc_type == AGGRESIVE_MOB)
					g_clients[npc].m_movetype = LOAMING_MODE;
				else g_clients[npc].m_movetype = LOAMING_MODE;

				OverEx *ex_over = new OverEx;
				ex_over->m_todo = OP_NPC_DIED;
				ex_over->m_other_id = id;
				PostQueuedCompletionStatus(g_hIOCP, 1, npc, &
					ex_over->m_wsa_over);

				if (g_clients[id].exp > g_clients[id].level * 100)
				{
					g_clients[id].exp = 0;
					g_clients[id].level++;
					g_clients[id].fhp = 100 + (g_clients[id].level * 30);
					g_clients[id].power = 10 + (g_clients[id].level * 5);
					OverEx *ex_over = new OverEx;
					ex_over->m_todo = OP_LEVEL_UP;

					PostQueuedCompletionStatus(g_hIOCP, 1, id, &
						ex_over->m_wsa_over);
				}
			}
			if (g_clients[npc].m_movetype == LOAMING_MODE)
			{
				g_clients[npc].target = id;
				g_clients[npc].m_movetype = CHASE_MODE;

				attack_start_npc(npc);
				int player_id = id;
				lua_State *L = g_clients[npc].L;
				lua_getglobal(L, "player_notify");
				lua_pushnumber(L, player_id);
				lua_pushnumber(L, g_clients[id].x);
				lua_pushnumber(L, g_clients[id].y);
				lua_pcall(L, 3, 0, 0);

			}
			send_packet(id, &packet);
		}
	}
	add_timer(id, job_attack_cooltime, high_resolution_clock::now() + 1s);
}

int CAPI_send_chat_packet(lua_State *L)
{
	int client = lua_tonumber(L, -3);
	int speaker = lua_tonumber(L, -2);
	char *mess = (char *)lua_tostring(L, -1);
	lua_pop(L, 4);

	size_t len = strlen(mess);
	if (len > MAX_STR_SIZE - 1) len = MAX_STR_SIZE - 1;
	size_t wlen = 0;
	WCHAR wmess[MAX_STR_SIZE + MAX_STR_SIZE];
	mbstowcs_s(&wlen, wmess, len, mess, _TRUNCATE);
	wmess[MAX_STR_SIZE - 1] = '\0';

	SendChatPacket(client, speaker, wmess);
	return 0;
}

int CAPI_get_x_position(lua_State *L)
{
	int obj_id = lua_tonumber(L, -1);
	lua_pop(L, 2);
	int x = g_clients[obj_id].x;
	lua_pushnumber(L, x);
	return 1;
}

int CAPI_get_y_position(lua_State *L)
{
	int obj_id = lua_tonumber(L, -1);
	lua_pop(L, 2);
	int y = g_clients[obj_id].y;
	lua_pushnumber(L, y);
	return 1;
}
void InitializeNetwork()
{
	WSADATA WSAData;
	if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0)
	{
		cout << "Error - Can not load 'winsock.dll' file\n";
		exit(-1);
	}

	g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
}

void InitializeNPC()
{
	std::default_random_engine generator;
	volatile int x = 0;
	volatile int y = 0;
	for (int i = NPC_START; i < MAX_NPC; ++i)
	{
		if (i < 13333)
		{
			std::uniform_int_distribution<int> distribution(0, 125);
			x = distribution(generator) + 15;
			std::uniform_int_distribution<int> distribution2(0, 140);
			y = distribution2(generator);
			if (g_map_buffer[y][x] == '0' || g_map_buffer[y][x] == '5')
				continue;
			g_clients[i].id = i;
			g_clients[i].m_alive = true;
			g_clients[i].m_connected = true;
			g_clients[i].m_moving = false;
			g_clients[i].x = x;
			g_clients[i].y = y;
			g_clients[i].npc_type = NORMAL_MOB;
			g_clients[i].m_movetype = FIXING_MODE;
			g_clients[i].fhp = 30;
			g_clients[i].hp = g_clients[i].fhp;
			g_clients[i].exp = 20;
			g_clients[i].power = 10;
			g_clients[i].m_attack = false;
		}
		if (i > 13332 && i < 16666)
		{


			std::uniform_int_distribution<int> distribution(0, 130);


			x = distribution(generator) + 140;
			std::uniform_int_distribution<int> distribution2(0, 270);
			y = distribution2(generator);
			if (g_map_buffer[y][x] == '0' || g_map_buffer[y][x] == '5')
				continue;
			g_clients[i].id = i;
			g_clients[i].m_alive = true;
			g_clients[i].m_connected = true;
			g_clients[i].m_moving = false;
			g_clients[i].x = x;
			g_clients[i].y = y;
			g_clients[i].npc_type = AGGRESIVE_MOB;
			g_clients[i].m_movetype = LOAMING_MODE;
			g_clients[i].fhp = 100;
			g_clients[i].hp = g_clients[i].fhp;
			g_clients[i].exp = 100;
			g_clients[i].power = 30;
			g_clients[i].m_attack = false;

		}
		if (i > 16665)
		{


			std::uniform_int_distribution<int> distribution(0, 30);

			x = distribution(generator) + 270;

			std::uniform_int_distribution<int> distribution2(0, 300);
			y = distribution2(generator);
			if (g_map_buffer[y][x] == '0' || g_map_buffer[y][x] == '5')
				continue;

			g_clients[i].id = i;
			g_clients[i].m_alive = true;
			g_clients[i].m_connected = true;
			g_clients[i].m_moving = false;
			g_clients[i].x = x;
			g_clients[i].y = y;
			g_clients[i].npc_type = SLAYER_MOB;
			g_clients[i].fhp = 500;
			g_clients[i].hp = g_clients[i].fhp;
			g_clients[i].exp = 500;
			g_clients[i].power = 50;
			g_clients[i].m_movetype = LOAMING_MODE;
			g_clients[i].m_attack = false;


		}
		lua_State *L = luaL_newstate();
		luaL_openlibs(L);
		luaL_loadfile(L, "notify.lua");
		lua_pcall(L, 0, 0, 0);

		lua_getglobal(L, "set_myid");
		lua_pushnumber(L, i);
		lua_pcall(L, 1, 0, 0);

		lua_register(L, "API_send_chat_packet", CAPI_send_chat_packet);
		lua_register(L, "API_get_x_position", CAPI_get_x_position);
		lua_register(L, "API_get_y_position", CAPI_get_y_position);
		g_clients[i].L = L;
	}
	cout << "VM loading completed!\n";
}

void EndNetwork()
{
	WSACleanup();
}

void OverlappedRecv(int id)
{
	DWORD flags = 0;
	ZeroMemory(&g_clients[id].m_over_ex.m_wsa_over, sizeof(WSAOVERLAPPED));
	if (WSARecv(g_clients[id].m_socket,
		&g_clients[id].m_over_ex.m_wsa_buf, 1,
		NULL, &flags, &(g_clients[id].m_over_ex.m_wsa_over), 0))
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			printf("Error - IO pending Failure\n");
			return;
		}
	}
	else {
		cout << "Non Overlapped Recv return.\n";
		return;
	}
}


void send_put_object_packet(int client, int obj)
{
	sc_packet_put_player packet;
	packet.ID = obj;
	packet.size = sizeof(sc_packet_put_player);
	packet.type = SC_ADD_OBJECT;
	if (is_player(obj))
		packet.TYPE = 0;// 유저
	else
		packet.TYPE = 1;// 몬스터	send_packet(client, &packet);
	send_packet(client, &packet);
	sc_packet_pos pos_packet;
	pos_packet.ID = obj;
	pos_packet.X_POS = g_clients[obj].x;
	pos_packet.Y_POS = g_clients[obj].y;
	pos_packet.size = sizeof(sc_packet_pos);
	pos_packet.type = SC_POSITION_INFO;
	send_packet(client, &pos_packet);
}

void send_remove_object_packet(int client, int obj)
{
	sc_packet_remove_player packet;
	packet.id = obj;
	packet.size = sizeof(sc_packet_remove_player);
	packet.type = SC_REMOVE_OBJECT;
	send_packet(client, &packet);
}
void send_npc_died_packet(int obj, int client)
{
	sc_npc_died_packet packet;
	packet.id = obj;
	packet.slayer_id = client;
	packet.exp = g_clients[obj].exp;
	packet.size = sizeof(sc_npc_died_packet);
	packet.type = SC_PACKET_NPC_DIED;
	for (int i = 0; i < MAX_USER; ++i)
	{
		if (!g_clients[i].m_connected)continue;
		if (0 == g_clients[i].v_list.count(obj))continue;
		if (Can_See(i, obj))
			send_packet(i, &packet);
	}

}
void send_levelup_packet(int id)
{
	sc_stat_change packet;
	packet.LEVEL = g_clients[id].level;//레벨업 이벤트 보낼때 미리 ++해둠
	packet.HP = g_clients[id].fhp;//레벨업 이벤트 보낼 때 미리 최대체력 증가 처리해둠
	packet.EXP = 0; //레벨업 했으니 0으로
	packet.size = sizeof(sc_stat_change);
	packet.type = SC_STAT_CHANGE;
	send_packet(id, &packet);
}
void Send_login_packet(wchar_t *name, int id)
{


	for (int i = 0; i < MAX_USER; ++i)
	{
		if (false == g_clients[i].m_connected)continue;

		if (wcscmp(g_clients[i].m_name, name) == 0)
		{
			printf("잘못된 아이디.. 로그인 실패 \n");
			sc_packet_login_failed packet;
			packet.size = sizeof(sc_packet_login_failed);
			packet.type = SC_LOGIN_FAIL;
			send_packet(id, &packet);
		}
	}
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;
	SQLWCHAR szName[NAME_LEN], szWeapon[NAME_LEN];
	SQLINTEGER ch_x, ch_y, ch_level, ch_exp, ch_power, ch_hp;
	SQLLEN cbName = 0, cbx = 0, cby = 0, cbexp = 0, cbpower = 0, cblevel = 0, cbweapon = 0, cbhp = 0;//cb -콜백
	wchar_t query[200] = { 0 };
	wchar_t insert_query[200] = { 0 };
	wchar_t weapon[10] = { 0 };
	swprintf_s(query, L"SELECT c_name, c_xpos, c_ypos, c_level, c_power, c_weapon, c_exp, c_hp FROM [2013184019유재용_숙제7번].[dbo].[C_Table1] WHERE c_name = '%s' ", name);
	swprintf_s(insert_query, L"INSERT INTO [2013184019유재용_숙제7번].[dbo].[C_Table1] VALUES('%s', %d, %d, %d, %d, '%s', %d, %d)",
		name, 5, 5, 1, 15, weapon, 0, 130);

	wprintf(L"%s", query);
	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
	{
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
		{
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
			{
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"수금 2013184019 유재용 숙제7번", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
				{
					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
					retcode = SQLExecDirect(hstmt, (SQLWCHAR *)query, SQL_NTS);

					SQLLEN count = 0;
					SQLRowCount(hstmt, &count);
					if (count == 0)
					{
						retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
						retcode = SQLExecDirect(hstmt,
							(SQLWCHAR *)insert_query,
							SQL_NTS);
						wprintf(L"신규 아이디 생성 : '%s'\n", name);
						retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
						retcode = SQLExecDirect(hstmt,
							(SQLWCHAR *)query,
							SQL_NTS);

						if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
							HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
					}

					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
					{
						// Bind columns 1, 2, and 3  
						retcode = SQLBindCol(hstmt, 1, SQL_C_CHAR, szName, NAME_LEN, &cbName);
						retcode = SQLBindCol(hstmt, 2/*필드*/, SQL_C_LONG/*자료형(int)*/, &ch_x, 100/*필드의 최대 길이*/, &cbx/*콜백*/);
						retcode = SQLBindCol(hstmt, 3, SQL_C_LONG, &ch_y, 100, &cby);
						retcode = SQLBindCol(hstmt, 4, SQL_C_LONG, &ch_level, 100, &cblevel);
						retcode = SQLBindCol(hstmt, 5, SQL_C_LONG, &ch_power, 100, &cbpower);
						retcode = SQLBindCol(hstmt, 6, SQL_C_CHAR, szWeapon, NAME_LEN, &cbweapon);
						retcode = SQLBindCol(hstmt, 7, SQL_C_LONG, &ch_exp, 100, &cbexp);
						retcode = SQLBindCol(hstmt, 8, SQL_C_LONG, &ch_hp, 100, &cbhp);
						// Fetch and print each row of data. On an error, display a message and exit.  
						for (int i = 0; ; i++)
						{
							retcode = SQLFetch(hstmt);//첫번째 출력결과를 꺼내서 바인딩된 변수에 집어넣는다(&ch_id)
							if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
								HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
							if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
							{
								wprintf(L"%d: id : %S	xpos : %d	ypos : %d	level : %d	exp: %d	hp : %d	weapon	: %S\n", i + 1, szName, ch_x, ch_y, ch_level, ch_exp, ch_hp, szWeapon);
								g_clients[id].x = ch_x;
								g_clients[id].y = ch_y;
								g_clients[id].hp = ch_hp;
								g_clients[id].level = ch_level;
								g_clients[id].fhp = 100 + (g_clients[id].level * 30);
								g_clients[id].exp = ch_exp;
								g_clients[id].power = 10 + (g_clients[id].level * 5);
								g_clients[id].m_alive = true;
								wcscpy_s(g_clients[id].m_name, sizeof(name), name);
								wcscpy_s(g_clients[id].weapon, sizeof(szWeapon), szWeapon);

							}
							else
								break;
						}
					}


					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
					}

					SQLDisconnect(hdbc);
				}
			}

			SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
		}
	}
	SQLFreeHandle(SQL_HANDLE_ENV, henv);

	sc_packet_login_success loginpacket;
	loginpacket.size = sizeof(sc_packet_login_success);
	loginpacket.id = id;
	loginpacket.type = SC_LOGIN_OK;
	loginpacket.X_POS = g_clients[id].x;
	loginpacket.Y_POS = g_clients[id].y;
	loginpacket.EXP = g_clients[id].exp;
	loginpacket.LEVEL = g_clients[id].level;
	loginpacket.HP = g_clients[id].hp;
	send_packet(id, &loginpacket);

	sc_packet_login_success_ex login_packet_ex;
	login_packet_ex.size = sizeof(sc_packet_login_success_ex);
	login_packet_ex.type = SC_LOGIN_OK_EX;
	wcscpy(login_packet_ex.name, g_clients[id].m_name);
	wcscpy(login_packet_ex.weapon, g_clients[id].weapon);
	login_packet_ex.power = g_clients[id].power;
	send_packet(id, &login_packet_ex);

	// 신규 플레이어에게 다른 클라이언트들의 위치를 알려준다.
	for (int i = 0; i < MAX_NPC; ++i) {
		if (false == g_clients[i].m_connected) continue;
		if (i == id) continue;
		if (false == Can_See(id, i)) continue;

		if (false == is_player(i)) {
			wake_up_npc(i);
			if (g_clients[i].npc_type == SLAYER_MOB)
			{
				g_clients[i].m_movetype = CHASE_MODE;
				if (g_clients[i].target == -1)
				{
					g_clients[i].target = id;

					attack_start_npc(i);
					int player_id = id;
					lua_State *L = g_clients[i].L;
					lua_getglobal(L, "player_notify");
					lua_pushnumber(L, player_id);
					lua_pushnumber(L, g_clients[id].x);
					lua_pushnumber(L, g_clients[id].y);
					lua_pcall(L, 3, 0, 0);
				}
			}
		}

		g_clients[id].v_l.lock();
		g_clients[id].v_list.insert(i);
		g_clients[id].v_l.unlock();



		send_put_object_packet(id, i);
	}
	// 신규 플레이어의 접속을 다른 클라이언트에게 알려준다.
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == g_clients[i].m_connected) continue;
		if (i == id) continue;
		if (false == Can_See(i, id)) continue;
		g_clients[i].v_l.lock();
		g_clients[i].v_list.insert(id);
		g_clients[i].v_l.unlock();

		send_put_object_packet(i, id);
	}
	add_timer(id, job_healing, std::chrono::high_resolution_clock::now() + 5s);
}




void ProcessPacket(int id, unsigned char *packet)
{
	int x = g_clients[id].x;
	int y = g_clients[id].y;

	switch (packet[1]) {
	case CS_ATTACK:
	{

		attack_cooltime(id);

		break;
	}

	case CS_MOVE:
	{

		if (!g_clients[id].m_alive) break;
		if (g_clients[id].m_moving == false)
		{
			move_cooltime(id);

			switch (packet[2])
			{
			case UP:

				if (y > 0 && g_map_buffer[y - 1][x] != '5')
					y--;

				if (y <= 0)
					y = 0;

				break;
			case DOWN:
				if (y < BOARD_HEIGHT - 1 && g_map_buffer[y + 1][x] != '5')
					y++;
				if (y >= BOARD_HEIGHT)
					y = BOARD_HEIGHT - 1;

				break;
			case LEFT:
				if (x > 0 && g_map_buffer[y][x - 1] != '5')
					x--;
				if (x <= 0)
					x = 0;
				break;
			case RIGHT:
				if (x < BOARD_WIDTH - 1 && g_map_buffer[y][x + 1] != '5')
					x++;
				if (x >= BOARD_WIDTH)
					x = BOARD_WIDTH - 1;
				break;
			}
			g_clients[id].x = x;
			g_clients[id].y = y;
		}
		sc_packet_pos pos_packet;
		pos_packet.ID = id;
		pos_packet.size = sizeof(sc_packet_pos);
		pos_packet.type = SC_POSITION_INFO;
		pos_packet.X_POS = x;
		pos_packet.Y_POS = y;

		// 시야에 있는 플레이어들 에게만 패킷을 보낸다.
		// 새로 시야에 들어오는 플레이어가 누구인지
		// 시야에서 사라진 플레이어가 누구인지
		unordered_set <int> old_vl, new_vl;
		g_clients[id].v_l.lock();
		old_vl = g_clients[id].v_list;
		g_clients[id].v_l.unlock();
		for (int i = 0; i < MAX_NPC; ++i) {
			if (false == g_clients[i].m_connected) continue;
			if (i == id) continue;
			if (true == Can_See(id, i)) new_vl.insert(i);
		}

		send_packet(id, &pos_packet);

		for (auto pl : new_vl) {
			if (0 == old_vl.count(pl)) {
				send_put_object_packet(id, pl);
				g_clients[id].v_l.lock();
				g_clients[id].v_list.insert(pl);
				g_clients[id].v_l.unlock();
				if (false == is_player(pl)) {
					wake_up_npc(pl);
					if (g_clients[pl].npc_type == SLAYER_MOB)
					{
						g_clients[pl].m_movetype = CHASE_MODE;
						if (g_clients[pl].target == -1)
							g_clients[pl].target = id;
					}
					if (g_clients[pl].m_movetype == CHASE_MODE)
					{
						attack_start_npc(pl);
						int player_id = id;
						lua_State *L = g_clients[pl].L;
						lua_getglobal(L, "player_notify");
						lua_pushnumber(L, player_id);
						lua_pushnumber(L, g_clients[id].x);
						lua_pushnumber(L, g_clients[id].y);
						lua_pcall(L, 3, 0, 0);
					}
				}
			}

			if (false == is_player(pl)) continue;
			g_clients[pl].v_l.lock();
			if (0 != g_clients[pl].v_list.count(id)) {
				g_clients[pl].v_l.unlock();
				send_packet(pl, &pos_packet);
			}
			else {
				g_clients[pl].v_list.insert(id);
				g_clients[pl].v_l.unlock();
				send_put_object_packet(pl, id);
			}
		}

		for (auto pl : old_vl) {
			if (0 == new_vl.count(pl)) {
				// 시야에서 사라진 플레이어
				g_clients[id].v_l.lock();
				g_clients[id].v_list.erase(pl);
				g_clients[id].v_l.unlock();
				send_remove_object_packet(id, pl);
				if (g_clients[pl].target == id)
					g_clients[pl].target = -1;
				if (false == is_player(pl)) continue;
				g_clients[pl].v_l.lock();
				if (0 != g_clients[pl].v_list.count(id)) {
					g_clients[pl].v_list.erase(id);
					g_clients[pl].v_l.unlock();
					send_remove_object_packet(pl, id);
				}
				else {
					g_clients[pl].v_l.unlock();
				}
			}

		}
		break;
	}


	case CS_LOGIN:
	{
		cs_packet_request_login* p = reinterpret_cast<cs_packet_request_login*>(packet);
		//wprintf(L"%s",p->name);
		Send_login_packet(p->name, id);

		break;
	}

	default: cout << "Invalid Packet Type Error!\n";
		while (true);
	}

}
void ClientDIsconnect(int key)
{
	// 클라이언트가 종료해서 접속 끊어 졌다.


	sc_packet_remove_player packet;
	packet.id = key;
	packet.size = sizeof(sc_packet_remove_player);
	packet.type = SC_REMOVE_OBJECT;

	g_clients[key].v_l.lock();
	unordered_set<int> vl = g_clients[key].v_list;
	g_clients[key].v_l.unlock();
	for (auto pl : vl) {
		if (pl == key)continue;
		if (!is_player(pl))continue;
		g_clients[pl].v_l.lock();
		g_clients[pl].v_list.erase(key);
		g_clients[pl].v_l.unlock();
		send_packet(pl, &packet);
	}

	g_clients[key].m_connected = false;
	closesocket(g_clients[key].m_socket);
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;
	SQLWCHAR szName[NAME_LEN];
	SQLINTEGER ch_x, ch_y;
	SQLLEN cbName = 0, cbx = 0, cby = 0; //cb -콜백

	wchar_t query[200] = { 0 };
	swprintf_s(query, L"UPDATE [2013184019유재용_숙제7번].[dbo].[C_Table1] SET c_xpos = %d , c_ypos = %d , c_level = %d, c_power = %d, c_weapon = '%s', c_exp = %d , c_hp = %d WHERE c_name = '%s' ",
		g_clients[key].x, g_clients[key].y, g_clients[key].level, g_clients[key].power, g_clients[key].weapon, g_clients[key].exp, g_clients[key].hp, g_clients[key].m_name);
	SQLLEN count = 0;
	wchar_t init[10] = { 0 };
	wcscpy(g_clients[key].m_name, init);
	wcscpy(g_clients[key].weapon, init);
	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
	{
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
		{
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
			{
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"수금 2013184019 유재용 숙제7번", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
				{
					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
					retcode = SQLExecDirect(hstmt, (SQLWCHAR *)query, SQL_NTS);

					SQLRowCount(hstmt, &count);
					if (count == 0)
					{
						printf("DB : THIS ID ISN'T MINE. DON'T SAVE DATA.\n");
					}
					else
						wprintf(L"%s\n", query);

					//if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
					//{
					//
					//	// Bind columns 1, 2, and 3  
					//	retcode = SQLBindCol(hstmt, 1, SQL_C_CHAR, szName, NAME_LEN, &cbName);
					//	retcode = SQLBindCol(hstmt, 2/*필드*/, SQL_C_LONG/*자료형(int)*/, &ch_x, 100/*필드의 최대 길이*/, &cbx/*콜백*/);
					//	retcode = SQLBindCol(hstmt, 3, SQL_C_LONG, &ch_y, 100, &cby);
					//
					//	// Fetch and print each row of data. On an error, display a message and exit.  
					//	for (int i = 0; ; i++)
					//	{
					//		retcode = SQLFetch(hstmt);//첫번째 출력결과를 꺼내서 바인딩된 변수에 집어넣는다(&ch_id)
					//		if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
					//			HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
					//		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
					//			wprintf(L"%d: %S %d %d\n", i + 1, szName, ch_x, ch_y);
					//		else
					//			break;
					//	}

					//}
					//else
					//{
					//	printf("디비저장 실패 \n");
					//
					//}

					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
					}

					SQLDisconnect(hdbc);
				}
			}

			SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
		}
	}
	SQLFreeHandle(SQL_HANDLE_ENV, henv);
	g_clients[key].x = 0;
	g_clients[key].y = 0;
}
void attack_npc(int id)
{
	int x = g_clients[id].x;
	int y = g_clients[id].y;
	for (int i = 0; i < MAX_USER; ++i)
	{

		if (g_clients[i].m_alive == false) continue;
		if (g_clients[i].m_connected == false) continue;
		int dist = 0;
		dist += (g_clients[id].x - g_clients[i].x) * (g_clients[id].x - g_clients[i].x);
		dist += (g_clients[id].y - g_clients[i].y) * (g_clients[id].y - g_clients[i].y);
		//엔피씨 공격패킷 제작
		if (dist <= 2)
		{
			
			g_clients[id].m_battle = true;
			g_clients[id].m_movetype = FIXING_MODE; //공격할땐 고정
			sc_packet_npc_attack packet;
			packet.size = sizeof(sc_packet_npc_attack);
			packet.type = SC_NPC_ATTACK;
			packet.damage = g_clients[id].power;
			g_clients[i].hp -= g_clients[id].power;
			send_packet(i, &packet);
			if (g_clients[i].hp <= 0)
			{
				g_clients[i].hp = 0;
				OverEx *ex = new OverEx;
				ex->m_todo = OP_PLAYER_DIED;
				ex->m_other_id = id;
				PostQueuedCompletionStatus(g_hIOCP, 1, i, &
					ex->m_wsa_over);
			}
		}
		else {

			g_clients[id].m_battle = false;
			g_clients[id].m_attack = false;
		}
	}
}
void move_npc(int id)
{

	if (g_clients[id].m_battle == true)
		return;
	if (g_clients[id].m_alive == false)
		return;
	int x = g_clients[id].x;
	int y = g_clients[id].y;
	int target = g_clients[id].target;
	switch (rand() % 4) {
	case 0:
	{
		if (x > 0 && g_map_buffer[y][x - 1] != '5'
			&&g_map_buffer[y][x - 1] != '0')
		{
			if (g_clients[id].m_movetype == CHASE_MODE)
			{

				if (g_clients[target].x < g_clients[id].x)
				{
					x--;
				}
				else {
					x++;

					attack_start_npc(id);
					
				}

			}
			else if (g_clients[id].m_movetype == LOAMING_MODE)
				x--;
			else {}
		}
		break;
	}
	case 1:
		if (x < BOARD_WIDTH - 1 && g_map_buffer[y][x + 1] != '5'
			&& g_map_buffer[y][x + 1] != '0')
		{
			if (g_clients[id].m_movetype == CHASE_MODE)
			{

				if (g_clients[target].x > g_clients[id].x)

					x++;

				else x--;

				attack_start_npc(id);


			}
			else if (g_clients[id].m_movetype == LOAMING_MODE)
				x++;
			else {}
		}
		break;
	case 2:
		if (y > 0 && g_map_buffer[y - 1][x] != '5'
			&& g_map_buffer[y - 1][x] != '0')
		{
			if (g_clients[id].m_movetype == CHASE_MODE)
			{

				if (g_clients[target].y < g_clients[id].y)
				{
					y--;
				}
				else y++;

				attack_start_npc(id);
			}

			else if (g_clients[id].m_movetype == LOAMING_MODE)
				y--;
			else {}
		}
		break;
	case 3:
		if (y < BOARD_HEIGHT - 1 && g_map_buffer[y + 1][x] != '5'
			&& g_map_buffer[y + 1][x] != '0')
		{
			if (g_clients[id].m_movetype == CHASE_MODE)
			{

				if (g_clients[target].y > g_clients[id].y)
				{
					y++;
				}
				else y--;

				attack_start_npc(id);

			}
			else if (g_clients[id].m_movetype == LOAMING_MODE)
				y++;
			else {}
		}
		break;
	}

	g_clients[id].x = x;
	g_clients[id].y = y;

	sc_packet_pos packet;
	packet.ID = id;
	packet.size = sizeof(packet);
	packet.type = SC_POSITION_INFO;
	packet.X_POS = x;
	packet.Y_POS = y;

	for (int pl = 0; pl < MAX_USER; ++pl)
	{
		if (false == g_clients[pl].m_connected) continue;
		if (true == Can_See(pl, id)) {
			g_clients[pl].v_l.lock();
			if (0 == g_clients[pl].v_list.count(id)) {
				g_clients[pl].v_list.insert(id);
				g_clients[pl].v_l.unlock();
				send_put_object_packet(pl, id);
			}
			else {
				g_clients[pl].v_l.unlock();
				send_packet(pl, &packet);
			}
		}
		else { // 시야에서 사라진 경우
			g_clients[pl].v_l.lock();
			if (0 == g_clients[pl].v_list.count(id)) {
				g_clients[pl].v_l.unlock();
			}
			else {
				g_clients[pl].v_list.erase(id);
				g_clients[pl].v_l.unlock();
				send_remove_object_packet(pl, id);
			}

		}
	}
}
void send_respawn_packet(int id)
{
	if (is_player(id))
	{
		g_clients[id].hp = g_clients[id].fhp;
		g_clients[id].x = 5;
		g_clients[id].y = 5;
		send_put_object_packet(id, id);

		for (int i = 0; i < MAX_NPC; ++i)
		{
			if (i == id)continue;
			if (Can_See(i, id))
			{
				g_clients[id].v_l.lock();
				g_clients[id].v_list.insert(i);
				g_clients[id].v_l.unlock();
				send_put_object_packet(id, i);
			}
		}
		for (int i = 0; i < MAX_USER; ++i)
		{
			if (i == id)continue;
			if (Can_See(i, id))
			{
				g_clients[i].v_l.lock();
				g_clients[i].v_list.insert(id);
				g_clients[i].v_l.unlock();
				send_put_object_packet(i, id);
			}
		}

	}
	else
	{
		g_clients[id].hp = g_clients[id].fhp;
		g_clients[id].m_attack = false;
		g_clients[id].m_moving = false;
		g_clients[id].m_battle = false;
		for (int i = 0; i < MAX_USER; ++i)
		{
			if (i == id)continue;
			if (g_clients[i].m_connected == false)continue;
			if (Can_See(i, id))
			{
				g_clients[i].v_l.lock();
				g_clients[i].v_list.insert(id);
				g_clients[i].v_l.unlock();
				send_put_object_packet(i, id);
			}
		}
	}
	g_clients[id].m_alive = true;

}
void worker_thread()
{
	while (true) {

		DWORD iosize;
		unsigned long long key;
		OverEx *over;

		int ret = GetQueuedCompletionStatus(g_hIOCP, &iosize, &key,
			reinterpret_cast<WSAOVERLAPPED **>(&over), INFINITE);

		if (0 == ret) {
			int err_no = GetLastError();
			if (64 == err_no)
				ClientDIsconnect(key);
			else error_display("GQCS : ", err_no);
			continue;
		}

		if (0 == iosize) {
			ClientDIsconnect(key);
			continue;
		}

		if (OP_RECV == over->m_todo) {
			// 패킷 재조립
			int rest_data = iosize;
			unsigned char *ptr = over->IOCPbuf;
			int packet_size = 0;
			if (0 != g_clients[key].m_prev_size)
				packet_size = g_clients[key].m_packet_buf[0];

			while (rest_data > 0) {
				if (0 == packet_size) packet_size = ptr[0];
				int need_size = packet_size - g_clients[key].m_prev_size;
				if (rest_data >= need_size) {
					// 패킷을 하나 조립할 수 있음
					memcpy(g_clients[key].m_packet_buf
						+ g_clients[key].m_prev_size,
						ptr, need_size);
					ProcessPacket(key, g_clients[key].m_packet_buf);
					packet_size = 0;
					g_clients[key].m_prev_size = 0;
					ptr += need_size;
					rest_data -= need_size;
				}
				else {
					// 패킷을 완성할 수 없으니 후일 기약하고 남은 데이터 저장
					memcpy(g_clients[key].m_packet_buf
						+ g_clients[key].m_prev_size,
						ptr, rest_data);
					ptr += rest_data;
					g_clients[key].m_prev_size += rest_data;
					rest_data = 0;
				}
			}
			OverlappedRecv(key);
		}
		else if (OP_SEND == over->m_todo) {
			delete over;
		}
		else if (OP_MOVE == over->m_todo)
		{
			move_npc(key);
			bool player_exist = false;
			for (int i = 0; i < MAX_USER; ++i) {
				if (true == Can_See(i, key)) {
					player_exist = true;
					break;
				}
			}
			if (player_exist)
				add_timer(key, job_move,
					high_resolution_clock::now() + 1s);
			else
				g_clients[key].m_moving = false;
			delete over;
		}
		else if (OP_ATTACK_COOLTIME == over->m_todo)
		{
			g_clients[key].m_attack = false;
			delete over;
		}
		else if (OP_PLAYER_DIED == over->m_todo)
		{
			g_clients[key].exp = g_clients[key].exp / 2;
			g_clients[key].m_alive = false;
			sc_packet_player_died packet;
			packet.id = key;
			packet.exp = g_clients[key].exp;
			packet.other_id = over->m_other_id;
			packet.size = sizeof(sc_packet_player_died);
			packet.type = SC_PLAYER_DIED;
			for (int i = 0; i < MAX_USER; ++i)
				if (Can_See(i, key))
					send_packet(i, &packet);
			add_timer(key, job_respawn, std::chrono::high_resolution_clock().now() + 10s);
		}
		else if (OP_NPC_ATTACK_COOLTIME == over->m_todo)
		{

			if (g_clients[key].m_alive == true)
			{

				bool player_exist = false;
				for (int i = 0; i < MAX_USER; ++i) {
					if (true == Can_See(i, key))
					{
						int dist = 0;
						dist += (g_clients[key].x - g_clients[i].x) * (g_clients[key].x - g_clients[i].x);
						dist += (g_clients[key].y - g_clients[i].y) * (g_clients[key].y - g_clients[i].y);
						if (dist <= 2)
						{
							attack_npc(key);
							
							player_exist = true;
							break;
						}
					}
				}
				if (player_exist)
					add_timer(key, job_npc_attack,
						high_resolution_clock::now() + 2s);
				else
					g_clients[key].m_attack = false;
			}
			else
			{
			}
			delete over;


		}
		else if (OP_MOVE_COOLTIME == over->m_todo)
		{
			g_clients[key].m_moving = false;
			delete over;
		}
		else if (OP_NPC_DIED == over->m_todo)
		{
			g_clients[key].m_alive = false;
			send_npc_died_packet(key, over->m_other_id);
			add_timer(key, job_respawn, std::chrono::high_resolution_clock().now() + 10s);
			delete over;
		}
		else if (OP_RESPAWN == over->m_todo)
		{
			send_respawn_packet(key);
			delete over;
		}
		else if (OP_LEVEL_UP == over->m_todo)
		{
			send_levelup_packet(key);
			delete over;
		}
		else if (OP_HEALING == over->m_todo)
		{
			
			if (g_clients[key].m_alive)
			{
				if (g_clients[key].fhp > g_clients[key].hp)
				{

					g_clients[key].hp += 5;
					if (g_clients[key].fhp < g_clients[key].hp)
						g_clients[key].hp = g_clients[key].fhp;

					sc_healing_packet packet;
					packet.size = sizeof(sc_healing_packet);
					packet.type = SC_HEALING;
					packet.hp = g_clients[key].hp;
					send_packet(key, &packet);
				}
			}
			if (g_clients[key].m_connected)
				add_timer(key, job_healing, high_resolution_clock::now() + 5s);
			delete over;
		}
		else {
			cout << "Unknown Worker Thread Job!\n";
			while (true);
		}
	}
}

void accept_thread()
{
	// 1. 소켓생성  
	SOCKET listenSocket = WSASocketW(AF_INET, SOCK_STREAM, 0,
		NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listenSocket == INVALID_SOCKET)
	{
		printf("Error - Invalid socket\n");
		exit(-1);
	}

	// 서버정보 객체설정
	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = PF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	// 2. 소켓설정
	if (::bind(listenSocket, (struct sockaddr*)&serverAddr,
		sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		printf("Error - Fail bind\n");
		// 6. 소켓종료
		closesocket(listenSocket);
		// Winsock End
		WSACleanup();
		exit(-1);
	}

	// 3. 수신대기열생성
	if (listen(listenSocket, 5) == SOCKET_ERROR)
	{
		printf("Error - Fail listen\n");
		// 6. 소켓종료
		closesocket(listenSocket);
		// Winsock End
		WSACleanup();
		exit(-1);
	}

	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(SOCKADDR_IN);
	memset(&clientAddr, 0, addrLen);
	SOCKET clientSocket;

	while (1)
	{
		clientSocket = WSAAccept(listenSocket,
			(struct sockaddr *)&clientAddr, &addrLen, NULL, NULL);
		if (clientSocket == INVALID_SOCKET)
		{
			printf("Error - Accept Failure\n");
			exit(-1);
		}
		int id = -1;
		for (int i = 0; i < MAX_USER; ++i)
			if (false == g_clients[i].m_connected) {
				id = i;
				break;
			}

		if (-1 == id) {
			cout << "User FULL!! \n";
			closesocket(clientSocket);
			continue;
		}

		g_clients[id].id = id;
		g_clients[id].m_socket = clientSocket;
		g_clients[id].m_over_ex.m_todo = OP_RECV;
		g_clients[id].m_over_ex.m_wsa_buf.buf =
			(CHAR*)g_clients[id].m_over_ex.IOCPbuf;
		g_clients[id].m_over_ex.m_wsa_buf.len =
			sizeof(g_clients[id].m_over_ex.IOCPbuf);
		g_clients[id].m_prev_size = 0;

		g_clients[id].m_connected = true;
		g_clients[id].v_list.clear();

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_clients[id].m_socket),
			g_hIOCP, id, 0);


		//g_clients[id].x = 5;
		//
		//g_clients[id].y = 5;


		OverlappedRecv(id);
	}
}

void ai_thread()
{
	while (true) {
		auto start_t = high_resolution_clock::now();
		for (int i = NPC_START; i < MAX_NPC; ++i)
			move_npc(i);
		auto end_t = high_resolution_clock::now();
		auto ai_time = end_t - start_t;
		cout << "AI Time = " << duration_cast<milliseconds>(ai_time).count() << "ms\n";
	}
}

void timer_thread()
{
	while (true) {
		this_thread::sleep_for(1ms);
		while (true) {
			tq_l.lock();
			if (true == timer_queue.empty()) {
				tq_l.unlock();  break;
			}
			time_event ev = timer_queue.top();
			if (ev.wake_time <= high_resolution_clock::now()) {
				timer_queue.pop();
				tq_l.unlock();

				OverEx *ex_over = new OverEx;
				if (ev.job == job_attack_cooltime)
					ex_over->m_todo = OP_ATTACK_COOLTIME;
				else if (ev.job == job_move)

					ex_over->m_todo = OP_MOVE;

				else if (ev.job == job_move_player)

					ex_over->m_todo = OP_MOVE_COOLTIME;

				else if (ev.job == job_npc_attack)
				{
					ex_over->m_todo = OP_NPC_ATTACK_COOLTIME;
				}
				else if (ev.job == job_respawn)
				{
					ex_over->m_todo = OP_RESPAWN;
				}
				else if (ev.job == job_healing)
				{
					ex_over->m_todo = OP_HEALING;
				}
				PostQueuedCompletionStatus(g_hIOCP, 1, ev.id, &
					ex_over->m_wsa_over);
			}
			else {
				tq_l.unlock();  break;
			}
		}
	}
}

int main()
{
	int index_i = 0;
	int index_j = 0;
	char data = 0;
	ifstream File("Map.txt");

	while (!File.eof())
	{
		File >> data;
		g_map_buffer[index_i][index_j] = data;

		index_j++;
		if (index_j == 300)
		{
			index_i++;
			index_j = 0;
		}
	}

	const int NUM_THREADS = 4;
	vector <thread> worker_threads;
	srand(time(NULL));
	wcout.imbue(locale("korean"));
	InitializeNetwork();
	InitializeNPC();
	for (auto i = 0; i < NUM_THREADS; ++i)
		worker_threads.push_back(thread{ worker_thread });
	thread accept_th{ accept_thread };

	//	thread ai_th{ ai_thread };
	thread timer_th{ timer_thread };



	for (auto &wth : worker_threads) wth.join();
	accept_th.join();

	EndNetwork();
}