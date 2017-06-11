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

#define BUFSIZE     256                    // 전송 메시지 전체 크기
#define MSGSIZE     (BUFSIZE-sizeof(int))  // 채팅 메시지 최대 길이

#define INIT 100
#define CHATTING    1000                   // 메시지 타입 : 채팅
#define NICKNAMECHANGE 2000				   // 메세지 타입 : 닉네임 변경
#define SAMENICKNAME 2001
#define NICKNAMECHECK 2002
#define REQUESTNICKNAME 3000
#define REQUESTSECONDNICKNAME 3100
#define RECEIVENICKNAME 3001

#define WM_DRAWIT   (WM_USER+1)            // 사용자 정의 윈도우 메시지

#define FIRST_CHAT 1					   // 채팅방 : 1번째
#define SECOND_CHAT 2					   // 채팅방 : 2번째

// 공통 메시지 형식
// sizeof(COMM_MSG) == 256
struct COMM_MSG
{
	int  type;
	char dummy[MSGSIZE];
};

// 채팅 메시지 형식
// sizeof(CHAT_MSG) == 256
struct CHAT_MSG
{
	int  type;
	int  chatMode;
	char buf[124];
	char nickName[124];
};

static HINSTANCE     g_hInst; // 응용 프로그램 인스턴스 핸들
static HWND          g_hButtonSendMsg; // '메시지 전송' 버튼
static HWND          g_hEditStatus; // 받은 메시지 출력
static HWND			 g_hSecondEditStatus; // 받은 메세지 출력 두번째 창
static char          g_ipaddr[64]; // 서버 IP 주소
static char			 g_nickNameFirstConnection[124];
static u_short       g_port; // 서버 포트 번호
static BOOL          g_isIPv6; // IPv4 or IPv6 주소?
static HANDLE        g_hClientThread; // 스레드 핸들
static volatile BOOL g_bStart; // 통신 시작 여부
static SOCKET        g_sock; // 클라이언트 소켓
static HANDLE        g_hReadEvent, g_hWriteEvent, g_nickNameEvent; // 이벤트 핸들
static CHAT_MSG      g_chatmsg; // 채팅 메시지 저장

static int			 g_isChatting; // 채팅방 모드

static char			 firstNickName[256] = { '\0', };
static char			 secondNickName[256] = { '\0', };
static char			 tempNickName[256] = { '\0', };
static bool			 showNickName = false;
static bool			 sendNickName = false;

static bool			 isNickNameChange = false;
static bool			 isDuplicationNickName = false;
static bool			 isPassNickName = true;


// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// 소켓 통신 스레드 함수
DWORD WINAPI ClientMain(LPVOID arg);
DWORD WINAPI ReadThread(LPVOID arg);
DWORD WINAPI WriteThread(LPVOID arg);
// 편집 컨트롤 출력 함수
void DisplayText(int chatingMode, char *fmt, ...);
// 사용자 정의 데이터 수신 함수
int recvn(SOCKET s, char *buf, int len, int flags);
// 오류 출력 함수
void err_quit(char *msg);
void err_display(char *msg);

// 메인 함수
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
	// 윈속 초기화
	WSADATA wsa;
	if(WSAStartup(MAKEWORD(2,2), &wsa) != 0) return 1;

	// 이벤트 생성
	g_hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if(g_hReadEvent == NULL) return 1;
	g_hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(g_hWriteEvent == NULL) return 1;
	g_nickNameEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_nickNameEvent == NULL) return 1;

	// 변수 초기화(일부)
	g_chatmsg.type = CHATTING;
	g_chatmsg.chatMode = FIRST_CHAT;

	// 대화상자 생성
	g_hInst = hInstance;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	// 이벤트 제거
	CloseHandle(g_hReadEvent);
	CloseHandle(g_hWriteEvent);
	CloseHandle(g_nickNameEvent);

	// 윈속 종료
	WSACleanup();
	return 0;
}

// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static HWND hEditIPaddr;
	static HWND hEditPort;
	static HWND hButtonConnect;
	static HWND hEditMsg;
	static HWND hEditNickName;
	static HWND hSecondEditNickName;

	// 채팅방 접속하는 버튼 생성
	static HWND btnFirstChatConnect;
	static HWND btnSecondChatConnect;

	// 닉네임 변경하는 버튼
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

		// 채팅방 접속하는 버튼 초기화
		btnFirstChatConnect = GetDlgItem(hDlg, IDC_CHAT1);
		btnSecondChatConnect = GetDlgItem(hDlg, IDC_CHAT2);

		// 닉네임 변경하는 버튼
		btnNickNameChange = GetDlgItem(hDlg, IDC_NICKNAMECHANGEBTN);

		btnShowFirstRoom = GetDlgItem(hDlg, IDC_SHOWFIRST);
		btnShowSecondRoom = GetDlgItem(hDlg, IDC_SHOWSECOND);

		// 컨트롤 초기화
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

		// 윈도우 클래스 등록
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

			// 포트번호 예외처리
			if (g_port < 1024 || g_port > 49151) {
				MessageBox(NULL, "PORT를 제대로 입력하세요", "경고", MB_OK);
				break;
			}

			if (g_chatmsg.nickName[0] == '\0') {
				MessageBox(NULL, "닉네임이 비어있습니다!\n닉네임을 입력해주세요!", "경고", MB_OK);
				break;
			}

			// 소켓 통신 스레드 시작
			g_hClientThread = CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);
			if(g_hClientThread == NULL){
				MessageBox(hDlg, "클라이언트를 시작할 수 없습니다."
					"\r\n프로그램을 종료합니다.", "실패!", MB_ICONERROR);
				EndDialog(hDlg, 0);
			}
			else{
				EnableWindow(hButtonConnect, FALSE);
				while(g_bStart == FALSE); // 서버 접속 성공 기다림
				EnableWindow(hEditIPaddr, FALSE);
				EnableWindow(hEditPort, FALSE);
				EnableWindow(g_hButtonSendMsg, TRUE);
				EnableWindow(btnFirstChatConnect, FALSE);
				EnableWindow(btnSecondChatConnect, TRUE);
				EnableWindow(btnShowFirstRoom, TRUE);
				EnableWindow(btnShowSecondRoom, TRUE);
				SetFocus(hEditMsg);
				g_isChatting = FIRST_CHAT;
				EnableWindow(btnNickNameChange, TRUE);
				EnableWindow(hEditNickName, FALSE);

				g_chatmsg.type = INIT;
				send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
			}
			return TRUE;

		// 첫번째 채팅방 입장버튼을 클릭할 경우

		case IDC_CHAT1:
			ShowWindow(g_hSecondEditStatus, FALSE);
			ShowWindow(g_hEditStatus, TRUE);
			EnableWindow(btnFirstChatConnect, FALSE);
			EnableWindow(btnSecondChatConnect, TRUE);
			ShowWindow(hEditNickName, TRUE);
			ShowWindow(hSecondEditNickName, FALSE);
			g_chatmsg.chatMode = FIRST_CHAT;
			g_isChatting = FIRST_CHAT;
			return TRUE;

		// 첫번째 채팅방 입장버튼을 클릭할 경우
		case IDC_CHAT2:
			ShowWindow(g_hEditStatus, FALSE);
			ShowWindow(g_hSecondEditStatus, TRUE);
			EnableWindow(btnFirstChatConnect, TRUE);
			EnableWindow(btnSecondChatConnect, FALSE);
			ShowWindow(hEditNickName, FALSE);
			ShowWindow(hSecondEditNickName, TRUE);
			g_chatmsg.chatMode = SECOND_CHAT;
			g_isChatting = SECOND_CHAT;
			return TRUE;

		case IDC_SENDMSG:
			// 읽기 완료를 기다림
			WaitForSingleObject(g_hReadEvent, INFINITE);
			g_chatmsg.type = CHATTING;
			if (g_isChatting == FIRST_CHAT) {
				GetDlgItemText(hDlg, IDC_NICKNAME, g_chatmsg.nickName, MSGSIZE);
			}
			else {
				GetDlgItemText(hDlg, IDC_NICKNAME2, g_chatmsg.nickName, MSGSIZE);
				if (g_chatmsg.nickName[0] == '\0') {
					MessageBox(hDlg, "두번째 채팅방의 닉네임을 입력해주세요", "실패!", MB_ICONERROR);
					break;
				}
				else {
					strcpy(secondNickName, g_chatmsg.nickName);
					EnableWindow(hSecondEditNickName, FALSE);
				}
			}
			GetDlgItemText(hDlg, IDC_MSG, g_chatmsg.buf, MSGSIZE);
			// 쓰기 완료를 알림
			SetEvent(g_hWriteEvent);
			// 입력된 텍스트 전체를 선택 표시
			SendMessage(hEditMsg, EM_SETSEL, 0, -1);
			return TRUE;

		// 첫번째 채팅방 유저출력 버튼
		case IDC_SHOWFIRST:
			sendNickName = false;
			showNickName = true;

			g_chatmsg.type = REQUESTNICKNAME;

			send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
			DisplayText(g_isChatting, "[첫번째 채팅방 유저를 출력합니다]\r\n");
			DisplayText(g_isChatting, "%s(나)\r\n", firstNickName);
			SetEvent(g_hReadEvent);

			return TRUE;

		// 두번째 채팅방 유저출력 버튼
		case IDC_SHOWSECOND:
			sendNickName = false;
			showNickName = true;

			g_chatmsg.type = REQUESTSECONDNICKNAME;

			send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
			DisplayText(g_isChatting, "[두번째 채팅방 유저를 출력합니다]\r\n");
			if (secondNickName == NULL) {
				DisplayText(g_isChatting, "닉네임 미설정(나)\r\n");
			}
			else {
				DisplayText(g_isChatting, "%s(나)\r\n", secondNickName);
			}
			SetEvent(g_hReadEvent);

			return TRUE;

		// 닉네임 변경버튼
		case IDC_NICKNAMECHANGEBTN:
			switch (g_isChatting) {
			case FIRST_CHAT:
				if (isNickNameChange) {

					GetDlgItemText(hDlg, IDC_NICKNAME, g_chatmsg.nickName, MSGSIZE);
					strcpy(tempNickName, g_chatmsg.nickName);
					g_chatmsg.type = NICKNAMECHANGE;
					send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);

					Sleep(1000);

					// 중복이 됬을경우
					if (isDuplicationNickName) {
						MessageBox(NULL, "닉네임이 중복됩니다.", "경고", MB_OK);
						isDuplicationNickName = false;
					}
					else {
						EnableWindow(hEditNickName, FALSE);
						isNickNameChange = false;
						isPassNickName = true;
						isDuplicationNickName = false;

						MessageBox(NULL, "닉네임이 정상적으로 변경되었습니다.", "성공", MB_OK);
						switch (g_isChatting)
						{
						case FIRST_CHAT:
							strcpy(firstNickName, tempNickName);
							isPassNickName = false;
							break;
						case SECOND_CHAT:
							strcpy(secondNickName, tempNickName);
							isPassNickName = false;
							break;
						}
					}

					//MessageBox(NULL, "isPassNickName 조건문에 성립하였습니다", "성공!", MB_ICONINFORMATION);
					
						
					/*GetDlgItemText(hDlg, IDC_NICKNAME, g_chatmsg.nickName, MSGSIZE);
					strcpy(firstNickName, g_chatmsg.nickName);c
					isNickNameChange = false;
					MessageBox(NULL, "첫번째 대화방 닉네임 변경에 성공하였습니다 :)", "성공!", MB_ICONINFORMATION);
					EnableWindow(hEditNickName, FALSE);
					break;*/
					break;
				}
				EnableWindow(hEditNickName, TRUE);
				isNickNameChange = true;
				break;

			case SECOND_CHAT:
				if (isNickNameChange) {
					GetDlgItemText(hDlg, IDC_NICKNAME2, g_chatmsg.nickName, MSGSIZE);
					strcpy(secondNickName, g_chatmsg.nickName);
					isNickNameChange = false;
					MessageBox(NULL, "두번째 대화방 닉네임 변경에 성공하였습니다 :)", "성공!", MB_ICONINFORMATION);
					EnableWindow(hSecondEditNickName, FALSE);
					break;
				}
				EnableWindow(hSecondEditNickName, TRUE);
				isNickNameChange = true;

				break;
			}

			return TRUE;

		case IDCANCEL:
			if(MessageBox(hDlg, "정말로 종료하시겠습니까?",
				"질문", MB_YESNO|MB_ICONQUESTION) == IDYES)
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

