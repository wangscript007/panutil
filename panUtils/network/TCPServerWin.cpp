
#include "TCPServer.h"
#include "SocketFunc.h"
#ifdef WINDOW_SYSTEM

#include	"ITCPConn.h"
#include "TCPConnWindows.h"
#include <WS2tcpip.h>
#include	<chrono>

namespace panutils {



	//HANDLE hCompletion;
	int TCPServer::Start()
	{
		auto tmpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
		_hICompletionPort = new HANDLE;
		memcpy(_hICompletionPort, &tmpHandle, sizeof(HANDLE));
		_endLoop = false;

		_threadIocp = std::move(std::thread(&TCPServer::IocpLoop, this));

		return 0;
	}

	int TCPServer::Stop()
	{
		_endLoop = true;
		if (_hICompletionPort != nullptr)
		{
			CloseHandle(*(HANDLE*)_hICompletionPort);
			delete (HANDLE*)_hICompletionPort;
			_hICompletionPort = nullptr;
		}
		CloseSocket(_fd);
		_fd = -1;

		if (_threadIocp.joinable())
		{
			_threadIocp.join();
		}
		return 0;
	}



	void TCPServer::IocpLoop()
	{
		std::vector<std::thread> recvWorkers;
		for (int i = 0; i < _numThread; i++)
		{
			recvWorkers.push_back(std::move(std::thread(&TCPServer::RecvWorker, this, _hICompletionPort)));
		}

		while (_endLoop == false)
		{
			SOCKADDR_IN	addrRemote;
			int addrLen = sizeof(addrRemote);
			auto infd = accept(_fd, (sockaddr*)&addrRemote, &addrLen);
			if (infd == -1)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			auto handleData = new IOCP_HANDLER();
			auto client = ITCPConn::CreateConn((int)infd);
			handleData->conn = client;
			auto winClient = dynamic_cast<TCPConnWindows*>(client.get());
			{
				char hostname[NI_MAXHOST];
				char servInfo[NI_MAXSERV];
				getnameinfo((const SOCKADDR*)&addrRemote, addrLen,
					hostname,
					NI_MAXHOST, servInfo, NI_MAXSERV, NI_NUMERICSERV);
				client->hostname = hostname;

				struct sockaddr_in *ipv4 = &addrRemote;
				char ipAddress[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &(ipv4->sin_addr), ipAddress, INET_ADDRSTRLEN);
				client->remoteAddr = ipAddress;
				client->port = ipv4->sin_port;
			}

			CreateIoCompletionPort((HANDLE)infd, (*(HANDLE*)_hICompletionPort), (ULONG_PTR)handleData, 0);
			auto param = this->_dataParam.lock();
			if (param != nullptr)
			{
				this->_dataCallback(handleData->conn, nullptr, 0,
					param);
			}

			winClient->CallWindowRecv();

		}


		for (size_t i = 0; i < recvWorkers.size(); i++)
		{
			if (recvWorkers[i].joinable())
			{
				recvWorkers[i].join();
			}

		}
	}

	void TCPServer::RecvWorker(void * lpParam)
	{
		if (nullptr == lpParam)
		{
			return;
		}
		HANDLE				completionPort = static_cast<HANDLE>(*(HANDLE*)lpParam);
		DWORD				bytesTransferred;
		LPOVERLAPPED		lpOverlapped = 0;
		IOCP_HANDLER		*lpHandleData = nullptr;
		DWORD				recvBytes = 0;
		DWORD				flags = 0;
		int					iRet = 0;

		while (!_endLoop)
		{
			iRet = GetQueuedCompletionStatus(completionPort, &bytesTransferred,
				(PULONG_PTR)&lpHandleData,
				(LPOVERLAPPED*)&lpOverlapped, INFINITE);
			IOCP_IO_DATA* iodata = nullptr;
			std::shared_ptr<void> param = this->_dataParam.lock();
			if (lpOverlapped != nullptr)
			{
				iodata = (IOCP_IO_DATA*)CONTAINING_RECORD(lpOverlapped, IOCP_IO_DATA, overlapped);
			}
			if (0 == iRet)
			{
				continue;
			}


			if (0 == bytesTransferred)
			{
				this->ProcessError(lpHandleData);
				lpHandleData = nullptr;
				continue;
			}

			auto winClient = dynamic_cast<TCPConnWindows*>(lpHandleData->conn.get());
			if (winClient == nullptr)
			{
				this->ProcessError(lpHandleData);
				lpHandleData = nullptr;
				continue;
			}

			switch (iodata->ioType)
			{
			case IO_READ:
				if (param != nullptr)
				{
					this->_dataCallback(lpHandleData->conn, winClient->Buf(), bytesTransferred,
						param);
				}
				winClient->CallWindowRecv();
				break;
			case IO_WRITE:
				winClient->Sended(bytesTransferred);
				break;
			default:
				break;
			}

		}
	}

	void TCPServer::ProcessError(void* ptr)
	{
		auto lpHandleData = (IOCP_HANDLER*)ptr;
		lpHandleData->conn->Close();
		auto param = this->_dataParam.lock();
		if (param != nullptr)
		{
			this->_errCallback(lpHandleData->conn, 0, param);
		}
		delete lpHandleData;
	}


}
#endif