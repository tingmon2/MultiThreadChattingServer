#include "stdafx.h"
#include <winsock2.h>
#include <list>
#include <iostream>
#include <chrono>
#pragma comment(lib, "ws2_32")

typedef struct CLIENTHEALTH
{
	int clientId;
	uint64_t lastPing;
	uint64_t lastPong;

	bool operator==(const CLIENTHEALTH& other) const {
		return clientId == other.clientId;
	}
} CLIENTHEALTH;

CRITICAL_SECTION g_cs; // critical section for thread synchronization
SOCKET g_hSocket; // listening socket
std::list<SOCKET> g_clientList; // linked list of client sockets
std::list<CLIENTHEALTH> g_clientHealthList; // store health check information
std::list<SOCKET> g_clientRemoveList; // list of timeout clients

uint64_t timeSinceEpochMillisec() {
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

CLIENTHEALTH* WINAPI findByClientId(int clientId)
{
	std::list<CLIENTHEALTH>::iterator it;
	::EnterCriticalSection(&g_cs);
	for (it = g_clientHealthList.begin(); it != g_clientHealthList.end(); it++)
	{
		if (it->clientId == clientId)
		{
			::LeaveCriticalSection(&g_cs);
			return &(*it);
		}
	}
	::LeaveCriticalSection(&g_cs);
	return &(*g_clientHealthList.begin());
}

// kind of dummy node in linked list which only indicates the start of list
DWORD WINAPI createDummyHealth()
{
	CLIENTHEALTH newHealth = *(CLIENTHEALTH*)malloc(sizeof(CLIENTHEALTH)); // memory initialize
	newHealth.clientId = 0;
	newHealth.lastPing = 0;
	newHealth.lastPong = 0;
	g_clientHealthList.push_back(newHealth);
	return 0;
}

DWORD setPing(SOCKET client, uint64_t time)
{
	CLIENTHEALTH* health = findByClientId((int)client);
	if (health->clientId != 0)
	{
		health->lastPing = time;
	}
	return 0;
}

DWORD setPong(SOCKET client, uint64_t time)
{
	CLIENTHEALTH* health = findByClientId((int)client);
	if (health->clientId != 0)
	{
		health->lastPong = time;
	}
	return 0;
}

BOOL CtrlHandler(DWORD dwType)
{
	// if Ctrl + C throws Exception look Debug -> Windows -> Exceptions Setting -> Win32 Exceptions - Control-C
	if (dwType == CTRL_C_EVENT)
	{
		std::list<SOCKET>::iterator it;

		::shutdown(g_hSocket, SD_BOTH); // block request

		::EnterCriticalSection(&g_cs);
		for (it = g_clientList.begin(); it != g_clientList.end(); it++)
		{
			::closesocket(*it);
		}
		g_clientList.clear(); 
		g_clientHealthList.clear();
		::LeaveCriticalSection(&g_cs);

		puts("Disconnect all clients and closing server...");
		// this kind of waiting is not good for data critical server.
		// for example, MMORPG server must save every info before closure.
		::Sleep(100); // wait until all client connection close

		DeleteCriticalSection(&g_cs);
		::closesocket(g_hSocket);
		::WSACleanup();
		exit(0);
		return TRUE;
	}
	return FALSE;
}

BOOL AddHealth(SOCKET hClient)
{
	CLIENTHEALTH newHealth = *(CLIENTHEALTH*)malloc(sizeof(CLIENTHEALTH));;
	newHealth.clientId = (int)hClient;
	newHealth.lastPing = 0;
	newHealth.lastPong = 0;

	g_clientHealthList.push_back(newHealth);

	return TRUE;
}

BOOL AddUser(SOCKET hClient)
{	// using critical section means giving up synchronism which undermines the reason of multi threading
	// it could provoke dead lock if server has too many or long critical section. therefore, minimize it.

	::EnterCriticalSection(&g_cs); // critical section start, object(socket list) lock?

	g_clientList.push_back(hClient); // this code will be run by only one thread at a time
	AddHealth(hClient);

	::LeaveCriticalSection(&g_cs); // critical section end

	return TRUE;
}

void BroadCastChattingMessage(char *pszParam)
{
	int mLength = strlen(pszParam);
	std::list<SOCKET>::iterator it;
	::EnterCriticalSection(&g_cs);
	for (it = g_clientList.begin(); it != g_clientList.end(); it++)
	{
		::send(*it, pszParam, mLength + 1, 0);
	}
	::LeaveCriticalSection(&g_cs);
}

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
		if (std::string(szBuffer) == "pong")
		{
			printf("%s\n", "pong received");
			// record pong time
			setPong(hClient, timeSinceEpochMillisec());
		}
		else
		{
			BroadCastChattingMessage(szBuffer);
			memset(szBuffer, 0, sizeof(szBuffer)); // clear buffer
		}
	}

	// 4-4. receive disconnection request from client
	puts("Client disconnected.");
	fflush(stdout);

	::EnterCriticalSection(&g_cs);
	g_clientList.remove(hClient);

	CLIENTHEALTH* removeHealth = findByClientId((int)hClient);
	g_clientHealthList.remove(*removeHealth);

	::LeaveCriticalSection(&g_cs);

	::shutdown(hClient, SD_BOTH);
	::closesocket(hClient);

	return 0;
}

