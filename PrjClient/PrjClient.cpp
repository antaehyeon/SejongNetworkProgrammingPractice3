#pragma comment(lib, "ws2_32")
#define _WINSOCK_DEPRECATED_NO_WARNINGS 

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include "resource.h"
#include "windows.h"

#define SERVERIPV4  "127.0.0.1"
#define SERVERIPV6  "::1"
#define SERVERPORT  9000

#define BUFSIZE     256                    // ���� �޽��� ��ü ũ��
#define MSGSIZE     (BUFSIZE-sizeof(int))  // ä�� �޽��� �ִ� ����

#define INIT 100
#define CHATTING    1000                   // �޽��� Ÿ�� : ä��
#define NICKNAMECHANGE 2000				   // �޼��� Ÿ�� : �г��� ����
#define SAMENICKNAME 2001
#define NICKNAMECHECK 2002
#define REQUESTNICKNAME 3000
#define REQUESTSECONDNICKNAME 3100
#define RECEIVENICKNAME 3001
#define WHISPERMODE 4000
#define IDCHECKFORWHISPER 4001
#define SAMENICKNAMEFORWHISPER 4002
#define RECEIVEFORWHISPER 4003

#define WM_DRAWIT   (WM_USER+1)            // ����� ���� ������ �޽���

#define FIRST_CHAT 1					   // ä�ù� : 1��°
#define SECOND_CHAT 2					   // ä�ù� : 2��°

// ���� �޽��� ����
// sizeof(COMM_MSG) == 256
struct COMM_MSG
{
	int  type;
	char dummy[MSGSIZE];
};

// ä�� �޽��� ����
// sizeof(CHAT_MSG) == 256
struct CHAT_MSG
{
	int  type;
	int  chatMode;
	char buf[124];
	char nickName[124];
};

static HINSTANCE     g_hInst; // ���� ���α׷� �ν��Ͻ� �ڵ�
static HWND          g_hButtonSendMsg; // '�޽��� ����' ��ư
static HWND          g_hEditStatus; // ���� �޽��� ���
static HWND			 g_hSecondEditStatus; // ���� �޼��� ��� �ι�° â
static char          g_ipaddr[64]; // ���� IP �ּ�
static char			 g_nickNameFirstConnection[124];
static u_short       g_port; // ���� ��Ʈ ��ȣ
static BOOL          g_isIPv6; // IPv4 or IPv6 �ּ�?
static HANDLE        g_hClientThread; // ������ �ڵ�
static volatile BOOL g_bStart; // ��� ���� ����
static SOCKET        g_sock; // Ŭ���̾�Ʈ ����
static HANDLE        g_hReadEvent, g_hWriteEvent; // �̺�Ʈ �ڵ�
static CHAT_MSG      g_chatmsg; // ä�� �޽��� ����

static int			 g_isChatting; // ä�ù� ���

static char			 firstNickName[256] = { '\0', };
static char			 secondNickName[256] = { '\0', };
static char			 tempNickName[256] = { '\0', };
static char			 tempWhisperNickName[256] = { '\0', };
static char			 tempWhisperChatMsg[256] = { '\0', };
static char			 tempWhisperSecondNickName[256] = { '\0', };
static char			 tempWhisperSecondChatMsg[256] = { '\0', };
static bool			 showNickName = false;
static bool			 sendNickName = false;

static bool			 isNickNameChange = false;
static bool			 isDuplicationNickName = false;

static bool			 isInitialNickNameFirstRoom = false;
static bool			 isInitialNickNameSecondRoom = false;

static bool			 isSetFirstNickName = false;
static bool			 isSetSecondNickName = false;

// �ӼӸ� ���� ���
static bool			 isInitialWhisperFirst = false;
static bool			 isFirstWhisperState = false;
static bool			 isFirstWhisperButtonChk = false;

// �ӼӸ� ���� ���
static bool			 isInitialWhisperSecond = false;
static bool			 isSecondWhisperState = false;
static bool			 isSecondWhisperButtonChk = false;

static bool			 isDuplicationForWhisper = false;
static bool			 isJudgeOwnerWhisper = false;

static bool			 isMsgSend = false;


// ��ȭ���� ���ν���
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// ���� ��� ������ �Լ�
DWORD WINAPI ClientMain(LPVOID arg);
DWORD WINAPI ReadThread(LPVOID arg);
DWORD WINAPI WriteThread(LPVOID arg);
// ���� ��Ʈ�� ��� �Լ�
void DisplayText(int chatingMode, char *fmt, ...);
// ����� ���� ������ ���� �Լ�
int recvn(SOCKET s, char *buf, int len, int flags);
// ���� ��� �Լ�
void err_quit(char *msg);
void err_display(char *msg);

