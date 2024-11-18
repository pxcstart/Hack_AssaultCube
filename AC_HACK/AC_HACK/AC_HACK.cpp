#include <stdio.h>
#include <math.h>
#include <tchar.h>
#include <Windows.h>
#include <tlhelp32.h>
#define TOP_HEIGHT 25 //���崰�ڱ����������ظ߶�
#define M_PI 3.14159265358979323846264338327950288
typedef struct PlayerData
{
	DWORD BaseEntity;
	float Position[3];
	int HP;
	int TeamFlag;
	char Name[32];
}PD;
bool IsAuto; //���鿪��
HANDLE g_hProcess; //��Ϸ���̾��
RECT g_winRect; //������Ļ�������
HWND g_hWnd_Overlay; //�ɰ洰��-���ھ��
float g_Matrix[4][4]; //��������

///OFFSET///  ע��ʹ�õ�assaultcube�İ汾Ϊv1.3.0.2
static DWORD base_address = 0x00400000;
static DWORD offset_address = 0x18AC00;           
static DWORD player_base = base_address + offset_address; //��һ���ַ
static DWORD entity_base = player_base + 0x4; //entity����ַ
static DWORD offset_playersnum = player_base + 0xC; //��Ϸ�е��������
static DWORD offset_viewmatrix = 0x57DFD0; //��������
//player�ṹ���ڲ���Ա����ƫ��
static DWORD offset_health = 0xEC;    
static DWORD offset_name = 0x205;       
static DWORD offset_posx = 0x4;
static DWORD offset_posy = 0x8;
static DWORD offset_posz = 0xC;
static DWORD offset_team = 0x30C;            
static DWORD offset_ang_left_right = 0x34;       
static DWORD offset_ang_up_down = 0x38;        

void __stdcall KeyHandlerThread() //���������ݼ�
{
	while (1)
	{
		if (GetAsyncKeyState(VK_CONTROL) < 0)
			IsAuto = true;
		else IsAuto = false;
		Sleep(1u);
	}
}

LRESULT CALLBACK WindowProc_Overlay(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)  //���ڴ�����
{
	switch (message)
	{
	case WM_PAINT:
		break;
	case WM_CREATE:
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_CLOSE:
		DestroyWindow(g_hWnd_Overlay);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
		break;
	}
	return 0;
}

void CreateOverlayWindow()  //�����ɰ洰��
{
	WNDCLASSEX wc;  //step1:���һ��������
	ZeroMemory(&wc, sizeof(WNDCLASSEX));
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WindowProc_Overlay;  //���ڹ��̺���
	wc.hInstance = GetModuleHandle(0);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)RGB(0, 0, 0);
	wc.lpszClassName = "Overlay";
	RegisterClassEx(&wc); //step2:ע�ᴰ����
	//step3:��������  ע�����ڴ��������ɰ洰�ڣ���WS_EX_TRANSPARENT��������Ϊ͸��
	g_hWnd_Overlay = CreateWindowEx(WS_EX_LAYERED | WS_EX_TRANSPARENT, wc.lpszClassName, "OverlayWindow", WS_POPUP, g_winRect.left, g_winRect.top,
		(g_winRect.right - g_winRect.left), (g_winRect.bottom - g_winRect.top), NULL, NULL, wc.hInstance, NULL);
	//���ô���͸����
	SetLayeredWindowAttributes(g_hWnd_Overlay, RGB(0, 0, 0), 0, ULW_COLORKEY);
	//step4:��ʾ�͸���
	ShowWindow(g_hWnd_Overlay, SW_SHOW);
	UpdateWindow(g_hWnd_Overlay);
}

BOOL GameIsForegroundWindow()  //�ж�ǰ̨�����Ƿ�Ϊ��Ϸ����
{
	HWND hWnd = GetForegroundWindow();
	if (hWnd == FindWindow(NULL, "AssaultCube"))
		return TRUE;
	else
		return FALSE;
}

void Clear()
{
	RECT rect = { 0,0,g_winRect.right - g_winRect.left,g_winRect.bottom - g_winRect.top };
	HWND hWnd = GetForegroundWindow();//��ȡ��ǰ����
	if (hWnd == FindWindow(NULL, "AssaultCube"))
	{
		HDC hDc = GetDC(g_hWnd_Overlay);//�˴�Ϊoverlay���ھ��
		HBRUSH hBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);//�ջ�ˢ
		SelectObject(hDc, hBrush);
		FillRect(hDc, &rect, hBrush);
		DeleteObject(hBrush);
		ReleaseDC(g_hWnd_Overlay, hDc);//�˴�Ϊoverlay���ھ��
	}
}

