// PROG14_1_16b.CPP - DirectInput keyboard demo
// INCLUDES ///////////////////////////////////////////////
#pragma comment(linker,"/entry:WinMainCRTStartup /subsystem:console")

#define WIN32_LEAN_AND_MEAN  
#define INITGUID
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <WinSock2.h>
#include <windows.h>   // include important windows stuff
//#include <imm.h>
#include <windowsx.h>
#include <stdio.h>
#include <wchar.h>
#include <fstream>
#include <d3d9.h>     // directX includes

#include "d3dx9tex.h"     // directX includes
#include "gpdumb1.h"
#include "protocol.h"
using namespace std;
#pragma comment (lib, "ws2_32.lib")
#pragma comment (lib, "d3dx9.lib")
#pragma comment (lib, "d3d9.lib")
// DEFINES ////////////////////////////////////////////////

#define MAX(a,b)	((a)>(b))?(a):(b)
#define	MIN(a,b)	((a)<(b))?(a):(b)

// defines for windows 
#define WINDOW_CLASS_NAME L"WINXCLASS"  // class name

#define WINDOW_WIDTH    910   // size of window
#define WINDOW_HEIGHT   960

#define	BUF_SIZE				1024
#define	WM_SOCKET				WM_USER + 1

// PROTOTYPES /////////////////////////////////////////////

// game console
int Game_Init(void *parms = NULL);
int Game_Shutdown(void *parms = NULL);
int Game_Main(void *parms = NULL);

// GLOBALS ////////////////////////////////////////////////

HWND main_window_handle = NULL; // save the window handle
HINSTANCE main_instance = NULL; // save the instance
char buffer[80];                // used to print text

// demo globals
BOB			player;				// 플레이어 Unit
BOB			npc[20000];      // NPC Unit

BOB         skelaton[MAX_USER];     // the other player skelaton

BITMAP_IMAGE reactor;      // the background   

BITMAP_IMAGE stone_tile;
BITMAP_IMAGE snow_tile;

BITMAP_IMAGE sand_tile;
BITMAP_IMAGE crystal_tile;
BITMAP_IMAGE grass_tile;
#define TILE_WIDTH 45

#define UNIT_TEXTURE  0

SOCKET g_mysocket;
WSABUF	send_wsabuf;
char 	send_buffer[BUF_SIZE];
WSABUF	recv_wsabuf;
char	recv_buffer[BUF_SIZE];
char	packet_buffer[BUF_SIZE];
DWORD		in_packet_size = 0;
int		saved_packet_size = 0;
int		g_myid;
wchar_t	g_myname[10];
wchar_t chatting[255];
bool enterflag = false;
int		g_my_level = 0;
int		g_my_exp = 0;
int		g_my_power = 0;
int		g_my_hp_full = 0;
int		g_my_hp_current = 0;
int		g_left_x = 0;
int     g_top_y = 0;

char g_map_buffer[BOARD_WIDTH][BOARD_HEIGHT];
wchar_t system_message[300] = { 0 };
wchar_t UI[300];
int R = 0;
int G = 0;
int B = 0;
bool color_flag = true;
// FUNCTIONS //////////////////////////////////////////////