// ���� �Լ�
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
	// ���� �ʱ�ȭ
	WSADATA wsa;
	if(WSAStartup(MAKEWORD(2,2), &wsa) != 0) return 1;

	// �̺�Ʈ ����
	g_hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if(g_hReadEvent == NULL) return 1;
	g_hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(g_hWriteEvent == NULL) return 1;

	// ���� �ʱ�ȭ(�Ϻ�)
	g_chatmsg.type = CHATTING;
	g_chatmsg.chatMode = FIRST_CHAT;

	// ��ȭ���� ����
	g_hInst = hInstance;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	// �̺�Ʈ ����
	CloseHandle(g_hReadEvent);
	CloseHandle(g_hWriteEvent);

	// ���� ����
	WSACleanup();
	return 0;
}

// ��ȭ���� ���ν���
BOOL CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static HWND hEditIPaddr;
	static HWND hEditPort;
	static HWND hButtonConnect;
	static HWND hEditMsg;
	static HWND hEditNickName;
	static HWND hSecondEditNickName;

	// ä�ù� �����ϴ� ��ư ����
	static HWND btnFirstChatConnect;
	static HWND btnSecondChatConnect;

	// �г��� �����ϴ� ��ư
	static HWND btnNickNameChange;

	// �г��� ����ϴ� ��ư
	static HWND btnShowFirstRoom;
	static HWND btnShowSecondRoom;

	// �ӼӸ� ��ü
	static HWND hEditWhisper;
	static HWND btnWhisper;
	static HWND hEditSecondWhisper;
	static HWND btnSecondWhisper;

	switch(uMsg){
	case WM_INITDIALOG:
		hEditIPaddr = GetDlgItem(hDlg, IDC_IPADDRESS);
		hEditPort = GetDlgItem(hDlg, IDC_PORT);
		hEditNickName = GetDlgItem(hDlg, IDC_NICKNAME);
		hSecondEditNickName = GetDlgItem(hDlg, IDC_NICKNAME2);
		hButtonConnect = GetDlgItem(hDlg, IDC_CONNECT);

		g_hButtonSendMsg = GetDlgItem(hDlg, IDC_SENDMSG);
		g_hEditStatus = GetDlgItem(hDlg, IDC_STATUS);
		g_hSecondEditStatus = GetDlgItem(hDlg, IDC_SECONDCHATINGROOM);

		hEditMsg = GetDlgItem(hDlg, IDC_MSG);

		// ä�ù� �����ϴ� ��ư �ʱ�ȭ
		btnFirstChatConnect = GetDlgItem(hDlg, IDC_CHAT1);
		btnSecondChatConnect = GetDlgItem(hDlg, IDC_CHAT2);

		// �г��� �����ϴ� ��ư
		btnNickNameChange = GetDlgItem(hDlg, IDC_NICKNAMECHANGEBTN);

		// ù��°, �ι�° �г��� ����ϴ� ��ư
		btnShowFirstRoom = GetDlgItem(hDlg, IDC_SHOWFIRST);
		btnShowSecondRoom = GetDlgItem(hDlg, IDC_SHOWSECOND);
		
		hEditWhisper = GetDlgItem(hDlg, IDC_WHISPERTEXTBOX);
		btnWhisper = GetDlgItem(hDlg, IDC_WHISPERBUTTON);
		hEditSecondWhisper = GetDlgItem(hDlg, IDC_SECONDWHISPERTEXTBOX);
		btnSecondWhisper = GetDlgItem(hDlg, IDC_BTNSECONDWHISPER);

		// ��Ʈ�� �ʱ�ȭ
		SendMessage(hEditMsg, EM_SETLIMITTEXT, MSGSIZE, 0);
		EnableWindow(g_hButtonSendMsg, FALSE);
		EnableWindow(btnFirstChatConnect, FALSE);
		EnableWindow(btnSecondChatConnect, FALSE);
		EnableWindow(btnNickNameChange, FALSE);
		ShowWindow(g_hSecondEditStatus, FALSE);
		ShowWindow(hSecondEditNickName, FALSE);
		EnableWindow(btnShowFirstRoom, FALSE);
		EnableWindow(btnShowSecondRoom, FALSE);
		EnableWindow(hEditWhisper, FALSE);
		EnableWindow(btnWhisper, FALSE);
		EnableWindow(hEditNickName, FALSE);
		// �ӼӸ� �κ� ��Ʈ�� ����
		EnableWindow(hEditWhisper, FALSE);
		EnableWindow(btnWhisper, FALSE);
		EnableWindow(hEditSecondWhisper, FALSE);
		EnableWindow(btnSecondWhisper, FALSE);
		ShowWindow(hEditSecondWhisper, FALSE);
		ShowWindow(btnSecondWhisper, FALSE);
		SetDlgItemText(hDlg, IDC_IPADDRESS, SERVERIPV4);
		SetDlgItemInt(hDlg, IDC_PORT, SERVERPORT, FALSE);

		// ������ Ŭ���� ���
		WNDCLASS wndclass;
		wndclass.style = CS_HREDRAW|CS_VREDRAW;
		wndclass.cbClsExtra = 0;
		wndclass.cbWndExtra = 0;
		wndclass.hInstance = g_hInst;
		wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
		wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wndclass.lpszMenuName = NULL;
		wndclass.lpszClassName = "MyWndClass";
		if(!RegisterClass(&wndclass)) return 1;

		return TRUE;

	case WM_COMMAND:
		switch(LOWORD(wParam)){

		case IDC_CONNECT:
			GetDlgItemText(hDlg, IDC_IPADDRESS, g_ipaddr, sizeof(g_ipaddr));
			GetDlgItemText(hDlg, IDC_NICKNAME, g_chatmsg.nickName, MSGSIZE);
			g_port = GetDlgItemInt(hDlg, IDC_PORT, NULL, FALSE);

			strcpy(firstNickName, g_chatmsg.nickName);

			// ��Ʈ��ȣ ����ó��
			if (g_port < 1024 || g_port > 49151) {
				MessageBox(NULL, "PORT�� ����� �Է��ϼ���", "���", MB_OK);
				break;
			}

			// ���� ��� ������ ����
			g_hClientThread = CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);
			if(g_hClientThread == NULL){
				MessageBox(hDlg, "Ŭ���̾�Ʈ�� ������ �� �����ϴ�."
					"\r\n���α׷��� �����մϴ�.", "����!", MB_ICONERROR);
				EndDialog(hDlg, 0);
			}
			else{
				EnableWindow(hButtonConnect, FALSE);
				while(g_bStart == FALSE); // ���� ���� ���� ��ٸ�
				EnableWindow(hEditIPaddr, FALSE);
				EnableWindow(hEditPort, FALSE);
				EnableWindow(g_hButtonSendMsg, TRUE);
				EnableWindow(btnFirstChatConnect, FALSE);
				EnableWindow(btnSecondChatConnect, TRUE);
				EnableWindow(btnShowFirstRoom, TRUE);
				EnableWindow(btnShowSecondRoom, TRUE);
				EnableWindow(hEditNickName, TRUE);
				 //�г��� ���õ� �κ�
				EnableWindow(hEditWhisper, FALSE);
				EnableWindow(btnWhisper, FALSE);
				EnableWindow(hEditSecondWhisper, FALSE);
				EnableWindow(btnSecondWhisper, FALSE);

				SetFocus(hEditMsg);
				g_isChatting = FIRST_CHAT;
				EnableWindow(btnNickNameChange, TRUE);
			}
			return TRUE;

		// ù��° ä�ù� �����ư�� Ŭ���� ���
		case IDC_CHAT1:
			ShowWindow(g_hSecondEditStatus, FALSE);
			ShowWindow(g_hEditStatus, TRUE);
			EnableWindow(btnFirstChatConnect, FALSE);
			EnableWindow(btnSecondChatConnect, TRUE);
			ShowWindow(hEditNickName, TRUE);
			ShowWindow(hSecondEditNickName, FALSE);
			// �ӼӸ� ���� ��Ʈ��
			ShowWindow(hEditWhisper, TRUE);
			ShowWindow(btnWhisper, TRUE);
			ShowWindow(hEditSecondWhisper, FALSE);
			ShowWindow(btnSecondWhisper, FALSE);

			g_chatmsg.chatMode = FIRST_CHAT;
			g_isChatting = FIRST_CHAT;
			return TRUE;

		// �ι�° ä�ù� �����ư�� Ŭ���� ���
		case IDC_CHAT2:
			ShowWindow(g_hEditStatus, FALSE);
			ShowWindow(g_hSecondEditStatus, TRUE);
			EnableWindow(btnFirstChatConnect, TRUE);
			EnableWindow(btnSecondChatConnect, FALSE);
			ShowWindow(hEditNickName, FALSE);
			ShowWindow(hSecondEditNickName, TRUE);
			// �ӼӸ� ���� ��Ʈ��
			ShowWindow(hEditWhisper, FALSE);
			ShowWindow(btnWhisper, FALSE);
			ShowWindow(hEditSecondWhisper, TRUE);
			ShowWindow(btnSecondWhisper, TRUE);

			g_chatmsg.chatMode = SECOND_CHAT;
			g_isChatting = SECOND_CHAT;
			return TRUE;

		case IDC_SENDMSG:
			// �б� �ϷḦ ��ٸ�
			WaitForSingleObject(g_hReadEvent, INFINITE);
			g_chatmsg.type = CHATTING;
			switch (g_isChatting)
			{
			// ù��° ä�ù��� ���
			case FIRST_CHAT:
				GetDlgItemText(hDlg, IDC_NICKNAME, g_chatmsg.nickName, MSGSIZE);
				GetDlgItemText(hDlg, IDC_MSG, g_chatmsg.buf, MSGSIZE);
				GetDlgItemText(hDlg, IDC_WHISPERTEXTBOX, tempWhisperNickName, MSGSIZE);
				strcpy(tempWhisperChatMsg, g_chatmsg.buf);
				if (!isInitialNickNameFirstRoom) {
					MessageBox(hDlg, "�г����� �������ּ��� :)", "����!", MB_ICONERROR);
					break;
				}
				else {
					strcpy(firstNickName, g_chatmsg.nickName);
					EnableWindow(hEditNickName, FALSE);
				}
				if (isFirstWhisperState) {
					isMsgSend = true;
					char whisperMessage[124] = { '\0', };

					strcat(whisperMessage, "[");
					strcat(whisperMessage, firstNickName);
					strcat(whisperMessage, "���� �ӼӸ�] : ");
					strcat(whisperMessage, g_chatmsg.buf);

					g_chatmsg.type = RECEIVEFORWHISPER;
					strcpy(g_chatmsg.nickName, tempWhisperNickName);
					strcpy(g_chatmsg.buf, whisperMessage);					
				}
				break;
			// �ι�° ä�ù��� ���
			case SECOND_CHAT:
				GetDlgItemText(hDlg, IDC_NICKNAME2, g_chatmsg.nickName, MSGSIZE);
				GetDlgItemText(hDlg, IDC_MSG, g_chatmsg.buf, MSGSIZE);
				GetDlgItemText(hDlg, IDC_SECONDWHISPERTEXTBOX, tempWhisperSecondNickName, MSGSIZE);
				strcpy(tempWhisperSecondChatMsg, g_chatmsg.buf);
				if (!isInitialNickNameSecondRoom) {
					MessageBox(hDlg, "�г����� �������ּ��� :)", "����!", MB_ICONERROR);
					break;
				}
				else {
					strcpy(secondNickName, g_chatmsg.nickName);
					EnableWindow(hSecondEditNickName, FALSE);
				}
				if (isSecondWhisperState) {
					isMsgSend = true;
					char whisperMessage[124] = { '\0', };

					strcat(whisperMessage, "[");
					strcat(whisperMessage, secondNickName);
					strcat(whisperMessage, "���� �ӼӸ�] : ");
					strcat(whisperMessage, g_chatmsg.buf);

					g_chatmsg.type = RECEIVEFORWHISPER;
					strcpy(g_chatmsg.nickName, tempWhisperSecondNickName);
					strcpy(g_chatmsg.buf, whisperMessage);
				}
				break;
			}
			// ���� �ϷḦ �˸�
			SetEvent(g_hWriteEvent);
			// �Էµ� �ؽ�Ʈ ��ü�� ���� ǥ��
			SendMessage(hEditMsg, EM_SETSEL, 0, -1);
			return TRUE;

		// ù��° ä�ù� ������� ��ư
		case IDC_SHOWFIRST:
			sendNickName = false;
			showNickName = true;

			g_chatmsg.type = REQUESTNICKNAME;

			send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
			DisplayText(g_isChatting, "[ù��° ä�ù� ������ ����մϴ�]\r\n");
			if (!strcmp(firstNickName, "")) {
				DisplayText(g_isChatting, "�г��� �̼���(��)\r\n");
			}
			else {
				DisplayText(g_isChatting, "%s(��)\r\n", firstNickName);
			}
			SetEvent(g_hReadEvent);

			return TRUE;

		// �ι�° ä�ù� ������� ��ư
		case IDC_SHOWSECOND:
			sendNickName = false;
			showNickName = true;

			g_chatmsg.type = REQUESTSECONDNICKNAME;

			send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
			DisplayText(g_isChatting, "[�ι�° ä�ù� ������ ����մϴ�]\r\n");
			if (!strcmp(secondNickName, "")) {
				DisplayText(g_isChatting, "�г��� �̼���(��)\r\n");
			}
			else {
				DisplayText(g_isChatting, "%s(��)\r\n", secondNickName);
			}
			SetEvent(g_hReadEvent);

			return TRUE;

		// �г��� �����ư
		case IDC_NICKNAMECHANGEBTN:
			EnableWindow(g_hButtonSendMsg, FALSE);
			switch (g_isChatting) {
			case FIRST_CHAT:
				if (isNickNameChange || !isInitialNickNameFirstRoom) {
					isNickNameChange = true;
					GetDlgItemText(hDlg, IDC_NICKNAME, g_chatmsg.nickName, MSGSIZE);

					if (strlen(g_chatmsg.nickName) == 0) {
						MessageBox(hDlg, "�г����� ������ �� �� �����ϴ�", "���", MB_ICONERROR);
						return true;
					}

					strcpy(tempNickName, g_chatmsg.nickName);
					g_chatmsg.type = NICKNAMECHANGE;
					send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);

					Sleep(100);

					// �ߺ��� �������
					if (isDuplicationNickName) {
						MessageBox(NULL, "�г����� �ߺ��˴ϴ�.", "���", MB_OK);
						isDuplicationNickName = false;
					}
					else {
						EnableWindow(hEditNickName, FALSE);
						isNickNameChange = false;
						isDuplicationNickName = false;

						// �ӼӸ� ���� ��ҵ� TRUE
						if (isFirstWhisperState) {
							EnableWindow(hEditWhisper, FALSE);
						} else {
							EnableWindow(hEditWhisper, TRUE);
						}
						EnableWindow(btnWhisper, TRUE);

						MessageBox(NULL, "�г����� ���������� ����Ǿ����ϴ�.", "����", MB_OK);
						strcpy(firstNickName, tempNickName);
						isInitialNickNameFirstRoom = true;
						EnableWindow(g_hButtonSendMsg, TRUE);
						isSetFirstNickName = true;
						EnableWindow(btnWhisper, TRUE);
						// �ӼӸ� �κ��� ���۽�, ��Ȱ��ȭ
						if (!isFirstWhisperState) {
							EnableWindow(hEditWhisper, TRUE);
						}
					}
					break;
				}
				isNickNameChange = true;
				EnableWindow(hEditNickName, TRUE);
				EnableWindow(hEditWhisper, FALSE);
				EnableWindow(btnWhisper, FALSE);
				break;

			case SECOND_CHAT:
				EnableWindow(g_hButtonSendMsg, FALSE);
				if (isNickNameChange || !isInitialNickNameSecondRoom) {
					isNickNameChange = true;
					GetDlgItemText(hDlg, IDC_NICKNAME2, g_chatmsg.nickName, MSGSIZE);

					if (strlen(g_chatmsg.nickName) == 0) {
						MessageBox(hDlg, "�г����� ������ �� �� �����ϴ�", "���", MB_ICONERROR);
						return true;
					}

					strcpy(tempNickName, g_chatmsg.nickName);
					g_chatmsg.type = NICKNAMECHANGE;
					send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);

					Sleep(100);

					// �ߺ��� �������
					if (isDuplicationNickName) {
						MessageBox(NULL, "�г����� �ߺ��˴ϴ�.", "���", MB_OK);
						isDuplicationNickName = false;
					}
					else {
						EnableWindow(hSecondEditNickName, FALSE);
						isNickNameChange = false;
						isDuplicationNickName = false;

						// �ӼӸ� ���� ��ҵ� ó��
						if (isFirstWhisperState) {
							EnableWindow(hEditSecondWhisper, FALSE);
						}
						else {
							EnableWindow(hEditSecondWhisper, TRUE);
						}
						EnableWindow(btnSecondWhisper, TRUE);

						MessageBox(NULL, "�г����� ���������� ����Ǿ����ϴ�.", "����", MB_OK);
						strcpy(secondNickName, tempNickName);
						isInitialNickNameSecondRoom = true;
						EnableWindow(g_hButtonSendMsg, TRUE);
						isSetSecondNickName = true;
						EnableWindow(btnSecondWhisper, TRUE);
						// �ӼӸ� �κ��� �������� ���� ��, Ȱ��ȭ
						if (!isSecondWhisperState) {
							EnableWindow(hEditSecondWhisper, TRUE);
						}
					}
					break;
				}
				isNickNameChange = true;
				EnableWindow(hSecondEditNickName, TRUE);
				EnableWindow(hEditSecondWhisper, FALSE);
				EnableWindow(btnSecondWhisper, FALSE);
				break;
			}
			return TRUE;

		// �ӼӸ� ��ư
		case IDC_WHISPERBUTTON:
			if (isFirstWhisperButtonChk || !isInitialWhisperFirst) {
				g_chatmsg.type = IDCHECKFORWHISPER;
				GetDlgItemText(hDlg, IDC_WHISPERTEXTBOX, g_chatmsg.nickName, MSGSIZE);

				if (!strcmp(g_chatmsg.nickName, firstNickName)) {
					MessageBox(hDlg, "�ڱ� �ڽ����״� �ӼӸ��� �� �� �����ϴ�", "���!", MB_ICONERROR);
					return TRUE;
				}
				else if (strlen(g_chatmsg.nickName) == 0) {
					MessageBox(hDlg, "�ӼӸ� �� ����� ������ �� �� �����ϴ�", "���!", MB_ICONERROR);
					return TRUE;
				}

				send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
				isJudgeOwnerWhisper = true;

				Sleep(100);

				if (!isDuplicationForWhisper) {
					MessageBox(hDlg, "�г����� �������� �ʽ��ϴ�!", "Error", MB_ICONERROR);
					EnableWindow(g_hButtonSendMsg, TRUE);
					return TRUE;
				}
				else {
					MessageBox(hDlg, "�ӼӸ� ���·� ��ȯ�˴ϴ� :)", "����", MB_OK);
					isInitialWhisperFirst = true;
					isFirstWhisperButtonChk = false;
					isDuplicationForWhisper = false;
					isFirstWhisperState = true;
					EnableWindow(hEditWhisper, FALSE);
					return TRUE;
				}
			}
			isFirstWhisperState = false;
			isDuplicationForWhisper = false;
			isFirstWhisperButtonChk = true;
			isJudgeOwnerWhisper = false;
			EnableWindow(hEditWhisper, TRUE);
			return TRUE;

		// �ι�° �ӼӸ� ��ư
		case IDC_BTNSECONDWHISPER:
			if (isSecondWhisperButtonChk || !isInitialWhisperSecond) {
				g_chatmsg.type = IDCHECKFORWHISPER;
				GetDlgItemText(hDlg, IDC_SECONDWHISPERTEXTBOX, g_chatmsg.nickName, MSGSIZE);

				if (!strcmp(g_chatmsg.nickName, secondNickName)) {
					MessageBox(hDlg, "�ڱ� �ڽ����״� �ӼӸ��� �� �� �����ϴ�", "���!", MB_ICONERROR);
					return TRUE;
				}
				else if (strlen(g_chatmsg.nickName) == 0) {
					MessageBox(hDlg, "�ӼӸ� �� ����� ������ �� �� �����ϴ�", "���!", MB_ICONERROR);
					return TRUE;
				}

				send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
				isJudgeOwnerWhisper = true;

				Sleep(100);

				if (!isDuplicationForWhisper) {
					MessageBox(hDlg, "�г����� �������� �ʽ��ϴ�!", "Error", MB_ICONERROR);
					EnableWindow(g_hButtonSendMsg, TRUE);
					return TRUE;
				}
				else {
					MessageBox(hDlg, "�ӼӸ� ���·� ��ȯ�˴ϴ� :)", "����", MB_OK);
					isInitialWhisperSecond = true;
					isSecondWhisperButtonChk = false;
					isDuplicationForWhisper = false;
					isSecondWhisperState = true;
					EnableWindow(hEditSecondWhisper, FALSE);
					return TRUE;
				}
			}
			isSecondWhisperState = false;
			isDuplicationForWhisper = false;
			isSecondWhisperButtonChk = true;
			isJudgeOwnerWhisper = false;
			EnableWindow(hEditSecondWhisper, TRUE);
			return TRUE;

		case IDCANCEL:
			if(MessageBox(hDlg, "������ �����Ͻðڽ��ϱ�?",
				"����", MB_YESNO|MB_ICONQUESTION) == IDYES)
			{
				closesocket(g_sock);
				EndDialog(hDlg, IDCANCEL);
			}
			return TRUE;

		}
		return FALSE;
	}

	return FALSE;
}