BOOL WorldToScreen(float from[3], float to[2]) //��������תΪ��Ļ����
{
	int width = g_winRect.right - g_winRect.left;
	int height = g_winRect.bottom - g_winRect.top;
	float camX = width / 2.0;
	float camY = height / 2.0;
	float screenX = g_Matrix[0][0] * from[0] + g_Matrix[1][0] * from[1] + g_Matrix[2][0] * from[2] + g_Matrix[3][0];
	float screenY = g_Matrix[0][1] * from[0] + g_Matrix[1][1] * from[1] + g_Matrix[2][1] * from[2] + g_Matrix[3][1];
	float screenW = g_Matrix[0][3] * from[0] + g_Matrix[1][3] * from[1] + g_Matrix[2][3] * from[2] + g_Matrix[3][3];
	if (screenW > 0.001f)
	{
		to[0] = camX + (camX * screenX / screenW);
		to[1] = camY - (camY * screenY / screenW);
		return true;
	}
	return false;

}

void GetXYDistance(int index,PD enemydata,float* xyDist)
{
	float enemy_pos_screen[2];
	WorldToScreen(enemydata.Position, enemy_pos_screen);
	if ((enemy_pos_screen[0] > 0 && enemy_pos_screen[0] < g_winRect.right - g_winRect.left) && (enemy_pos_screen[1] > 0 && enemy_pos_screen[1] < g_winRect.bottom - g_winRect.top)) //�ж��Ƿ�����Ļ��
		xyDist[index] = fabs(enemy_pos_screen[0] - (g_winRect.right - g_winRect.left) / 2) + fabs(enemy_pos_screen[1] - (g_winRect.bottom - g_winRect.top) / 2);
	else
		xyDist[index] = 999999.0f;
}

float GetDistance3D(float MyPos[3], float ObjPos[3])
{
	return sqrt
	(
		pow(double(ObjPos[0] - MyPos[0]), 2.0) +
		pow(double(ObjPos[1] - MyPos[1]), 2.0) +
		pow(double(ObjPos[2] - MyPos[2]), 2.0)
	);
}

void DrawEsp(float Enemy_x, float Enemy_y, float distance, PD enemy)
{
	float Rect_w = 2100 / distance;
	float Rect_h = 4000 / distance;
	float Rect_x = (Enemy_x - (Rect_w / 2));
	float Rect_y = Enemy_y;
	float Line_x = (g_winRect.right - g_winRect.left) / 2;
	float Line_y = g_winRect.bottom - g_winRect.top;
	HWND hWnd = GetForegroundWindow();//��ȡ��ǰ����
	if (hWnd == FindWindow(NULL, "AssaultCube"))
	{
		HDC hDc = GetDC(g_hWnd_Overlay);//!�˴�Ϊoverlay���ھ��
		HPEN hPen = CreatePen(PS_SOLID, 2, 0x0000FF);//����һ����������޷��޸�
		HBRUSH hBrush = (HBRUSH)GetStockObject(NULL_BRUSH);//�ջ�ˢ
		SelectObject(hDc, hPen);
		SelectObject(hDc, hBrush);

		Rectangle(hDc, Rect_x, Rect_y, Rect_x + Rect_w, Rect_y + Rect_h);
		MoveToEx(hDc, Line_x, Line_y, NULL);
		LineTo(hDc, Enemy_x, Enemy_y);

		SetTextColor(hDc, RGB(0, 0, 255));
		SetBkMode(hDc, NULL_BRUSH);     //��������
		TextOutA(hDc, Rect_x, Rect_y - 20, enemy.Name, strlen(enemy.Name));
		DeleteObject(hBrush);
		DeleteObject(hPen);

		ReleaseDC(g_hWnd_Overlay, hDc);//!�˴�Ϊoverlay���ھ��
	}
}

void Esp(PD player, PD enemy)
{
	float EnemyXY[2];
	WorldToScreen(enemy.Position, EnemyXY);
	//printf("������Ļ���꣺%d %d\n", EnemyXY[0], EnemyXY[1]);
	float Distance = GetDistance3D(player.Position, enemy.Position);
	DrawEsp(EnemyXY[0], EnemyXY[1], Distance, enemy);
}

int GetMinXYDistanceIndex(float* xyDistance, int len)
{
	int MinIndex = 0;
	for (int i = 0; i < len; i++)
	{
		if (xyDistance[i] < xyDistance[MinIndex] )
			MinIndex = i;
	}
	if (xyDistance[MinIndex] == 999999.0f )
		return -1;
	return MinIndex;
}

