// windup.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "windup.h"
#include "WinDesktopDup.h"

WinDesktopDup dup;
SOCKET serverSocket;
SOCKET workingSocket;

void Timerproc(HWND Arg1, UINT Arg2, UINT_PTR Arg3, DWORD Arg4) {
	dup.CaptureNext();
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int    nCmdShow) {
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	
	WSADATA wsaData;
	auto iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		throw std::runtime_error(tsf::fmt("WSAStartup error, code: %d\n", iResult));
	}

	serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSocket == INVALID_SOCKET) {
		throw std::runtime_error(tsf::fmt("socket error, code: %d\n", WSAGetLastError()));
	}

	sockaddr_in serverAddr;
	serverAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	serverAddr.sin_port = htons(3300);
	serverAddr.sin_family = AF_INET;
	if (bind(serverSocket, (sockaddr*)& serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		throw std::runtime_error(tsf::fmt("bind error, code: %d\n", WSAGetLastError()));
	}

	if (listen(serverSocket, 5) == SOCKET_ERROR) {
		throw std::runtime_error(tsf::fmt("listen error, code: %d\n", WSAGetLastError()));
	}

	sockaddr_in clientAddr;
	int clientAddrLen = sizeof(clientAddr);
	workingSocket = accept(serverSocket, (sockaddr*)& clientAddr, &clientAddrLen);
	if (workingSocket == INVALID_SOCKET) {
		throw std::runtime_error(tsf::fmt("accept error, code: %d\n", WSAGetLastError()));
	}

	auto err = dup.Initialize();
	if (err != "") {
		throw std::runtime_error(err);
	}

	SetTimer(NULL, 1, 16, Timerproc);

	// Main message loop:
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return (int)msg.wParam;
}

void SendNBytes(const void* buf, int n) {
	int remainedBytesCount = n;
	auto ptr = (uint8_t*)buf;
	while (remainedBytesCount) {
		int sentBytesCount;
		while ((sentBytesCount = (int)send(workingSocket, (char*)ptr, (size_t)remainedBytesCount, 0)) == -1) {
			OutputDebugStringA(tsf::fmt("send error, code: %d\n", WSAGetLastError()).c_str());
			sockaddr_in clientAddr;
			int clientAddrLen = sizeof(clientAddr);
			workingSocket = accept(serverSocket, (sockaddr*)& clientAddr, &clientAddrLen);
			if (workingSocket == INVALID_SOCKET) {
				throw std::runtime_error(tsf::fmt("accept error, code: %d\n", WSAGetLastError()));
			}
		}
		OutputDebugStringA(tsf::fmt("send %d bytes\n", sentBytesCount).c_str());
		remainedBytesCount -= sentBytesCount;
		ptr += sentBytesCount;
	}
}
