/*////////////////////////////////////////////////////////////////////////////////////////////////////
//Chat_Server
//Ryan Toll 2018
//
//
////////////////////////////////////////////////////////////////////////////////////////////////////*/

#include "stdafx.h"

SOCKETMANAGER::SOCKETMANAGER() {
	//Start up WinSock
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		std::cout << "WinSock2 failed to initialize." << std::endl;
	}
	//Ensure sets are empty before use.
	FD_ZERO(&ConnectionSet); FD_ZERO(&ErrorSet);

	//Set up connection parameters used for all socket connections.
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;		//IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;	//Stream connection type
	hints.ai_protocol = IPPROTO_TCP;	//TCP/IP protocol for connection

	s = OpenNewSocket();
	killConnection.store(FALSE, std::memory_order_release);		//Set "kill connection" flag to FALSE.
																//Acquire/release semantics are used to enforce adequate memory ordering at minimal cost.

	//t = std::thread(&SOCKETMANAGER::PollPort, this);		//Create a new thread constructed with the PollPorts function called on this object. Assign this new thread to variable t.
	//listener = std::thread(&SOCKETMANAGER::ReceiveConnections, this, s);	//Create a dedicated thread to listen for and accept new connections.
}

SOCKETMANAGER::~SOCKETMANAGER(){
	//Close all sockets
	closesocket(s);

	std::unique_lock<std::mutex> sets(mEX_Sets);		//Lock mutex for the three fd sets
		for (size_t i = 0; i < ConnectionSet.fd_count; ++i) { closesocket(ConnectionSet.fd_array[i]); }
		for (size_t i = 0; i < ErrorSet.fd_count; ++i) { closesocket(ErrorSet.fd_array[i]); }
	sets.unlock();							//Unlock mutex for the three fd sets once done.
											//Note, unlocking may be overkill here since the SOCKETMANAGER destructor should only be run when everything else is shutting down.
											//unique_lock<> or lock_guard<> both release the mutex automatically in their in their own destructor when the local variable expires.
	
	//Wait to cue thread exit until just before joining. If not, thread t may finish first and prematurely terminate the program upon completion.
	killConnection.store(TRUE, std::memory_order_release);		//Set "kill connection" flag to TRUE. This will cue the loop polling the sockets to exit.
	if (t.joinable()) { t.join(); }		//Join the connection thread before destruction. Check first that it's joinable.

	//Cleanup any lingering WinSock data
	WSACleanup();
}

SOCKET OpenNewSocket() {
	SOCKET newSocket = INVALID_SOCKET;
	int status = -1;
	addrinfo* res;

	//Set up address structure based off of input parameters. Store results in pointer: res.
	status = getaddrinfo(NULL, portNumber.c_str(), &hints, &res);

	//Iterate through all addresses in set until one works.
	for (addrinfo* i = res; i != NULL; i = i->ai_next) {
		newSocket = socket(i->ai_family, i->ai_socktype, i->ai_protocol);
		if (newSocket == INVALID_SOCKET) { closesocket(newSocket); continue; }

		
		status = bind(newSocket, i->ai_addr, i->ai_addrlen);	//
		if (status > -1) { continue; }
	}
	if (status == -1) { closesocket(newSocket); std::cout << "Socket connection failed." << std::endl; freeaddrinfo(res); return INVALID_SOCKET; }	//Upon failure, return an invalid socket and give error message.

	freeaddrinfo(res);
	return newSocket;
}

void SOCKETMANAGER::PollPort() {
	//Run indefinitely until cued to stop.
	while (!killConnection.load(std::memory_order_acquire)) {
		std::unique_lock<std::mutex> sets(mEX_Sets, std::defer_lock);		//Associates variable "sets" with mutex mEX_Sets while leaving the mutex unlocked.

		//The function gets its own copy of the fd sets to prevent data collision/contention.
		//This is remade each pass with the most current data.
		sets.lock();		//Lock fd sets
			fd_set readFDS = ConnectionSet, exceptFDS = ErrorSet;		//Read from all vaild connections.
		sets.unlock();		//Unlock fd sets

		//Output should go to all active connections.
		//ReadFDS will be altered in select() command, so it must be copied first.
		//ReadFDS is local only and therefore safe to copy outside of lock.
		fd_set writeFDS = readFDS;

		timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 10000;		//To be clear, this is denominated in MICRO seconds (Greek letter mu represented by our letter u). Times out in 0.01 sec.
		int ready = select(NULL, &readFDS, NULL, &exceptFDS, &tv);

		while (ready == 0 && !killConnection.load(std::memory_order_acquire)) {
			Sleep(10);			//Denominated in miliseconds.
			sets.lock();		//Lock fd sets
				readFDS = ConnectionSet;
				exceptFDS = ErrorSet;
			sets.unlock();		//Unlock fd sets
			ready = select(NULL, &readFDS, NULL, &exceptFDS, &tv);
		}

		if (killConnection) { return; }

		if (ready == -1) {
			int x = WSAGetLastError();
			std::cout << gai_strerrorA(x) << std::endl;
			Sleep(250);
			continue;
		}

		for (unsigned int i = 0; i < readFDS.fd_count; ++i) {
			char buf[1024];
			memset(buf, '\0', 1024);
			recv(readFDS.fd_array[i], buf, 1024, 0);

			//Send message to all connections.
			for (unsigned int j = 0; j < readFDS.fd_count; ++j) { send(readFDS.fd_array[j], buf, 1024, 0); }
		}

		for (unsigned int i = 0; i < exceptFDS.fd_count; ++i) {
			SOCKET s = exceptFDS.fd_array[i];
			closesocket(s);
			sets.lock();	//Lock fd sets
			FD_CLR(s, &ConnectionSet);
			FD_CLR(s, &ErrorSet);
			sets.unlock();	//Unlock fd sets

			//Send error message.
			std::string out = "Connection to ";
			out += "_____";		//Placeholder. Connections are unnamed at this point.
			out += " was lost.";
			std::cout << out.c_str() << std::endl;
		}
	}

	return;
}

void SOCKETMANAGER::ReceiveConnections(SOCKET in) {
	std::unique_lock<std::mutex> sets(mEX_Sets, std::defer_lock);		//Associates variable "sets" with mutex mEX_Sets while leaving the mutex unlocked.
	std::unique_lock<std::mutex> names(mEX_NameMap, std::defer_lock);		//Associates variable "sets" with mutex mEX_Sets while leaving the mutex unlocked.

	while (!killConnection.load(std::memory_order_acquire) && in != INVALID_SOCKET) {
		//Check for incoming connection requests...
		std::cout << "Listening..." << std::endl;
		int result = listen(in, 20);
		if (result > -1) { std::cout << "Connection heard." << std::endl; }
		else {
			int x = WSAGetLastError();
			std::cout << gai_strerrorA(x) << std::endl;
		}
		SOCKET newConnection = accept(in, NULL, NULL);
		if (newConnection != INVALID_SOCKET) { std::cout << "Connection accepted." << std::endl; }
		else {
			int x = WSAGetLastError();
			std::cout << gai_strerrorA(x) << std::endl;
		}
		sets.lock();
			FD_SET(newConnection, &ConnectionSet);	//Adds any incoming connections to connection set
		sets.unlock();

		char x[1024];
		int bytesRecv = recv(newConnection, x, 1023, 0);
		std::string name = x;

		names.lock();
			nameMap[newConnection] = name;	//Associates a username with a given socket
		names.unlock();
	}
}

//char name[64];
//memset(name, '\0', 64);
//int x = gethostname(name, 64);