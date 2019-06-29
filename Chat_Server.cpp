// Chat_Server.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

using namespace std;

addrinfo hints;
string portNumber = "7777";
atomic_bool killConnection = TRUE;
fd_set ConnectionSet, ErrorSet;
mutex mEX_Sets, mEX_NameMap;
thread t;
map<SOCKET, string> nameMap;

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

		status = ::bind(newSocket, i->ai_addr, i->ai_addrlen);	//Specify ::bind because bind is the prefered overload.
		if (status > -1) { continue; }
	}
	if (status == -1) { closesocket(newSocket); cout << "Socket connection failed." << endl; freeaddrinfo(res); return INVALID_SOCKET; }	//Upon failure, return an invalid socket and give error message.

	freeaddrinfo(res);
	return newSocket;
}

void ReceiveConnections(SOCKET in) {
	unique_lock<mutex> sets(mEX_Sets, defer_lock);			//Associates variable "sets" with mutex mEX_sets while leaving the mutex unlocked.
	unique_lock<mutex> names(mEX_NameMap, defer_lock);		//Associates variable "names" with mutex mEX_names while leaving the mutex unlocked.

	//while (!killConnection.load(memory_order_acquire) && in != INVALID_SOCKET) {		//I considered having this on it's own thread for conceptual separation and to free up the UI.
		//Check for incoming connection requests...
		cout << "Listening..." << endl;
		int result = listen(in, 20);
		if (result > -1) { cout << "Connection heard." << endl; }
		else {
			int x = WSAGetLastError();
			cout << gai_strerrorA(x) << endl;
		}
		SOCKET newConnection = accept(in, NULL, NULL);
		if (newConnection != INVALID_SOCKET) { cout << "Connection accepted." << endl; }
		else {
			int x = WSAGetLastError();
			cout << gai_strerrorA(x) << endl;
		}
		sets.lock();
		FD_SET(newConnection, &ConnectionSet);	//Adds any incoming connections to connection set
		sets.unlock();

		char x[1025];
		memset(x, '\0', 1025);
		int bytesRecv = recv(newConnection, x, 1024, 0);
		string name = x;

		names.lock();
		nameMap[newConnection] = name;	//Associates a username with a given socket
		names.unlock();
	//}
}


void PollPorts() {
	//Run indefinitely until cued to stop.
	while (!killConnection.load(memory_order_acquire)) {
		unique_lock<mutex> sets(mEX_Sets, defer_lock);		//Associates variable "sets" with mutex mEX_sets while leaving the mutex unlocked.
		unique_lock<mutex> names(mEX_NameMap, defer_lock);		//Associates variable "names" with mutex mEX_names while leaving the mutex unlocked.

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

		while (ready == 0 && !killConnection.load(memory_order_acquire)) {
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
			cout << gai_strerrorA(x) << endl;
			Sleep(250);
			continue;
		}

		for (unsigned int i = 0; i < readFDS.fd_count; ++i) {
			char buf[1025];
			string out;

			names.lock();
			out = "[" + nameMap.find(readFDS.fd_array[i])->second + "] ";
			names.unlock();

			//Read from socket and append to output string until no new characters are read.
			while (true) {
				memset(buf, '\0', 1025);
				recv(readFDS.fd_array[i], buf, 1024, 0);
				out.append(buf);
				if (buf[1023] == '\0') { break; }
			}

			//Send message to all connections.
			for (unsigned int j = 0; j < readFDS.fd_count; ++j) { send(readFDS.fd_array[j], out.c_str(), out.size() + 1, 0); }
			out.clear();
		}

		for (unsigned int i = 0; i < exceptFDS.fd_count; ++i) {
			SOCKET s = exceptFDS.fd_array[i];
			closesocket(s);
			sets.lock();	//Lock fd sets
			FD_CLR(s, &ConnectionSet);
			FD_CLR(s, &ErrorSet);
			sets.unlock();	//Unlock fd sets

			//Send error message.
			string out = "Connection to ";
			out += "_____";		//Placeholder. Connections are unnamed at this point.
			out += " was lost.";
			cout << out.c_str() << endl;
		}
	}

	return;
}

int main()
{
	//Start up WinSock
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		cout << "WinSock2 failed to initialize." << endl;
	}
	//Ensure sets are empty before use.
	FD_ZERO(&ConnectionSet); FD_ZERO(&ErrorSet);

	//Set up connection parameters used for all socket connections.
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;		//IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;	//Stream connection type
	hints.ai_protocol = IPPROTO_TCP;	//TCP/IP protocol for connection

	SOCKET s = OpenNewSocket();
	killConnection.store(FALSE, memory_order_release);		//Set "kill connection" flag to FALSE.
															//Acquire/release semantics are used to enforce adequate memory ordering at minimal cost.
	ReceiveConnections(s);
	t = thread(PollPorts);


	cout << "text" << endl;
	
	char x[2];
	cin >> x;
	string input(x);
	while (input != "x") {
		ReceiveConnections(s);
	}


	//Close all sockets
	closesocket(s);

	unique_lock<mutex> sets(mEX_Sets);		//Lock mutex for the three fd sets
		for (size_t i = 0; i < ConnectionSet.fd_count; ++i) { closesocket(ConnectionSet.fd_array[i]); }
		for (size_t i = 0; i < ErrorSet.fd_count; ++i) { closesocket(ErrorSet.fd_array[i]); }
	sets.unlock();							//Unlock mutex for the three fd sets once done.
											//Note, unlocking may be overkill here since the SOCKETMANAGER destructor should only be run when everything else is shutting down.
											//unique_lock<> or lock_guard<> both release the mutex automatically in their in their own destructor when the local variable expires.
	
	//Wait to cue thread exit until just before joining. If not, thread t may finish first and prematurely terminate the program upon completion.
	killConnection.store(TRUE, memory_order_release);		//Set "kill connection" flag to TRUE. This will cue the loop polling the sockets to exit.
	if (t.joinable()) { t.join(); }		//Join the connection thread before destruction. Check first that it's joinable.

	//Cleanup any lingering WinSock data
	WSACleanup();

    return 0;
}