// ���� ��� ������ �Լ�
DWORD WINAPI ClientMain(LPVOID arg)
{
	int retval;

	if(g_isIPv6 == false){
		// socket()
		g_sock = socket(AF_INET, SOCK_STREAM, 0);
		if(g_sock == INVALID_SOCKET) err_quit("socket()");

		// connect()
		SOCKADDR_IN serveraddr;
		ZeroMemory(&serveraddr, sizeof(serveraddr));
		serveraddr.sin_family = AF_INET;
		serveraddr.sin_addr.s_addr = inet_addr(g_ipaddr);
		serveraddr.sin_port = htons(g_port);
		retval = connect(g_sock, (SOCKADDR *)&serveraddr, sizeof(serveraddr));
		if(retval == SOCKET_ERROR) err_quit("connect()");
	}
	else{
		// socket()
		g_sock = socket(AF_INET6, SOCK_STREAM, 0);
		if(g_sock == INVALID_SOCKET) err_quit("socket()");

		// connect()
		SOCKADDR_IN6 serveraddr;
		ZeroMemory(&serveraddr, sizeof(serveraddr));
		serveraddr.sin6_family = AF_INET6;
		int addrlen = sizeof(serveraddr);
		WSAStringToAddress(g_ipaddr, AF_INET6, NULL,
			(SOCKADDR *)&serveraddr, &addrlen);
		serveraddr.sin6_port = htons(g_port);
		retval = connect(g_sock, (SOCKADDR *)&serveraddr, sizeof(serveraddr));
		if(retval == SOCKET_ERROR) err_quit("connect()");
	}
	MessageBox(NULL, "������ �����߽��ϴ�.", "����!", MB_ICONINFORMATION);
	MessageBox(NULL, "ù��° ä�ù����� �����մϴ�", "����!", MB_ICONINFORMATION);

	// �б� & ���� ������ ����
	HANDLE hThread[2];
	hThread[0] = CreateThread(NULL, 0, ReadThread, NULL, 0, NULL);
	hThread[1] = CreateThread(NULL, 0, WriteThread, NULL, 0, NULL);
	if(hThread[0] == NULL || hThread[1] == NULL){
		MessageBox(NULL, "�����带 ������ �� �����ϴ�."
			"\r\n���α׷��� �����մϴ�.",
			"����!", MB_ICONERROR);
		exit(1);
	}

	g_bStart = TRUE;

	// ������ ���� ���
	retval = WaitForMultipleObjects(2, hThread, FALSE, INFINITE);
	retval -= WAIT_OBJECT_0;
	if(retval == 0)
		TerminateThread(hThread[1], 1);
	else
		TerminateThread(hThread[0], 1);
	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);

	g_bStart = FALSE;

	MessageBox(NULL, "������ ������ �������ϴ�", "�˸�", MB_ICONINFORMATION);
	EnableWindow(g_hButtonSendMsg, FALSE);

	closesocket(g_sock);
	return 0;
}