void Login_Request()
{
	cs_packet_request_login *my_packet = reinterpret_cast<cs_packet_request_login *>(send_buffer);
	wchar_t input_id[10] = { 0 };
	printf("ID INPUT : ");
	wscanf_s(L"%s", my_packet->name, sizeof(wchar_t[10]));
	my_packet->size = sizeof(cs_packet_request_login);
	my_packet->type = CS_LOGIN;
	send_wsabuf.len = sizeof(cs_packet_request_login);
	DWORD iobyte;
	int ret = WSASend(g_mysocket, &send_wsabuf, 1, &iobyte, 0, NULL, NULL);
}
void ProcessPacket(char *ptr)
{

	static bool first_time = true;
	switch (ptr[1])
	{
	case SC_STAT_CHANGE:
	{
		sc_stat_change *my_packet = reinterpret_cast<sc_stat_change *>(ptr);
		if (my_packet->LEVEL > g_my_level)
			wsprintf(system_message, L"레벨업 !! %d 레벨이 되었습니다.", my_packet->LEVEL);

		g_my_level = my_packet->LEVEL;
		g_my_exp = my_packet->EXP;
		g_my_hp_full = my_packet->HP;

		g_my_power = 10 + (g_my_level * 5);

		break;
	}
	case SC_HEALING:
	{
		sc_healing_packet *my_packet = reinterpret_cast<sc_healing_packet *>(ptr);
		g_my_hp_current = my_packet->hp;
		break;
	}
	case SC_PACKET_NPC_DIED:
	{
		sc_npc_died_packet *my_packet = reinterpret_cast<sc_npc_died_packet *>(ptr);

		if (my_packet->slayer_id == g_myid)
		{
			
			wsprintf(system_message, L"%d 를 처치했습니다. 경험치 : %d", my_packet->id, my_packet->exp);
			g_my_exp += my_packet->exp;
		}
		npc[my_packet->id].attr &= ~BOB_ATTR_VISIBLE;
		break;

	}
	case SC_NPC_ATTACK:
	{
		sc_packet_npc_attack *my_packet = reinterpret_cast<sc_packet_npc_attack*>(ptr);
		wsprintf(system_message, L"%d의 데미지를 받았습니다", my_packet->damage);
		g_my_hp_current -= my_packet->damage;
		break;
	}
	case SC_ATTACK:
	{
		sc_attack_packet *my_packet = reinterpret_cast<sc_attack_packet *>(ptr);
		wsprintf(system_message, L"%d 캐릭터가 %d NPC를 공격했습니다. 데미지 : %d", my_packet->id, my_packet->other_id, my_packet->damage);
		break;
	}
	case SC_LOGIN_OK:
	{
		sc_packet_login_success *my_packet = reinterpret_cast<sc_packet_login_success *>(ptr);
		int id = my_packet->id;
		if (first_time) {
			first_time = false;
			g_myid = id;
		}
		if (id == g_myid)
		{
			g_left_x = my_packet->X_POS - 10;
			g_top_y = my_packet->Y_POS - 10;
			player.x = my_packet->X_POS;
			player.y = my_packet->Y_POS;
			//
			g_my_hp_current = my_packet->HP;
			//
			g_my_exp = my_packet->EXP;
			g_my_level = my_packet->LEVEL;
			player.attr |= BOB_ATTR_VISIBLE;
		}
		break;
	}
	case SC_LOGIN_OK_EX :
	{
		sc_packet_login_success_ex *my_packet = reinterpret_cast<sc_packet_login_success_ex *>(ptr);
		g_my_power = my_packet->power;
		wcscpy_s(g_myname, my_packet->name);
		break;
	}
	case SC_ADD_OBJECT:
	{
		sc_packet_put_player *my_packet = reinterpret_cast<sc_packet_put_player *>(ptr);
		int id = my_packet->ID;

		if (id == g_myid) {

			player.attr |= BOB_ATTR_VISIBLE;
		}
		else if (id < MAX_USER)
		{

			skelaton[id].attr |= BOB_ATTR_VISIBLE;
		}
		else
		{
			npc[id].attr |= BOB_ATTR_VISIBLE;
		}

		break;
	}
	case SC_LOGIN_FAIL:
	{
		printf("이미 접속중인 아이디입니다. 다시 접속해 주세요.\n");
		system("pause");
		exit(1);

		break;

	}

	case SC_CHAT:
	{
		sc_chat *my_packet = reinterpret_cast<sc_chat *>(ptr);
		int other_id = my_packet->CHAT_ID;
		if (other_id == g_myid) {
			wcscpy_s(player.message, my_packet->message);
			player.message_time = GetTickCount();
		}
		else if (other_id < NPC_START) {
			wcscpy_s(skelaton[other_id].message, my_packet->message);
			skelaton[other_id].message_time = GetTickCount();
		}
		else {
			wcscpy_s(npc[other_id].message, my_packet->message);
			npc[other_id].message_time = GetTickCount();
		}
		break;

	
	}
	case SC_POSITION_INFO:
	{

		sc_packet_pos *my_packet = reinterpret_cast<sc_packet_pos *>(ptr);
		int other_id = my_packet->ID;
		if (other_id == g_myid) {
			g_left_x = my_packet->X_POS - 10;
			g_top_y = my_packet->Y_POS - 10;
			player.x = my_packet->X_POS;
			player.y = my_packet->Y_POS;

			if (my_packet->X_POS == 5 && my_packet->Y_POS == 5 && g_my_hp_current == 0)
				g_my_hp_current = g_my_hp_full;
		}
		else if (other_id < MAX_USER)
		{
			skelaton[other_id].x = my_packet->X_POS;
			skelaton[other_id].y = my_packet->Y_POS;

		}
		else
		{
			npc[other_id].x = my_packet->X_POS;
			npc[other_id].y = my_packet->Y_POS;

		}
		break;
	}
	case SC_PLAYER_DIED:
	{
		sc_packet_player_died *my_packet = reinterpret_cast<sc_packet_player_died *>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			g_my_hp_current = 0;
			player.attr &= ~BOB_ATTR_VISIBLE;
			g_my_exp = my_packet->exp;
			wsprintf(system_message, L"%d에 의해 캐릭터가 죽었습니다. %d의 경험치를 잃었습니다.", my_packet->other_id, my_packet->exp);

		}
		else if (other_id < MAX_USER)

			skelaton[other_id].attr &= ~BOB_ATTR_VISIBLE;
		else
			npc[other_id].attr &= ~BOB_ATTR_VISIBLE;

		break;

	}
\
	case SC_REMOVE_OBJECT:
	{
		sc_packet_remove_player *my_packet = reinterpret_cast<sc_packet_remove_player *>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			player.attr &= ~BOB_ATTR_VISIBLE;
		}
		else if (other_id < MAX_USER)

			skelaton[other_id].attr &= ~BOB_ATTR_VISIBLE;
		else
			npc[other_id].attr &= ~BOB_ATTR_VISIBLE;

		break;



	}
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);
		break;
	}
}

