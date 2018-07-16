#pragma once

#include <stdlib.h>
#include <WinSock2.h>
#include <WS2tcpip.h>

#include <sys/types.h>
#include <sys/timeb.h>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

#include <queue>
#include <map>
#include <string>

#include "pf_log.h"
#include "zmq.h"
#include "czmq.h"
#include "zookeeper.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "pf_log.lib")
#pragma comment(lib, "libzmq.lib")
#pragma comment(lib, "libczmq.lib")
#pragma comment(lib, "zookeeper.lib")

namespace uwb_network
{
	const unsigned char HEAD = 0xa5;
	const unsigned char TAIL = 0x7e;
	const double TICK_COST = 15.625; //ps

	typedef struct tagNetData{
		char szIp[24];
		unsigned short usPort;
		unsigned long long ullTime;
		unsigned int uiDataLen;
		unsigned char * pData;
		tagNetData()
		{
			pData = NULL;
			uiDataLen = 0;
			ullTime = 0;
			usPort = 0;
			memset(szIp, 0, sizeof(szIp));
		}
	} NetData;

	typedef struct tagPacketData
	{
		uint8_t deviceType; //03:locator
		uint8_t packetType; //00:locate, 01:synchrozation
		uint8_t sequence; //0x00-ff
		uint8_t locatorId[4];
		uint8_t tagId[4];
		uint8_t timestamp[5];
		uint8_t tagBattery;
		uint8_t reverse;
		uint8_t crc[2];
	} PacketData;

	typedef struct tagLingeData
	{
		char szFromIp[24];
		unsigned short usFromPort;
		unsigned int uiLingeDataLen;
		unsigned char * pLingeData;
	} LingeData;

	typedef struct tagListenData
	{
		char szIp[32];
		unsigned short usListenPort;
		unsigned short usPubPort;
		uint64_t uStartTime;
		uint64_t uUpdateTime;
	} ListenerData;

	class Listener
	{
	public:
		explicit Listener(const char * logPath = NULL, unsigned int useZk = 0, const char * zkPath = NULL);
		~Listener();
		int Start(unsigned short, unsigned short, const char * szIp = NULL);
		int Stop();
	private:
		unsigned short m_usListenPort;
		unsigned short m_usPublishPort;
		char m_szLocalIp[32];
		bool m_bRunning;
		unsigned long long m_ullLogInst;
		
		bool m_bConnectZk;
		bool m_bUseZk;
		zhandle_t * m_zk;
		char m_szZkHome[256];
		char m_szInstPath[256];
		ListenerData m_listenerData;
		std::thread m_thdZkState;

		SOCKET m_listenSock;
		std::thread m_thdListenRecv;
		zsock_t * m_pub;

		typedef std::queue<NetData *> NetDataQueue;
		NetDataQueue m_netDataQue;
		std::mutex m_mutex4NetDataQue;
		std::condition_variable m_cond4NetDataQue;
		std::thread m_thdHandleNetDataQue;
		
		typedef std::map<std::string, LingeData *> LingeDataList;
		LingeDataList m_netLingeDataList;
		std::mutex m_mutex4LingeDataList;

	protected:
		int do_receive();
		void handle_netdata_queue();
		bool add_netdata(NetData *);
		void parse_netdata(const NetData *);
		bool add_lingeData(LingeData *);
		void clear_lingeDataList();
		void publish(const char *);
		void zk_addNode();
		void zk_updateNode();
		void zk_removeNode();
		
		friend void listen_receive_thread(void *);
		friend void handle_netdata_queue_thread(void *);
		friend void update_zk_state_thread(void *);

		friend void zk_server_watcher(zhandle_t *, int, int, const char *, void *);
		friend void zk_create_node_completion(int rc, const char *, const void *);
		friend void zk_create_instance_completion(int rc, const char *value, const void *data);
		friend void zk_set_node_stat_completion(int rc, const struct Stat *stat, const void *data);
	};

}