// ������ �ޱ�
DWORD WINAPI ReadThread(LPVOID arg)
{
	int retval;
	COMM_MSG comm_msg;
	CHAT_MSG *chat_msg;

	while(1){
		retval = recvn(g_sock, (char *)&comm_msg, BUFSIZE, 0);
		if(retval == 0 || retval == SOCKET_ERROR){
			break;
		}
		if(comm_msg.type == CHATTING){
			chat_msg = (CHAT_MSG *)&comm_msg;
			DisplayText(chat_msg->chatMode, "[%s] : %s \r\n", chat_msg->nickName, chat_msg->buf);			
		}
		else if (comm_msg.type == NICKNAMECHANGE || comm_msg.type == IDCHECKFORWHISPER) {
			chat_msg = (CHAT_MSG *)&comm_msg;
			if (!strcmp(chat_msg->nickName, firstNickName) || !strcmp(chat_msg->nickName, secondNickName)) {
				if (comm_msg.type == NICKNAMECHANGE) {
					g_chatmsg.type = SAMENICKNAME;
				}
				else if (comm_msg.type == IDCHECKFORWHISPER) {
					g_chatmsg.type = SAMENICKNAMEFORWHISPER;
				}
				send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
			}
		}
		else if (comm_msg.type == SAMENICKNAME && isNickNameChange) {
			isDuplicationNickName = true;
		}
		else if (comm_msg.type == SAMENICKNAMEFORWHISPER && isJudgeOwnerWhisper) {
			isDuplicationForWhisper = true;
		}
		else if (comm_msg.type == RECEIVEFORWHISPER) {
			chat_msg = (CHAT_MSG *)&comm_msg;
			if (isFirstWhisperState && isMsgSend && (g_isChatting == FIRST_CHAT)) {
				DisplayText(g_isChatting, "[%s �Կ��� ������ �ӼӸ�] : %s\r\n", tempWhisperNickName, tempWhisperChatMsg);
				isMsgSend = false;
				if (!strcmp(tempWhisperNickName, secondNickName)) {
					DisplayText(SECOND_CHAT, "%s\r\n", chat_msg->buf);
				}
			}
			else if (isSecondWhisperState && isMsgSend) {
				DisplayText(g_isChatting, "[%s �Կ��� ������ �ӼӸ�] : %s\r\n", tempWhisperSecondNickName, tempWhisperSecondChatMsg);
				isMsgSend = false;
				if (!strcmp(tempWhisperSecondNickName, firstNickName)) {
					DisplayText(FIRST_CHAT, "%s\r\n", chat_msg->buf);
				}
			}
			else {
				if (!strcmp(chat_msg->nickName, firstNickName)) {
					DisplayText(FIRST_CHAT, "%s\r\n", chat_msg->buf);
				}
				else if (!strcmp(chat_msg->nickName, secondNickName)) {
					DisplayText(SECOND_CHAT, "%s\r\n", chat_msg->buf);
				}
			}
		}
		// ���� ��û���� Ŭ���̾�Ʈ���� �޴°�
		else if (comm_msg.type == REQUESTNICKNAME) {
			if (!showNickName) {
				g_chatmsg.type = RECEIVENICKNAME;
				sendNickName = true;
				strcpy(g_chatmsg.nickName, firstNickName);
				send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
			}
		}
		else if (comm_msg.type == REQUESTSECONDNICKNAME) {
			if (!showNickName) {
				g_chatmsg.type = RECEIVENICKNAME;
				sendNickName = true;
				
				if (secondNickName == NULL) {
					strcpy(g_chatmsg.nickName, "[System]�г��� �̼���");
				}
				else {
					strcpy(g_chatmsg.nickName, secondNickName);
				}
				send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
			}
		}
		// ��û���� Ŭ���̾�Ʈ���� �ٽ� �����°�
		else if (comm_msg.type == RECEIVENICKNAME) {
			if (!sendNickName) {
				showNickName = false;
				chat_msg = (CHAT_MSG *)&comm_msg;
				DisplayText(g_isChatting, "%s\r\n", chat_msg->nickName);
			}
		}
	}

	return 0;
}

