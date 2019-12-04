// Networkpg.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

using namespace std;

volatile bool stopWorking = FALSE;	//TRUE가 되면 프로그램 종료

static const int MaxReceiveLength = 8192;

std::string GetLastErrorAsString(void);

void ProcessSignalAction(int sig_number)
{
	if (sig_number == SIGINT)
		stopWorking = TRUE;
}

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
	//ctl - C 누르면 메인루프 종료
	signal(SIGINT, ProcessSignalAction);

	//using namespace std::chrono_literals;

	try
	{
		//논블록 소켓

		//TCP 연결 각각의 객체
		struct RemoteClient
		{
			SOCKET _socket;	//socket handle
			LPFN_ACCEPTEX _acceptEx;	//acceptex 함수 포인터
			char _receiveBuffer[MaxReceiveLength];
		};
		unordered_map<RemoteClient*, shared_ptr<RemoteClient>> remoteClients;

		//TCP연결을 받는 소켓
		WSADATA w;
		WSAStartup(MAKEWORD(2, 2), &w);

		//소켓 핸들
		SOCKET _listensocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

		//Overlapped receive or accept 할 때 사용되는 overlapped 객체
		// I/O 완료 전까지 보존되야 함.
		WSAOVERLAPPED m_readOverlappedStruct;
		ZeroMemory(&m_readOverlappedStruct, sizeof(m_readOverlappedStruct));

		const char* _addr = "0.0.0.0";
		int _port = 5959;

		sockaddr_in m_ipv4Endpoint;
		memset(&m_ipv4Endpoint, 0, sizeof(m_ipv4Endpoint));
		m_ipv4Endpoint.sin_family = AF_INET;
		inet_pton(AF_INET, _addr, &m_ipv4Endpoint.sin_addr);
		m_ipv4Endpoint.sin_port = htons((uint16_t)_port);

		if (::bind(_listensocket, (sockaddr*)&m_ipv4Endpoint, sizeof(m_ipv4Endpoint)) < 0)
		{
			stringstream _ss;
			_ss << "bind failed : " << GetLastErrorAsString();
			throw Exception(_ss.str().c_str());
		}

		//Nonblocking socket으로 모드를 설정
		u_long val = 1;

		// ioctlsocket : 소켓의 입출력 모드를 변곃아는 함수
		// FIONBIO : 소켓을 봉쇄 혹은 비 봉쇄로 만들기 위한 값
		// val 값이 0이 되면 비 봉쇄, 1이면 봉쇄
		int ret = ioctlsocket(_listensocket, FIONBIO, &val);
		if (ret != 0)
		{
			stringstream _ss;
			_ss << "bind failed : " << GetLastErrorAsString();
			throw Exception(_ss.str().c_str());
		}

		listen(_listensocket, 5000);

		cout << "서버가 시작되었습니다. \n";
		cout << "CTL-C 키를 누르면 프로그램을 종료합니다. \n";

		// leten sockt과 TCP connection socket 모두에 대해서 I/O 가능(avail) 이벤트가 있을 때까지 대기.
		// 그 후, I/O 가능 소켓에 대해 일을 진행.

		//select 의 경우는 입출력 이벤트 발생 시, 넘겨주는 정보가 너무 적은 단점이 있다. 이를 poll을 이용해 극복이 가능.

		// 여기에 넣은 소켓 핸들에 대해 select나 poll을 함.
		// 다만, receive나 accept에 대해서만 처리
		vector<WSAPOLLFD> readFds;
		// 어느 소켓 이 어느 RemoteClient에 대한 것인지 가리킴.
		vector<RemoteClient*> readFdsToRemoteClients;

		while (!stopWorking)
		{
			readFds.reserve(remoteClients.size() + 1);
			readFds.clear();
			readFdsToRemoteClients.reserve(remoteClients.size() + 1);
			readFdsToRemoteClients.clear();

			for (auto i : remoteClients)
			{
				WSAPOLLFD _item;
				_item.events = POLLRDNORM;
				_item.fd = i.second->_socket;
				_item.revents = 0;

				readFds.push_back(_item);
				readFdsToRemoteClients.push_back(i.first);
			}

			//마지막 항목은 리슨 소켓
			WSAPOLLFD _item2;
			_item2.events = POLLRDNORM;
			_item2.fd = _listensocket;
			_item2.revents = 0;
			readFds.push_back(_item2);

			// I/O 가능 이벤트가 있을때까지 대기
			WSAPoll(readFds.data(), (int)readFds.size(), 100);

			// readFds를 수색해 필요한 처리를 함
			int num = 0;
			for (auto readFd : readFds)
			{
				if (readFd.revents != 0)
				{
					if (num == readFds.size() - 1) // 리슨 소켓이면
					{
						// accept 처리
						auto remoteClient = make_shared<RemoteClient>();

						//이미 "클라이언트 연결 들어왔음" 이벤트가 온 상태이므로 그냥 이걸 호출해도 됨
						string _ignore;
						remoteClient->_socket = accept(_listensocket, NULL, 0);
						// nonblick socket으로 모드 설정
						u_long val = 1;
						int ret = ioctlsocket(remoteClient->_socket, FIONBIO, &val);
						if (ret != 0)
						{
							stringstream ss;
							ss << "bind failed : " << GetLastErrorAsString();
							throw Exception(ss.str().c_str());
						}

						// New Client를 목록에 추가
						remoteClients.insert({ remoteClient.get(), remoteClient });

						cout << "Client joined. There are " << remoteClients.size() << " connection \n";
					}
					else //TCP 연결 소켓이면
					{
						// 받은 데이터를 그대로 회신
						RemoteClient* remoteClient = readFdsToRemoteClients[num];

						int ec = (int)recv(remoteClient->_socket, remoteClient->_receiveBuffer, MaxReceiveLength, 0);
						if (ec <= 0)
						{
							//error 혹은 소켓 종료
							//해당 소켓은 제거
							closesocket(remoteClient->_socket);
							remoteClients.erase(remoteClient);

							cout << "Client left. There are " << remoteClients.size() << " connection \n";
						}
						else
						{
							cout << remoteClient->_receiveBuffer << endl;
							//받은 데이터를 그대로 송신.
							//연결된 모든 client에게 송신
							for (auto _connectClient : readFdsToRemoteClients)
							{
								send(_connectClient->_socket, remoteClient->_receiveBuffer, ec, 0);
							}							
						}
					}
				}
				++num;

			}
		}

		// 사용자가 CTL-C를 눌러 루프에서 나감. 모두 종료
		closesocket(_listensocket);
		remoteClients.clear();
	}
	catch (Exception& e)
	{
		cout << "Exception! " << e.what() << endl;
	}
	//=========================================================================
	
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

	//WSADATA w;
	//WSAStartup(MAKEWORD(2, 2), &w);

	////소켓 핸들
	//SOCKET _socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	////Overlapped receive or accept 할 때 사용되는 overlapped 객체
	//// I/O 완료 전까지 보존되야 함.
	//WSAOVERLAPPED m_readOverlappedStruct;
	//ZeroMemory(&m_readOverlappedStruct, sizeof(m_readOverlappedStruct));

	//const char* _addr = "0.0.0.0";
	//int _port = 5959;

	//sockaddr_in m_ipv4Endpoint;
	//memset(&m_ipv4Endpoint, 0, sizeof(m_ipv4Endpoint));
	//m_ipv4Endpoint.sin_family = AF_INET;
	//inet_pton(AF_INET, _addr, &m_ipv4Endpoint.sin_addr);
	//m_ipv4Endpoint.sin_port = htons((uint16_t)_port);

	//if (bind(_socket, (sockaddr*)&m_ipv4Endpoint, sizeof(m_ipv4Endpoint)) < 0)
	//{
	//	stringstream _ss;
	//	_ss << "bind failed : " << GetLastErrorAsString();
	//	throw Exception(_ss.str().c_str());
	//}

	//listen(_socket, 5000);

	//cout << "Server started \n";
	////=========================================================================

	////=========================================================================
	//SOCKET _listenSocket = accept(_socket, NULL, 0);
	//if (_listenSocket == -1)
	//{
	//	stringstream _ss;
	//	_ss << "accept failed : " << GetLastErrorAsString();
	//	throw Exception(_ss.str().c_str());
	//}
	////=========================================================================

	////=========================================================================
	//sockaddr_in _tempEndpoint;
	//socklen_t retLength = sizeof(_tempEndpoint);
	//if (::getpeername(_listenSocket, (sockaddr*)&_tempEndpoint, &retLength) < 0)
	//{
	//	stringstream _ss;
	//	_ss << "getPeerAddr failed : " << GetLastErrorAsString();
	//	throw Exception(_ss.str().c_str());
	//}

	//if (retLength > sizeof(_tempEndpoint))
	//{
	//	stringstream _ss;
	//	_ss << "getPeerAddr buffer overrun : " << retLength;
	//	throw Exception(_ss.str().c_str());
	//}

	//char addrString[1000];
	//addrString[0] = 0;
	//inet_ntop(AF_INET, &_tempEndpoint.sin_addr, addrString, sizeof(addrString) - 1);

	//char finalString[1000];
	//sprintf_s(finalString, 1000, "%s : %d", addrString, htons(_tempEndpoint.sin_port));

	//cout << "Socket from " << finalString << " is accepted. \n";
	////=========================================================================

	//char _receiveBuffer[MaxReceiveLength] = { 0, };
	//while (TRUE)
	//{
	//	string receivedData;
	//	cout << "Receiveing Data...\n";
	//	int result = (int)recv(_listenSocket, _receiveBuffer, MaxReceiveLength, 0);

	//	if (result == 0)
	//	{
	//		cout << "Connection closed. \n";
	//		break;
	//	}
	//	else if (result < 0)
	//	{
	//		cout << "Connect lost : " << GetLastErrorAsString() << endl;
	//	}

	//	cout << "Recevied : " << _receiveBuffer << endl;
	//}

	//closesocket(_listenSocket);
	//	}
	//	catch (Exception& e)
	//	{
	//		cout << "Exception! " << e.what() << endl;
	//	}