void ReadPacket(SOCKET sock)
{
	DWORD iobyte, ioflag = 0;

	int ret = WSARecv(sock, &recv_wsabuf, 1, &iobyte, &ioflag, NULL, NULL);
	if (ret) {
		int err_code = WSAGetLastError();
		printf("Recv Error [%d]\n", err_code);
	}

	BYTE *ptr = reinterpret_cast<BYTE *>(recv_buffer);

	while (0 != iobyte) {
		if (0 == in_packet_size) in_packet_size = ptr[0];
		if (iobyte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			iobyte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, iobyte);
			saved_packet_size += iobyte;
			iobyte = 0;
		}
	}
}

void clienterror()
{
	exit(-1);
}

LRESULT CALLBACK WindowProc(HWND hwnd,
	UINT msg,
	WPARAM wparam,
	LPARAM lparam)
{
	// this is the main message handler of the system
	PAINTSTRUCT	ps;		   // used in WM_PAINT
	HDC			hdc;	   // handle to a device context
	//HIMC hImc = ::ImmGetContext(hwnd);

	// what is the message 
	switch (msg)
	{
	case WM_IME_COMPOSITION:
	{
		
		//int len = ImmGetCompositionString(hImc, GCS_COMPSTR, NULL, 0);

		//if (len > 0)
			//wcscpy(chatting, (wchar_t*)wparam);
		break;
	}
	case WM_CHAR:
	{
	
	
		break;
	}
	case WM_KEYUP:
	{
		if (wparam == VK_RETURN)
		{
			//if (enterflag == true)
			//{
			//	enterflag = false;
			//	DestroyCaret();
			//
			//}
			//else
			//{
				enterflag = true;
				CreateCaret(hwnd, NULL, 7, 32);
				SetCaretPos(10, 700);
				ShowCaret(hwnd);
			//
			//	
			//}
			
		}
		if (wparam == VK_CONTROL)
		{
			cs_attack_packet *my_packet = reinterpret_cast<cs_attack_packet *>(send_buffer);
			my_packet->size = sizeof(cs_attack_packet);
			send_wsabuf.len = sizeof(cs_attack_packet);
			my_packet->type = CS_ATTACK;
			DWORD iobyte;
			int ret = WSASend(g_mysocket, &send_wsabuf, 1, &iobyte, 0, NULL, NULL);
			
		}
		break;
	}
	case WM_KEYDOWN: {


		int x = 0, y = 0;
		if (wparam == VK_RIGHT)	x += 1;
		if (wparam == VK_LEFT)	x -= 1;
		if (wparam == VK_UP)	y -= 1;
		if (wparam == VK_DOWN)	y += 1;
		cs_packet_move *my_packet = reinterpret_cast<cs_packet_move*>(send_buffer);
		my_packet->size = sizeof(my_packet);
		send_wsabuf.len = sizeof(my_packet);
		my_packet->type = CS_MOVE;
		DWORD iobyte;
		if (0 != x) {
			if (1 == x) my_packet->DIR = RIGHT;
			else my_packet->DIR = LEFT;
			int ret = WSASend(g_mysocket, &send_wsabuf, 1, &iobyte, 0, NULL, NULL);

			if (ret) {
				int error_code = WSAGetLastError();
				printf("Error while sending packet [%d]", error_code);
			}
		}
		if (0 != y) {
			if (1 == y) my_packet->DIR = DOWN;
			else my_packet->DIR = UP;
			WSASend(g_mysocket, &send_wsabuf, 1, &iobyte, 0, NULL, NULL);
		}


	}
					 break;
	case WM_CREATE:
	{
		// do initialization stuff here
		return(0);
	} break;

	case WM_PAINT:
	{
		// start painting
		hdc = BeginPaint(hwnd, &ps);

		// end painting
		EndPaint(hwnd, &ps);
		return(0);
	} break;

	case WM_DESTROY:
	{
		// kill the application			
		PostQuitMessage(0);
		return(0);
	} break;
	case WM_SOCKET:
	{
		if (WSAGETSELECTERROR(lparam)) {
			closesocket((SOCKET)wparam);
			clienterror();
			break;
		}
		switch (WSAGETSELECTEVENT(lparam)) {
		case FD_READ:
			ReadPacket((SOCKET)wparam);
			break;
		case FD_CLOSE:
			closesocket((SOCKET)wparam);
			clienterror();
			break;
		}
	}

	default:break;

	} // end switch

// process any messages that we didn't take care of 
	return (DefWindowProc(hwnd, msg, wparam, lparam));

} // end WinProc

