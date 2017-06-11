#pragma comment(lib, "ws2_32")
#define _WINSOCK_DEPRECATED_NO_WARNINGS 

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include "resource.h"

#define SERVERIPV4  "127.0.0.1"
#define SERVERIPV6  "::1"
#define SERVERPORT  9000

#define BUFSIZE     256                    // ���� �޽��� ��ü ũ��
#define MSGSIZE     (BUFSIZE-sizeof(int))  // ä�� �޽��� �ִ� ����

#define CHATTING    1000                   // �޽��� Ÿ�� : ä��
#define NICKNAMECHANGE 2000				   // �޼��� Ÿ�� : �г��� ����
#define REQUESTNICKNAME 3000
#define RECEIVENICKNAME 3001

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

static int			 g_isChating; // ä�ù� ���

static char*		 nickName[2];
static bool			 showNickName = false;
static bool			 sendNickName = false;


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

	static HWND btnShowFirstRoom;
	static HWND btnShowSecondRoom;


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

		btnShowFirstRoom = GetDlgItem(hDlg, IDC_SHOWFIRST);
		btnShowSecondRoom = GetDlgItem(hDlg, IDC_SHOWSECOND);

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

			nickName[0] = g_chatmsg.nickName;

			// ��Ʈ��ȣ ����ó��
			if (g_port < 1024 || g_port > 49151) {
				MessageBox(NULL, "PORT�� ����� �Է��ϼ���", "���", MB_OK);
				break;
			}

			if (g_chatmsg.nickName[0] == '\0') {
				MessageBox(NULL, "�г����� ����ֽ��ϴ�!\n�г����� �Է����ּ���!", "���", MB_OK);
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
				SetFocus(hEditMsg);
				g_isChating = FIRST_CHAT;
				EnableWindow(btnNickNameChange, TRUE);
				EnableWindow(hEditNickName, FALSE);
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
			g_chatmsg.chatMode = FIRST_CHAT;
			g_isChating = FIRST_CHAT;
			return TRUE;

		// ù��° ä�ù� �����ư�� Ŭ���� ���
		case IDC_CHAT2:
			ShowWindow(g_hEditStatus, FALSE);
			ShowWindow(g_hSecondEditStatus, TRUE);
			EnableWindow(btnFirstChatConnect, TRUE);
			EnableWindow(btnSecondChatConnect, FALSE);
			ShowWindow(hEditNickName, FALSE);
			ShowWindow(hSecondEditNickName, TRUE);
			g_chatmsg.chatMode = SECOND_CHAT;
			g_isChating = SECOND_CHAT;
			return TRUE;

		case IDC_SENDMSG:
			// �б� �ϷḦ ��ٸ�
			WaitForSingleObject(g_hReadEvent, INFINITE);
			g_chatmsg.type = CHATTING;
			if (g_isChating == FIRST_CHAT) {
				GetDlgItemText(hDlg, IDC_NICKNAME, g_chatmsg.nickName, MSGSIZE);
			}
			else {
				GetDlgItemText(hDlg, IDC_NICKNAME2, g_chatmsg.nickName, MSGSIZE);
			}
			GetDlgItemText(hDlg, IDC_MSG, g_chatmsg.buf, MSGSIZE);
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
			DisplayText(g_isChating, "[ù��° ä�ù� ������ ����մϴ�]\r\n");
			DisplayText(g_isChating, "%s(��)\r\n", nickName[0]);
			SetEvent(g_hReadEvent);

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
		// ���� ��û���� Ŭ���̾�Ʈ���� �޴°�
		else if (comm_msg.type == REQUESTNICKNAME) {
			if (!showNickName) {
				g_chatmsg.type = RECEIVENICKNAME;
				sendNickName = true;
				strcpy(g_chatmsg.nickName, nickName[0]);
				send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
			}
		}
		// ��û���� Ŭ���̾�Ʈ���� �ٽ� �����°�
		else if (comm_msg.type == RECEIVENICKNAME) {
			if (!sendNickName) {
				showNickName = false;
				chat_msg = (CHAT_MSG *)&comm_msg;
				DisplayText(chat_msg->chatMode, "%s\r\n", chat_msg->nickName);
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
			// �б� �Ϸ� �˸���
			SetEvent(g_hReadEvent);
			continue;
		}

		//// �г��� ���̰� 0�̸� ������ ����
		//if (strlen(g_chatmsg.nickname) == 0) {
		//	MessageBox(NULL, "�г����� �Է����ֽñ� �ٶ��ϴ�", "�˸�", MB_ICONINFORMATION);
		//	SetEvent(g_hReadEvent);
		//	continue;
		//}

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