// ������ ������
DWORD WINAPI WriteThread(LPVOID arg)
{
	int retval;

	// ������ ������ ���
	while(1){
		// ���� �Ϸ� ��ٸ���
		WaitForSingleObject(g_hWriteEvent, INFINITE);

		// ���ڿ� ���̰� 0�̸� ������ ����
		if(strlen(g_chatmsg.buf) == 0){
			// '�޽��� ����' ��ư Ȱ��ȭ
			EnableWindow(g_hButtonSendMsg, TRUE);
			MessageBox(NULL, "�޼����� �ƹ��͵� �Էµ��� �ʾҽ��ϴ�", "�˸�", MB_ICONINFORMATION);
			SetEvent(g_hReadEvent);
			continue;
		}

		switch (g_isChatting)
		{
		case FIRST_CHAT:
			if (!isSetFirstNickName) {
				SetEvent(g_hReadEvent);
				continue;
			}
			break;
		case SECOND_CHAT:
			if (!isSetSecondNickName) {
				SetEvent(g_hReadEvent);
				continue;
			}
			break;
		}

		// ������ ������
		retval = send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
		if(retval == SOCKET_ERROR){
			break;
		}

		// '�޽��� ����' ��ư Ȱ��ȭ
		EnableWindow(g_hButtonSendMsg, TRUE);
		// �б� �Ϸ� �˸���
		SetEvent(g_hReadEvent);
	}

	return 0;
}