// WINMAIN ////////////////////////////////////////////////

int WINAPI WinMain(HINSTANCE hinstance,
	HINSTANCE hprevinstance,
	LPSTR lpcmdline,
	int ncmdshow)
{
	// this is the winmain function

	WNDCLASS winclass;	// this will hold the class we create
	HWND	 hwnd;		// generic window handle
	MSG		 msg;		// generic message

	// first fill in the window class stucture
	winclass.style = CS_DBLCLKS | CS_OWNDC |
		CS_HREDRAW | CS_VREDRAW;
	winclass.lpfnWndProc = WindowProc;
	winclass.cbClsExtra = 0;
	winclass.cbWndExtra = 0;
	winclass.hInstance = hinstance;
	winclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	winclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	winclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	winclass.lpszMenuName = NULL;
	winclass.lpszClassName = WINDOW_CLASS_NAME;

	// register the window class
	if (!RegisterClass(&winclass))
		return(0);

	// create the window, note the use of WS_POPUP
	if (!(hwnd = CreateWindow(WINDOW_CLASS_NAME, // class
		L"Chess Client",	 // title
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		0, 0,	   // x,y
		WINDOW_WIDTH,  // width
		WINDOW_HEIGHT, // height
		NULL,	   // handle to parent 
		NULL,	   // handle to menu
		hinstance,// instance
		NULL)))	// creation parms
		return(0);

	// save the window handle and instance in a global
	main_window_handle = hwnd;
	main_instance = hinstance;

	// perform all game console specific initialization
	Game_Init();

	Login_Request();
	// enter main event loop
	while (1)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			// test if this is a quit
			if (msg.message == WM_QUIT)
				break;

			// translate any accelerator keys
			TranslateMessage(&msg);

			// send the message to the window proc
			DispatchMessage(&msg);
		} // end if

	// main game processing goes here
		Game_Main();

	} // end while