void AutoCollimation(float MY_Position[3], float EL_Position[3], PD player)  //�Զ���׼
{
	float relative_x = EL_Position[0] - MY_Position[0];
	float relative_y = EL_Position[1] - MY_Position[1];
	float relative_z = EL_Position[2] - MY_Position[2];
	float dist = sqrt(pow(relative_x, 2) + pow(relative_y, 2)+pow(relative_z,2));
	
	float ang1 = atan(relative_x / relative_y) * 180 / M_PI;
	float ang2 = asin(relative_z / dist) * 180 / M_PI;
	float yaw, pitch;  //yaw�ķ�ΧΪ0~360��pitch�ķ�ΧΪ-90~90
	/*if (ang1 > 0)
	{
		if (relative_x >= 0 && relative_y >= 0) yaw = ang1;
		else yaw = 180 + ang1;
	}
	else
	{
		if (relative_x >= 0 && relative_y <= 0) yaw = 90 + ang1;
		else yaw = 360 - ang1;
	}
	yaw += 90;
	pitch = ang2;*/
	yaw = atan(relative_y / relative_x) * 180 / M_PI + 90;
	pitch = asin(relative_z/dist) * 180 / M_PI;
	WriteProcessMemory(g_hProcess, (LPVOID)(player.BaseEntity + offset_ang_left_right), &yaw, sizeof(yaw), nullptr);
	WriteProcessMemory(g_hProcess, (LPVOID)(player.BaseEntity + offset_ang_up_down), &pitch, sizeof(pitch), nullptr);
}



