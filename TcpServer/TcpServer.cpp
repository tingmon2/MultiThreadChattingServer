#include "stdafx.h"
#include <winsock2.h>
#pragma comment(lib, "ws2_32")

DWORD WINAPI ThreadFunction(LPVOID pParam)
{
	SOCKET hClient = (SOCKET)pParam; // communication socket
	char szBuffer[128] = { 0 }; // buffer size maximum 128 bytes
	int nReceive = 0;

	// 4-2. receive string from client
	// If no error occurs, recv returns the number of bytes received and 
	// the buffer pointed to by the buf parameter will contain this data received. MSN
	while ((nReceive = ::recv(hClient, szBuffer, sizeof(szBuffer), 0)) > 0)
	{
		// 4-3. send the received string back to client 
		::send(hClient, szBuffer, sizeof(szBuffer), 0);
		//puts(szBuffer);
		//fflush(stdout);
		printf("From client: %s\n", szBuffer);
		memset(szBuffer, 0, sizeof(szBuffer)); // clear buffer
	}

	// 4-4. receive disconnection request from client
	::shutdown(hClient, SD_BOTH); 
	::closesocket(hClient); 
	puts("Client disconnected."); 
	fflush(stdout); 

	return 0;
}

int _tmain(int argc, _TCHAR* argv[]) 
{
	// 0. initialize winsock
	WSADATA wsa = { 0 };
	if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		puts("Error: Can not initialize winsock.");
		return 0;
	}

	// 1. create listening socket
	// address family: IPv4, tpye: TCP
	SOCKET hSocket = ::socket(AF_INET, SOCK_STREAM, 0);
	if (hSocket == INVALID_SOCKET)
	{
		puts("Error: Can not create litstening socket.");
		return 0;
	}
	else 
	{
		puts("Socket created...");
		fflush(stdout);
	}

	// 2. port binding
	SOCKADDR_IN svrAddr = { 0 }; // server address
	svrAddr.sin_family = AF_INET; // IPv4
	svrAddr.sin_port = htons(25000); // port number 250000
	svrAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY); // any inbound address ok
	if (::bind(hSocket, (SOCKADDR*)&svrAddr, sizeof(svrAddr)) == SOCKET_ERROR)
	{
		puts("Error: Can not bind IP address and port.");
		return 0;
	}
	else
	{
		puts("Binding success...");
		fflush(stdout);
	}

	// 3. Open the listening socket to accept connection request
	if (::listen(hSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		puts("Error: Socket can not convert to listening state.");
		return 0;
	}
	else
	{
		puts("Server is listening for connection request.");
		fflush(stdout);
	}

	// 4. client request process and respond
	SOCKADDR_IN clientAddr = { 0 }; // client address
	int nAddrLen = sizeof(clientAddr);
	SOCKET hClient = 0; // communication socket
	DWORD dwThreadID = 0;
	HANDLE hThread;

	// 4-1. accept client connection and open communication socket 
	while ((hClient = ::accept(hSocket, (SOCKADDR*)&clientAddr, &nAddrLen)) != INVALID_SOCKET)
	{
		puts("New client has been connected.");
		fflush(stdout);

		hThread = ::CreateThread(
			NULL, // inherit security authority
			0, // defualt stack size
			ThreadFunction, // your thread function 
			(LPVOID)hClient, // thread function parameter
			0, // default flag
			&dwThreadID); // thread ID storing address

		::CloseHandle(hThread);
	}

	// 5. close listening socket
	::closesocket(hSocket);

	// 0. clear winsock
	::WSACleanup();
	return 0;
}