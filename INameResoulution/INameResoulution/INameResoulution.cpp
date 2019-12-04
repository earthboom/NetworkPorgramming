// INameResoulution.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#pragma comment(lib, "ws2_32.lib")

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>

#include <winsock2.h>
#include <stdlib.h>
#include <stdio.h>

//소켓 함수 오류 출력
void err_display(char* msg)
{
	LPVOID lpMsgBuf;

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);

	std::printf(" [%s] %s", msg, (LPCTSTR)lpMsgBuf);
	LocalFree(lpMsgBuf);
}

//도메인 이름 ->IP 주소
BOOL GetIPAddr(char* name, IN_ADDR* addr)
{
	HOSTENT* ptr = gethostbyname(name);
	if (ptr == NULL)
	{
		char msg[] = "gethostbyname() ";
		err_display(msg);
		return FALSE;
	}

	memcpy(addr, ptr->h_addr, ptr->h_length);
	return TRUE;
}

//IP주소 -> 도메인 이름
BOOL GetDomainName(IN_ADDR addr, char* name)
{
	HOSTENT* ptr = gethostbyaddr((char*)&addr, sizeof(addr), AF_INET);
	if (ptr == NULL)
	{
		char msg[] = "gethostbyaddr() ";
		err_display(msg);
		return FALSE;
	}

	strcpy_s(name, 256, ptr->h_name);
	return TRUE;
}

int main(int argc, char* argv[])
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return -1;

	//도메인 이름 -> IP주소
	IN_ADDR addr;

	if (GetIPAddr("www.google.co.kr", &addr))
	{
		// 성공이면 결과 출력
		printf(" IP 주소 = %s\n", inet_ntoa(addr));

		//IP 주소 -> 도메인 이름
		char name[256];
		if (GetDomainName(addr, name))
			printf(" 도메인 이름 = %s\n", name);
	}

	WSACleanup();
	return 0;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