// shutdown game and release all resources
	Game_Shutdown();

	// return to Windows like this
	return(msg.wParam);

} // end WinMain

///////////////////////////////////////////////////////////

// WINX GAME PROGRAMMING CONSOLE FUNCTIONS ////////////////

int Game_Init(void *parms)
{
	// this function is where you do all the initialization 
	// for your game
	//AllocConsole();
	//freopen("CONOUT$", "wt", stdout);


	// set up screen dimensions
	screen_width = WINDOW_WIDTH;
	screen_height = WINDOW_HEIGHT;
	screen_bpp = 32;

	// initialize directdraw
	DD_Init(screen_width, screen_height, screen_bpp);

	// create and load the reactor bitmap image

	int index_i = 0;
	int index_j = 0;
	char data = 0;
	ifstream File("Map.txt");

	while (!File.eof())
	{
		File >> data;
		g_map_buffer[index_i][index_j] = data;
		//printf("%c", data);
		index_j++;
		if (index_j == 300)
		{
			index_i++;
			index_j = 0;
			//printf("\n");
		}
	}


	Create_Bitmap32(&grass_tile, 0, 0, 45, 45);
	Load_Image_Bitmap32(&grass_tile, L"grass.png", 0, 0, BITMAP_EXTRACT_MODE_ABS);
	grass_tile.x = 0;
	grass_tile.y = 0;
	Create_Bitmap32(&stone_tile, 0, 0, 45, 45);
	Load_Image_Bitmap32(&stone_tile, L"stone.png", 0, 0, BITMAP_EXTRACT_MODE_ABS);
	stone_tile.x = 0;
	stone_tile.y = 0;
	Create_Bitmap32(&snow_tile, 0, 0, 45, 45);
	Load_Image_Bitmap32(&snow_tile, L"snow.png", 0, 0, BITMAP_EXTRACT_MODE_ABS);
	stone_tile.x = 0;
	stone_tile.y = 0;
	Create_Bitmap32(&sand_tile, 0, 0, 45, 45);
	Load_Image_Bitmap32(&sand_tile, L"sand.png", 0, 0, BITMAP_EXTRACT_MODE_ABS);
	sand_tile.x = 0;
	sand_tile.y = 0;
	Create_Bitmap32(&crystal_tile, 0, 0, 45, 45);
	Load_Image_Bitmap32(&crystal_tile, L"crystal.png", 0, 0, BITMAP_EXTRACT_MODE_ABS);
	stone_tile.x = 0;
	stone_tile.y = 0;

	// now let's load in all the frames for the skelaton!!!

	Load_Texture(L"soldier.png", UNIT_TEXTURE, 44, 44);

	if (!Create_BOB32(&player, 0, 0, 44, 44, 1, BOB_ATTR_SINGLE_FRAME)) return(0);
	Load_Frame_BOB32(&player, UNIT_TEXTURE, 0, 0, 0, BITMAP_EXTRACT_MODE_CELL);

	// set up stating state of skelaton
	Set_Animation_BOB32(&player, 0);
	Set_Anim_Speed_BOB32(&player, 4);
	Set_Vel_BOB32(&player, 0, 0);
	Set_Pos_BOB32(&player, 0, 0);


	// create skelaton bob
	for (int i = 0; i < MAX_USER; ++i) {
		if (!Create_BOB32(&skelaton[i], 0, 0, 44, 44, 0, BOB_ATTR_SINGLE_FRAME))
			return(0);
		Load_Frame_BOB32(&skelaton[i], UNIT_TEXTURE, 0, 0, 0, BITMAP_EXTRACT_MODE_CELL);

		// set up stating state of skelaton
		Set_Animation_BOB32(&skelaton[i], 0);
		Set_Anim_Speed_BOB32(&skelaton[i], 4);
		Set_Vel_BOB32(&skelaton[i], 0, 0);
		Set_Pos_BOB32(&skelaton[i], 0, 0);
	}

	Load_Texture(L"monster1.png", UNIT_TEXTURE, 44, 44);
	for (int i = NPC_START; i < 13333; ++i) {
		if (!Create_BOB32(&npc[i], 0, 0, 44, 44, 0, BOB_ATTR_SINGLE_FRAME))
			return(0);
		Load_Frame_BOB32(&npc[i], UNIT_TEXTURE, 0, 0, 0, BITMAP_EXTRACT_MODE_CELL);
	}
	

	Load_Texture(L"monster2.png", UNIT_TEXTURE, 44, 44);
	for (volatile int i = 13333; i < 16666; ++i) {
		if (!Create_BOB32(&npc[i], 0, 0, 44, 44, 0, BOB_ATTR_SINGLE_FRAME))
			return(0);
		Load_Frame_BOB32(&npc[i], UNIT_TEXTURE, 0, 0, 0, BITMAP_EXTRACT_MODE_CELL);

	}
	
	Load_Texture(L"monster3.png", UNIT_TEXTURE, 44, 44);
	for (volatile int i = 16666; i < MAX_NPC; ++i) {
		if (!Create_BOB32(&npc[i], 0, 0, 44, 44, 0, BOB_ATTR_SINGLE_FRAME))
			return(0);
		Load_Frame_BOB32(&npc[i], UNIT_TEXTURE, 0, 0, 0, BITMAP_EXTRACT_MODE_CELL);
	}

	//set up stating state of skelaton

	for (int i = 0; i < MAX_NPC; ++i)
	{
		Set_Animation_BOB32(&npc[i], 0);
		Set_Anim_Speed_BOB32(&npc[i], 4);
		Set_Vel_BOB32(&npc[i], 0, 0);
		Set_Pos_BOB32(&npc[i], 0, 0);
	}



	// set clipping rectangle to screen extents so mouse cursor
	// doens't mess up at edges
	//RECT screen_rect = {0,0,screen_width,screen_height};
	//lpddclipper = DD_Attach_Clipper(lpddsback,1,&screen_rect);

	// hide the mouse
	//ShowCursor(FALSE);


	WSADATA	wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	g_mysocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);

	SOCKADDR_IN ServerAddr;
	ZeroMemory(&ServerAddr, sizeof(SOCKADDR_IN));
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(SERVER_PORT);
	ServerAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	int Result = WSAConnect(g_mysocket, (sockaddr *)&ServerAddr, sizeof(ServerAddr), NULL, NULL, NULL, NULL);

	WSAAsyncSelect(g_mysocket, main_window_handle, WM_SOCKET, FD_CLOSE | FD_READ);

	send_wsabuf.buf = send_buffer;
	send_wsabuf.len = BUF_SIZE;
	recv_wsabuf.buf = recv_buffer;
	recv_wsabuf.len = BUF_SIZE;



	// return success
	return(1);

} // end Game_Init