// 소켓 통신 스레드 함수
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
	MessageBox(NULL, "서버에 접속했습니다.", "성공!", MB_ICONINFORMATION);
	MessageBox(NULL, "첫번째 채팅방으로 접속합니다", "성공!", MB_ICONINFORMATION);

	// 읽기 & 쓰기 스레드 생성
	HANDLE hThread[2];
	hThread[0] = CreateThread(NULL, 0, ReadThread, NULL, 0, NULL);
	hThread[1] = CreateThread(NULL, 0, WriteThread, NULL, 0, NULL);
	if(hThread[0] == NULL || hThread[1] == NULL){
		MessageBox(NULL, "스레드를 시작할 수 없습니다."
			"\r\n프로그램을 종료합니다.",
			"실패!", MB_ICONERROR);
		exit(1);
	}

	g_bStart = TRUE;

	// 스레드 종료 대기
	retval = WaitForMultipleObjects(2, hThread, FALSE, INFINITE);
	retval -= WAIT_OBJECT_0;
	if(retval == 0)
		TerminateThread(hThread[1], 1);
	else
		TerminateThread(hThread[0], 1);
	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);

	g_bStart = FALSE;

	MessageBox(NULL, "서버가 접속을 끊었습니다", "알림", MB_ICONINFORMATION);
	EnableWindow(g_hButtonSendMsg, FALSE);

	closesocket(g_sock);
	return 0;
}

