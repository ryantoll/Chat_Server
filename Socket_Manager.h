/*////////////////////////////////////////////////////////////////////////////////////////////////////
//Chat_Server
//Ryan Toll 2018
//
//
////////////////////////////////////////////////////////////////////////////////////////////////////*/

#ifndef SOCKET_MANAGER
#define SOCKET_MANAGER

#include "stdafx.h"

SOCKET OpenNewSocket();

class SOCKETMANAGER {
public:
	friend SOCKET OpenNewSocket();
	SOCKETMANAGER();
	~SOCKETMANAGER();

	void ReceiveConnections(SOCKET in);
	//SOCKET OpenNewSocket();

	std::string portNumber = "7777";
private:
	std::atomic_bool killConnection = TRUE;
	SOCKET s;
	addrinfo hints;
	fd_set ConnectionSet, ErrorSet;
	std::mutex mEX_Sets, mEX_NameMap;
	std::thread t, listener;
	std::map<SOCKET, std::string> nameMap;

	void PollPort();
};

#endif // !SOCKET_MANAGER

