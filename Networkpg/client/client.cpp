// client.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

using namespace std;

static const int MaxReceiveLength = 8192;
static const int MAX_MSG_LEN = 256;

const int ClientNum = 10;

std::string GetLastErrorAsString(void);

void TCP_many_client(void);

class Exception : public std::exception
{
public:
	Exception(const std::string& _str)
	{
		m_str = _str;
	}

	~Exception(void) {};

	std::string m_str;

	const char* what() { return m_str.c_str(); }
};

int main()
{
	try
	{
		WSADATA wsadata;
		WSAStartup(MAKEWORD(2, 2), &wsadata);

		SOCKET _socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

		WSAOVERLAPPED m_readOverlappedStruct;
		ZeroMemory(&m_readOverlappedStruct, sizeof(m_readOverlappedStruct));

		const char* _addr = "127.0.0.1";
		int _port = 5959;

		SOCKADDR_IN _servaddr;
		memset(&_servaddr, 0, sizeof(_servaddr));
		_servaddr.sin_family = AF_INET;
		inet_pton(AF_INET, _addr, &_servaddr.sin_addr);
		_servaddr.sin_port = htons((uint16_t)_port);

		if (connect(_socket, (sockaddr*)&_servaddr, sizeof(_servaddr)) < 0)
		{
			stringstream _ss;
			_ss << "connect failed : " << GetLastErrorAsString();
			throw Exception(_ss.str().c_str());
		}

		shared_ptr<thread> recvThreadPoint = make_shared<thread>([&_socket]() 
		{
			SOCKET _sock = _socket;
			char msg[MaxReceiveLength];

			//SOCKADDR_IN cliaddr = { 0, };
			//int len = sizeof(cliaddr);

			while ((int)recv(_sock, msg, MaxReceiveLength, 0) > 0)
			{
				cout << msg << "\n";
			}
			closesocket(_sock);
		});

		char _msg[MaxReceiveLength] = { 0, };
		while (TRUE)
		{
			cin.getline(_msg, MaxReceiveLength);
			
			send(_socket, _msg, strlen(_msg) + 1, 0);

			if (strcmp(_msg, "exit") == 0)
				break;
		}

		closesocket(_socket);
	}
	catch (Exception e)
	{
		cout << "A TCP socket work failed : " << e.what() << endl;
	}

	WSACleanup();

    return 0;
}

std::string GetLastErrorAsString(void)
{
	DWORD errorMessageID = ::GetLastError();
	if (errorMessageID == 0)
		return std::string();

	LPSTR messageBuffer = nullptr;
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

	std::string message(messageBuffer, size);

	LocalFree(messageBuffer);

	return message;
}

void TCP_many_client(void)
{
	//프로세스 우선순위를 의도적으로 낮춤.
	SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);	//실무에선 쓰지 않는 코드

	recursive_mutex mutex;	//변수 보호
	vector<shared_ptr<thread>> threads;
	int64_t totalReceiveBytes = 0;
	int connectedClientCount = 0;

	for (int i = 0; i < ClientNum; ++i)
	{
		shared_ptr<thread> th = make_shared<thread>([&connectedClientCount, &mutex, &totalReceiveBytes]()
		{
			try
			{
				//=========================================================================
				WSADATA w;
				WSAStartup(MAKEWORD(2, 2), &w);

				//소켓 핸들
				SOCKET _socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

				//Overlapped receive or accept 할 때 사용되는 overlapped 객체
				// I/O 완료 전까지 보존되야 함.
				WSAOVERLAPPED m_readOverlappedStruct;
				ZeroMemory(&m_readOverlappedStruct, sizeof(m_readOverlappedStruct));

				sockaddr_in _anyEndpoint;
				memset(&_anyEndpoint, 0, sizeof(_anyEndpoint));
				_anyEndpoint.sin_family = AF_INET;

				if (::bind(_socket, (sockaddr*)&_anyEndpoint, sizeof(_anyEndpoint)) < 0)
				{
					stringstream _ss;
					_ss << "bind failed : " << GetLastErrorAsString();
					throw Exception(_ss.str().c_str());
				}

				const char* _addr = "127.0.0.1";
				int _port = 5959;

				sockaddr_in m_ipv4Endpoint;
				memset(&m_ipv4Endpoint, 0, sizeof(m_ipv4Endpoint));
				m_ipv4Endpoint.sin_family = AF_INET;
				inet_pton(AF_INET, _addr, &m_ipv4Endpoint.sin_addr);
				m_ipv4Endpoint.sin_port = htons((uint16_t)_port);

				if (connect(_socket, (sockaddr*)&m_ipv4Endpoint, sizeof(m_ipv4Endpoint)) < 0)
				{
					stringstream _ss;
					_ss << "connect failed : " << GetLastErrorAsString();
					throw Exception(_ss.str().c_str());
				}
				//=========================================================================

				{
					lock_guard<recursive_mutex> lock(mutex);
					++connectedClientCount;
				}

				string receivedData;
				char _receiveBuffer[MaxReceiveLength] = { 0, };
				while (TRUE)
				{
					//=========================================================================
					const char* dataToSend = "hello world";

					::send(_socket, dataToSend, strlen(dataToSend) + 1, 0);

					int receiveLength = (int)recv(_socket, _receiveBuffer, MaxReceiveLength, 0);
					if (receiveLength <= 0)	//socket connect에 문제가 생김, 루프를 종료
						break;

					lock_guard<recursive_mutex> lock(mutex);
					totalReceiveBytes += receiveLength;
					//=========================================================================
				}
			}
			catch (Exception& e)
			{
				lock_guard<recursive_mutex> lock(mutex);
				cout << "A TCP socket work failed : " << e.what() << endl;
			}

			lock_guard<recursive_mutex> lock(mutex);
			--connectedClientCount;
		});

		lock_guard<recursive_mutex> lock(mutex);
		threads.push_back(th);
	}

	//main thread는 매 초마다 총 송수신량을 출력.
	while (TRUE)
	{
		{
			lock_guard<recursive_mutex> lock(mutex);
			cout << "Total eched bytes : " << (uint64_t)totalReceiveBytes << ", thread count : " << connectedClientCount << endl;
		}

		this_thread::sleep_for(2s);
	}
}