int main()
{
	HWND hWnd = FindWindow(NULL, "AssaultCube"); //��Ϸ���ھ��
	if (!hWnd) return -1;
	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)KeyHandlerThread, 0, 0, 0);
	DWORD dwPid;  //��Ϸ���̺�
	GetWindowThreadProcessId(hWnd, &dwPid);
	//printf("��Ϸ���̺�Ϊ%d\n", dwPid);
	g_hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwPid);
	GetWindowRect(hWnd, &g_winRect);
	g_winRect.top += TOP_HEIGHT;
	CreateOverlayWindow();
	//��Ϣѭ��
	MSG msg;
	ZeroMemory(&msg, sizeof(MSG));
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if (GameIsForegroundWindow() == TRUE)  //���ж���Ϸ�����Ƿ���ǰ̨���У�����Ϸ���е���̨�����ɰ�Ӧ��ͣ����
			SetWindowPos(g_hWnd_Overlay, HWND_TOPMOST, g_winRect.left, g_winRect.top, (g_winRect.right - g_winRect.left), (g_winRect.bottom - g_winRect.top), SWP_SHOWWINDOW);
		else
			SetWindowPos(g_hWnd_Overlay, HWND_BOTTOM, g_winRect.left, g_winRect.top, (g_winRect.right - g_winRect.left), (g_winRect.bottom - g_winRect.top), SWP_SHOWWINDOW);
		//���ݵ�ǰ��Ϣ���´�����Ļ���겢��ʱ�ƶ��ɰ�
		GetWindowRect(hWnd, &g_winRect);
		g_winRect.top += TOP_HEIGHT;
		MoveWindow(g_hWnd_Overlay, g_winRect.left, g_winRect.top, (g_winRect.right - g_winRect.left), (g_winRect.bottom - g_winRect.top), TRUE);
		Clear();
		//�������Լ�����Ϣ
		PD player;
		ReadProcessMemory(g_hProcess, (LPCVOID*)(player_base),&player.BaseEntity,sizeof(player.BaseEntity),0);
		ReadProcessMemory(g_hProcess, (LPCVOID*)(player.BaseEntity + offset_health), &player.HP, sizeof(player.HP), 0);
		ReadProcessMemory(g_hProcess, (LPCVOID*)(player.BaseEntity + offset_team), &player.TeamFlag, sizeof(player.TeamFlag), 0);
		ReadProcessMemory(g_hProcess, (LPCVOID*)(player.BaseEntity + offset_name), &player.Name, sizeof(player.Name), 0);
		ReadProcessMemory(g_hProcess, (LPCVOID*)(player.BaseEntity + 0x4), &player.Position[0], sizeof(player.Position[0]), 0);
		ReadProcessMemory(g_hProcess, (LPCVOID*)(player.BaseEntity + 0x8), &player.Position[1], sizeof(player.Position[1]), 0);
		ReadProcessMemory(g_hProcess, (LPCVOID*)(player.BaseEntity + 0xC), &player.Position[2], sizeof(player.Position[2]), 0);
		int playersnum;
		ReadProcessMemory(g_hProcess, (LPCVOID*)(offset_playersnum), &playersnum, sizeof(int), 0);
		PD entitylist[50];
		float xyDistance[50] = { 0 }; //���˾�����Ļ���ĵ�����
		for (int i = 0; i < playersnum; i++)
		{
			ReadProcessMemory(g_hProcess, LPCVOID(entity_base), &entitylist[i].BaseEntity, sizeof(entitylist[i].BaseEntity), 0);
			ReadProcessMemory(g_hProcess, LPCVOID(entitylist[i].BaseEntity + (0x4 * i)), &entitylist[i].BaseEntity, sizeof(entitylist[i].BaseEntity), 0);
			ReadProcessMemory(g_hProcess, (PBYTE*)(entitylist[i].BaseEntity + offset_name), &entitylist[i].Name, sizeof(entitylist[i].Name), 0);
			ReadProcessMemory(g_hProcess, LPCVOID(entitylist[i].BaseEntity + offset_health), &entitylist[i].HP, sizeof(entitylist[i].HP), 0);
			ReadProcessMemory(g_hProcess, LPCVOID(entitylist[i].BaseEntity + offset_posx), &entitylist[i].Position[0], sizeof(entitylist[i].Position[0]), 0);
			ReadProcessMemory(g_hProcess, LPCVOID(entitylist[i].BaseEntity + offset_posy), &entitylist[i].Position[1], sizeof(entitylist[i].Position[1]), 0);
			ReadProcessMemory(g_hProcess, LPCVOID(entitylist[i].BaseEntity + offset_posz), &entitylist[i].Position[2], sizeof(entitylist[i].Position[2]), 0);
			ReadProcessMemory(g_hProcess, LPCVOID(entitylist[i].BaseEntity + offset_team), &entitylist[i].TeamFlag, sizeof(entitylist[i].TeamFlag), 0);
			//��ȡ����������Ϣ
			ReadProcessMemory(g_hProcess, (PBYTE*)(offset_viewmatrix), &g_Matrix, sizeof(g_Matrix), 0);
			if (entitylist[i].TeamFlag != player.TeamFlag) //˵���ǵ���
			{
				if (entitylist[i].HP < 2) //��Ϊ�Ѿ�����
					continue;
				GetXYDistance(i, entitylist[i], xyDistance);
				Esp(player, entitylist[i]);
			}
		}
		if (IsAuto == true)
		{
			//ȡ�þ�����Ļ����������˵��±�
			int index = GetMinXYDistanceIndex(xyDistance,playersnum);
			if (index != -1)
			{
				ReadProcessMemory(g_hProcess, LPCVOID(entity_base), &entitylist[index].BaseEntity, sizeof(entitylist[index].BaseEntity), 0);
				ReadProcessMemory(g_hProcess, LPCVOID(entitylist[index].BaseEntity + (0x4 * index)), &entitylist[index].BaseEntity, sizeof(entitylist[index].BaseEntity), 0);
				ReadProcessMemory(g_hProcess, (PBYTE*)(entitylist[index].BaseEntity + offset_name), &entitylist[index].Name, sizeof(entitylist[index].Name), 0);
				ReadProcessMemory(g_hProcess, LPCVOID(entitylist[index].BaseEntity + offset_health), &entitylist[index].HP, sizeof(entitylist[index].HP), 0);
				ReadProcessMemory(g_hProcess, LPCVOID(entitylist[index].BaseEntity + offset_posx), &entitylist[index].Position[0], sizeof(entitylist[index].Position[0]), 0);
				ReadProcessMemory(g_hProcess, LPCVOID(entitylist[index].BaseEntity + offset_posy), &entitylist[index].Position[1], sizeof(entitylist[index].Position[1]), 0);
				ReadProcessMemory(g_hProcess, LPCVOID(entitylist[index].BaseEntity + offset_posz), &entitylist[index].Position[2], sizeof(entitylist[index].Position[2]), 0);
				ReadProcessMemory(g_hProcess, LPCVOID(entitylist[index].BaseEntity + offset_team), &entitylist[index].TeamFlag, sizeof(entitylist[index].TeamFlag), 0);
				//��ȡ����������Ϣ
				ReadProcessMemory(g_hProcess, (PBYTE*)(offset_viewmatrix), &g_Matrix, sizeof(g_Matrix), 0);
				AutoCollimation(player.Position, entitylist[index].Position,player);
			}
			Sleep(20);
		}
	}
	CloseHandle(g_hProcess);
	return 0;
}