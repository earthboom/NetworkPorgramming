// Networkpg.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

using namespace std;

volatile bool stopWorking = FALSE;	//TRUE�� �Ǹ� ���α׷� ����

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
	//ctl - C ������ ���η��� ����
	signal(SIGINT, ProcessSignalAction);

	//using namespace std::chrono_literals;

	try
	{
		//���� ����

		//TCP ���� ������ ��ü
		struct RemoteClient
		{
			SOCKET _socket;	//socket handle
			LPFN_ACCEPTEX _acceptEx;	//acceptex �Լ� ������
			char _receiveBuffer[MaxReceiveLength];
		};
		unordered_map<RemoteClient*, shared_ptr<RemoteClient>> remoteClients;

		//TCP������ �޴� ����
		WSADATA w;
		WSAStartup(MAKEWORD(2, 2), &w);

		//���� �ڵ�
		SOCKET _listensocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

		//Overlapped receive or accept �� �� ���Ǵ� overlapped ��ü
		// I/O �Ϸ� ������ �����Ǿ� ��.
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

		//Nonblocking socket���� ��带 ����
		u_long val = 1;

		// ioctlsocket : ������ ����� ��带 ���ɾƴ� �Լ�
		// FIONBIO : ������ ���� Ȥ�� �� ����� ����� ���� ��
		// val ���� 0�� �Ǹ� �� ����, 1�̸� ����
		int ret = ioctlsocket(_listensocket, FIONBIO, &val);
		if (ret != 0)
		{
			stringstream _ss;
			_ss << "bind failed : " << GetLastErrorAsString();
			throw Exception(_ss.str().c_str());
		}

		listen(_listensocket, 5000);

		cout << "������ ���۵Ǿ����ϴ�. \n";
		cout << "CTL-C Ű�� ������ ���α׷��� �����մϴ�. \n";

		// leten sockt�� TCP connection socket ��ο� ���ؼ� I/O ����(avail) �̺�Ʈ�� ���� ������ ���.
		// �� ��, I/O ���� ���Ͽ� ���� ���� ����.

		//select �� ���� ����� �̺�Ʈ �߻� ��, �Ѱ��ִ� ������ �ʹ� ���� ������ �ִ�. �̸� poll�� �̿��� �غ��� ����.

		// ���⿡ ���� ���� �ڵ鿡 ���� select�� poll�� ��.
		// �ٸ�, receive�� accept�� ���ؼ��� ó��
		vector<WSAPOLLFD> readFds;
		// ��� ���� �� ��� RemoteClient�� ���� ������ ����Ŵ.
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

			//������ �׸��� ���� ����
			WSAPOLLFD _item2;
			_item2.events = POLLRDNORM;
			_item2.fd = _listensocket;
			_item2.revents = 0;
			readFds.push_back(_item2);

			// I/O ���� �̺�Ʈ�� ���������� ���
			WSAPoll(readFds.data(), (int)readFds.size(), 100);

			// readFds�� ������ �ʿ��� ó���� ��
			int num = 0;
			for (auto readFd : readFds)
			{
				if (readFd.revents != 0)
				{
					if (num == readFds.size() - 1) // ���� �����̸�
					{
						// accept ó��
						auto remoteClient = make_shared<RemoteClient>();

						//�̹� "Ŭ���̾�Ʈ ���� ������" �̺�Ʈ�� �� �����̹Ƿ� �׳� �̰� ȣ���ص� ��
						string _ignore;
						remoteClient->_socket = accept(_listensocket, NULL, 0);
						// nonblick socket���� ��� ����
						u_long val = 1;
						int ret = ioctlsocket(remoteClient->_socket, FIONBIO, &val);
						if (ret != 0)
						{
							stringstream ss;
							ss << "bind failed : " << GetLastErrorAsString();
							throw Exception(ss.str().c_str());
						}

						// New Client�� ��Ͽ� �߰�
						remoteClients.insert({ remoteClient.get(), remoteClient });

						cout << "Client joined. There are " << remoteClients.size() << " connection \n";
					}
					else //TCP ���� �����̸�
					{
						// ���� �����͸� �״�� ȸ��
						RemoteClient* remoteClient = readFdsToRemoteClients[num];

						int ec = (int)recv(remoteClient->_socket, remoteClient->_receiveBuffer, MaxReceiveLength, 0);
						if (ec <= 0)
						{
							//error Ȥ�� ���� ����
							//�ش� ������ ����
							closesocket(remoteClient->_socket);
							remoteClients.erase(remoteClient);

							cout << "Client left. There are " << remoteClients.size() << " connection \n";
						}
						else
						{
							cout << remoteClient->_receiveBuffer << endl;
							//���� �����͸� �״�� �۽�.
							//����� ��� client���� �۽�
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

		// ����ڰ� CTL-C�� ���� �������� ����. ��� ����
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

	////���� �ڵ�
	//SOCKET _socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	////Overlapped receive or accept �� �� ���Ǵ� overlapped ��ü
	//// I/O �Ϸ� ������ �����Ǿ� ��.
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