// ����Ʈ ��Ʈ�ѿ� ���ڿ� ���
void DisplayText(int chatingMode, char *fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);

	char cbuf[1024];
	vsprintf(cbuf, fmt, arg);

	switch (chatingMode)
	{
		int nLength;
	case FIRST_CHAT:
		nLength = GetWindowTextLength(g_hEditStatus);
		SendMessage(g_hEditStatus, EM_SETSEL, nLength, nLength);
		SendMessage(g_hEditStatus, EM_REPLACESEL, FALSE, (LPARAM)cbuf);
		break;
	case SECOND_CHAT:
		nLength = GetWindowTextLength(g_hSecondEditStatus);
		SendMessage(g_hSecondEditStatus, EM_SETSEL, nLength, nLength);
		SendMessage(g_hSecondEditStatus, EM_REPLACESEL, FALSE, (LPARAM)cbuf);
		break;
	}
	va_end(arg);
}

// ����� ���� ������ ���� �Լ�
int recvn(SOCKET s, char *buf, int len, int flags)
{
	int received;
	char *ptr = buf;
	int left = len;

	while(left > 0){
		received = recv(s, ptr, left, flags);
		if(received == SOCKET_ERROR)
			return SOCKET_ERROR;
		else if(received == 0)
			break;
		left -= received;
		ptr += received;
	}

	return (len - left);
}

// ���� �Լ� ���� ��� �� ����
void err_quit(char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(1);
}

// ���� �Լ� ���� ���
void err_display(char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[%s] %s", msg, (char *)lpMsgBuf);
	LocalFree(lpMsgBuf);
}