// 데이터 받기
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
		else if (comm_msg.type == NICKNAMECHANGE) {
			chat_msg = (CHAT_MSG *)&comm_msg;
			if (!strcmp(chat_msg->nickName, firstNickName) || !strcmp(chat_msg->nickName, secondNickName)) {
				g_chatmsg.type = SAMENICKNAME;
				send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
			}
		}
		else if (comm_msg.type == SAMENICKNAME && isNickNameChange) {
			isDuplicationNickName = true;
		}
		//else if (comm_msg.type == NICKNAMECHECK && isNickNameChange) {
		//	//MessageBox(NULL, "닉네임 체크부분에 진입하였습니다.", "알림", MB_OK);
		//	if (isDuplicationNickName) {
		//		MessageBox(NULL, "닉네임이 중복됩니다.", "경고", MB_OK);
		//	}
		//	else {
		//		MessageBox(NULL, "닉네임이 정상적으로 변경되었습니다.", "성공", MB_OK);
		//		switch (g_isChatting)
		//		{
		//		case FIRST_CHAT:
		//			strcpy(firstNickName, tempNickName);
		//			isPassNickName = false;
		//			break;
		//		case SECOND_CHAT:
		//			strcpy(secondNickName, tempNickName);
		//			isPassNickName = false;
		//			break;
		//		}
		//	}
		//}
		// 여긴 요청당한 클라이언트들이 받는곳
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
					strcpy(g_chatmsg.nickName, "[System]닉네임 미설정");
				}
				else {
					strcpy(g_chatmsg.nickName, secondNickName);
				}
				send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
			}
		}
		// 요청당한 클라이언트들이 다시 보내는곳
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

// 데이터 보내기
DWORD WINAPI WriteThread(LPVOID arg)
{
	int retval;

	// 서버와 데이터 통신
	while(1){
		// 쓰기 완료 기다리기
		WaitForSingleObject(g_hWriteEvent, INFINITE);

		// 문자열 길이가 0이면 보내지 않음
		if(strlen(g_chatmsg.buf) == 0){
			// '메시지 전송' 버튼 활성화
			EnableWindow(g_hButtonSendMsg, TRUE);
			MessageBox(NULL, "메세지가 아무것도 입력되지 않았습니다", "알림", MB_ICONINFORMATION);
			// 읽기 완료 알리기
			SetEvent(g_hReadEvent);
			continue;
		}

		//// 닉네임 길이가 0이면 보내지 않음
		//if (strlen(g_chatmsg.nickname) == 0) {
		//	MessageBox(NULL, "닉네임을 입력해주시기 바랍니다", "알림", MB_ICONINFORMATION);
		//	SetEvent(g_hReadEvent);
		//	continue;
		//}

		// 데이터 보내기
		retval = send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
		if(retval == SOCKET_ERROR){
			break;
		}

		// '메시지 전송' 버튼 활성화
		EnableWindow(g_hButtonSendMsg, TRUE);
		// 읽기 완료 알리기
		SetEvent(g_hReadEvent);
	}

	return 0;
}

// 에디트 컨트롤에 문자열 출력
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

// 사용자 정의 데이터 수신 함수
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

// 소켓 함수 오류 출력 후 종료
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

// 소켓 함수 오류 출력
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