DWORD WINAPI hbThreadFunction(LPVOID pParam)
{
	while (1)
	{
		if (g_clientList.size() > 0)
		{
			std::list<SOCKET>::iterator it;
			::EnterCriticalSection(&g_cs);
			for (it = g_clientList.begin(); it != g_clientList.end(); it++)
			{
				CLIENTHEALTH* clientHealth = findByClientId((int)*it);
				// before send ping, if ping and pong discrepancy is bigger than 3 min, disconnect.
				// if pong is bigger than ping, that is normal -> unsigned will return differ as 4294967295 but it means healthy.
				if (clientHealth->lastPing - clientHealth->lastPong > 60000 * 3
					&& clientHealth->lastPing > clientHealth->lastPong) 
				{
					g_clientRemoveList.push_back(*it); // health check failed timeout
				}
				printf("ping: %u, pong: %u, differ: %u\n", 
					clientHealth->lastPing, clientHealth->lastPong, clientHealth->lastPing - clientHealth->lastPong);
				::send(*it, "ping", 5, 0);
				// record ping time
				setPing(*it, timeSinceEpochMillisec());
			}
			::LeaveCriticalSection(&g_cs);
		}
		if (g_clientRemoveList.size() > 0) // disconnect all timeout sockets
		{
			std::list<SOCKET>::iterator it;
			::EnterCriticalSection(&g_cs);
			for (it = g_clientRemoveList.begin(); it != g_clientRemoveList.end(); it++)
			{
				puts("Client timeout.");
				fflush(stdout);

				g_clientList.remove(*it);
				CLIENTHEALTH* removeHealth = findByClientId((int)*it);
				g_clientHealthList.remove(*removeHealth);

				::shutdown(*it, SD_BOTH);
				::closesocket(*it);
			}
			g_clientRemoveList.clear();
			::LeaveCriticalSection(&g_cs);
		}
		Sleep(60000);
	}
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

	// create critical section object
	::InitializeCriticalSection(&g_cs);

	if (::SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE) == FALSE)
	{
		puts("Error: Can not register console control handler.");
	}

	// 1. create listening socket
	// address family: IPv4, tpye: TCP
	g_hSocket = ::socket(AF_INET, SOCK_STREAM, 0);
	if (g_hSocket == INVALID_SOCKET)
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
	if (::bind(g_hSocket, (SOCKADDR*)&svrAddr, sizeof(svrAddr)) == SOCKET_ERROR)
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
	if (::listen(g_hSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		puts("Error: Socket can not convert to listening state.");
		return 0;
	}
	else
	{
		puts("*** Chatting server is now open and listening ***");
		fflush(stdout);
	}

	createDummyHealth();

	// send heartbeat for every 1 min
	DWORD hbThreadID = 0;
	HANDLE hbThread = ::CreateThread(
		NULL, // inherit security authority
		0, // defualt stack size
		hbThreadFunction, // your thread function 
		0, // thread function parameter
		0, // default flag
		&hbThreadID); // thread ID storing address
	::CloseHandle(hbThread);

	// 4. client request process and respond
	SOCKADDR_IN clientAddr = { 0 }; // client address
	int nAddrLen = sizeof(clientAddr);
	SOCKET hClient = 0; // communication socket
	DWORD dwThreadID = 0;
	HANDLE hThread;

	// 4-1. accept client connection and open communication socket 
	while ((hClient = ::accept(g_hSocket, (SOCKADDR*)&clientAddr, &nAddrLen)) != INVALID_SOCKET)
	{
		if (AddUser(hClient) == FALSE)
		{
			puts("Error: Adding user failed.");
			CtrlHandler(CTRL_C_EVENT);
			break;
		}
		
		puts("New client has been connected.");
		fflush(stdout);

		// create thread once new client is accepted
		hThread = ::CreateThread(
			NULL, // inherit security authority
			0, // defualt stack size
			ThreadFunction, // your thread function 
			(LPVOID)hClient, // thread function parameter
			0, // default flag
			&dwThreadID); // thread ID storing address
		::CloseHandle(hThread);
	}

	return 0;
}