///////////////////////////////////////////////////////////

int Game_Shutdown(void *parms)
{
	// ㅡㅡㅡhis function is where you shutdown your game and
	// release all resources that you allocated

	// kill the reactor
	Destroy_Bitmap32(&snow_tile);
	Destroy_Bitmap32(&crystal_tile);
	Destroy_Bitmap32(&stone_tile);
	Destroy_Bitmap32(&grass_tile);
	// kill skelaton
	for (int i = 0; i < MAX_USER; ++i) Destroy_BOB32(&skelaton[i]);
	for (int i = NPC_START; i < MAX_NPC; ++i)
		Destroy_BOB32(&npc[i]);

	// shutdonw directdraw
	DD_Shutdown();

	WSACleanup();

	// return success
	return(1);
} // end Game_Shutdown

///////////////////////////////////////////////////////////

int Game_Main(void *parms)
{
	// this is the workhorse of your game it will be called
	// continuously in real-time this is like main() in C
	// all the calls for you game go here!
	// check of user is trying to exit
	if (KEY_DOWN(VK_ESCAPE) || KEY_DOWN(VK_SPACE))
		PostMessage(main_window_handle, WM_DESTROY, 0, 0);


	// start the timing clock
	Start_Clock();

	// clear the drawing surface
	DD_Fill_Surface(D3DCOLOR_ARGB(255, 0, 0, 0));

	// get player input

	g_pd3dDevice->BeginScene();
	g_pSprite->Begin(D3DXSPRITE_ALPHABLEND | D3DXSPRITE_SORT_TEXTURE);

	// draw the background reactor image
	for (int i = 0; i < 21; ++i)
		for (int j = 0; j < 21; ++j)
		{
			int tile_x = j + g_left_x;
			int tile_y = i + g_top_y;
			if ((tile_x < 0) || (tile_y < 0) || ((tile_x > 300) || (tile_y > 300))) continue;
			if (g_map_buffer[tile_y][tile_x] == '0')
				Draw_Bitmap32(&grass_tile, TILE_WIDTH*j + 7, TILE_WIDTH*i + 7);
			else if (g_map_buffer[tile_y][tile_x] == '1')
				Draw_Bitmap32(&sand_tile, TILE_WIDTH*j + 7, TILE_WIDTH*i + 7);
			else if (g_map_buffer[tile_y][tile_x] == '2')
				Draw_Bitmap32(&snow_tile, TILE_WIDTH*j + 7, TILE_WIDTH*i + 7);
			else if (g_map_buffer[tile_y][tile_x] == '3')
				Draw_Bitmap32(&crystal_tile, TILE_WIDTH*j + 7, TILE_WIDTH*i + 7);
			else if (g_map_buffer[tile_y][tile_x] == '5')
				Draw_Bitmap32(&stone_tile, TILE_WIDTH*j + 7, TILE_WIDTH*i + 7);

			//if (tile_x % 8 == 0 || tile_y % 8 == 0) {
			//	Draw_Bitmap32(&wood_tile, TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
			//}
			//else if (((tile_x >> 2) % 2) == ((tile_y >> 2) % 2))
			//	Draw_Bitmap32(&white_tile, TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
			//else
			//	Draw_Bitmap32(&black_tile, TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
			//if (tile_x % 8 == 0 || tile_y % 8 == 0) {
			//	Draw_Bitmap32(&wood_tile, TILE_WIDTH * i + 8, TILE_WIDTH * j + 7);

		}
	//	Draw_Bitmap32(&reactor);

	g_pSprite->End();
	g_pSprite->Begin(D3DXSPRITE_ALPHABLEND | D3DXSPRITE_SORT_TEXTURE);


	// draw the skelaton
	Draw_BOB32(&player);
	for (int i = 0; i < MAX_USER; ++i) Draw_BOB32(&skelaton[i]);
	for (int i = NPC_START; i < MAX_NPC; ++i) Draw_BOB32(&npc[i]);

	// draw some text
	wchar_t text[300];
	
	wsprintf(text, L"MY POSITION (%3d, %3d)", player.x, player.y);
	Draw_Text_D3D(text, 10, screen_height - 64, D3DCOLOR_ARGB(255, R, G, B));
	g_my_hp_full = 100 + g_my_level * 30;
	wsprintf(UI, L"ID : %s  LEVEL : %d  EXP : %d / %d  HP : %d / %d  POWER : %d", g_myname, g_my_level, g_my_exp, g_my_level * 100, g_my_hp_current, g_my_hp_full, g_my_power);
	Draw_Text_D3D(UI, 10, 10, D3DCOLOR_ARGB(255, R, G, B));

	if (enterflag == true)
	{
		
	}
	if (color_flag == true)
	{
		R += 5;
		G += 5;
		B += 5;
		if (R == 255)
			color_flag = false;
	}
	else
	{
		R -= 5;
		G -= 5;
		B -= 5;
		if (R == 0)
			color_flag = true;
	}
	Draw_Text_D3D(system_message, 250, screen_height - 800, D3DCOLOR_ARGB(255, R, G, B));

	g_pSprite->End();
	g_pd3dDevice->EndScene();

	// flip the surfaces

	DD_Flip();

	// sync to 3o fps
	//Wait_Clock(30);


	// return success
	return(1);

} // end Game_Main

//////////////////////////////////////////////////////////