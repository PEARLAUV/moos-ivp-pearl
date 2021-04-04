/*
 * PEARLSimServer.h
 *
 * Created on: 01/27/2021
 * Author: Ethan Rolland
 * Adapted from:
 * 
 */

#ifndef PEARLSIMSERVER_H_
#define PEARLSIMSERVER_H_

#include <deque>
#include <vector>
#include "MOOS/libMOOS/MOOSLib.h"

/*
 Creates a thread in which to launch the PEARL simulated server.
 The server blocks when listening for an incoming connection.
 The main MOOS app won't block because the listener is running
 in this class as a separate thread.
 */

enum appCastType { AC_NONE, AC_ERROR, AC_WARNING, AC_EVENT, AC_UNKNOWN };

class appCastMsgs {
public:
    appCastMsgs() { type = AC_UNKNOWN, msg = ""; };
    ~appCastMsgs() {};
    appCastMsgs(int acType, std::string acMsg)
        { define(acType, acMsg); };
    void define (int acType, std::string acMsg)
        { type = acType; msg = acMsg; };
    int type;
    std::string msg;
};

class PEARLSimServer {
public:
                        PEARLSimServer(int portID=ACM0);
        virtual         ~PEARLSimServer() {};
        bool            Run();
        static bool     dispatch(void* param);

        void 			quit();
        bool            IsListening();
        bool            ClientAttached();
        int             DataAvailable();
        std::string     GetNextSentence();
        bool            SendToClient(std::string str);
        bool            ServerLoop();
        int             GetNextAppCastMsg(std::string& msg);

private:
        bool            ServerListen();
        bool            ServerIn();

        bool            PrepareConnection();
        static bool     thread_func(void *pThreadData);

        CMOOSThread     m_serverThread;

        unsigned short  m_blankCount;
        int             m_port;
        bool            m_bListening;
        bool            m_bAccepted;
        bool            m_bGoodSocket;
        int             m_socketfd;
        int             m_newSocketfd;
        socklen_t       m_cliLen;
        struct sockaddr_in m_serv_addr, m_cli_addr;
        char            m_inBuffer[MAX_BUF_SIZE];         // persistent char buffer
        std::string     m_fullBuff;
        std::deque<std::string> m_inNMEA;
        std::deque<appCastMsgs> m_appCasts;
};

#endif

