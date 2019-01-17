// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#include <atomic>
#include <iostream>
#include <map>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <thread>
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")	//Links in dll library needed for WS2tcpip.h

//Classes
#include "Socket_Manager.h"

// TODO: reference additional headers your program requires here
