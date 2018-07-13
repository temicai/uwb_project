#include "listener.h"

void uwb_network::listen_receive_thread(void * param_)
{
	auto pInst = (uwb_network::Listener *)param_;
	if (pInst) {
		int nRetVal = pInst->do_receive();
	}
}

void uwb_network::handle_netdata_queue_thread(void * param_)
{
	auto pInst = (uwb_network::Listener *)param_;
	if (pInst) {
		pInst->handle_netdata_queue();
	}
}

void uwb_network::zk_server_watcher(zhandle_t * zk_, int nType_, int nState_, const char * path_,
	void * watcherCtx_)
{
	if (nType_ == ZOO_SESSION_EVENT) {
		auto pInst = (uwb_network::Listener *)watcherCtx_;
		if (pInst) {
			if (nState_ == ZOO_CONNECTED_STATE) {
				pInst->m_bConnectZk = true;
			}
			else if (nState_ == ZOO_EXPIRED_SESSION_STATE) {
				pInst->m_bConnectZk = false;
				zookeeper_close(pInst->m_zk);
				pInst->m_zk = zookeeper_init(pInst->m_szZkHome, uwb_network::zk_server_watcher, 60000, 
					NULL, pInst, 0);
			}
		}
	}
}

void uwb_network::zk_create_node_completion(int rc_, const char * path_, const void * data_)
{
	auto pInst = (uwb_network::Listener *)data_;
	if (pInst) {
		switch (rc_) {
			case ZCONNECTIONLOSS:
			case ZOPERATIONTIMEOUT:
				if (pInst->m_bRunning && pInst->m_bConnectZk && pInst->m_zk) {
					zoo_acreate(pInst->m_zk, path_, "", 0, &ZOO_OPEN_ACL_UNSAFE, 0, 
						uwb_network::zk_create_node_completion, data_);
				}
				break;
			default: break;
		}
	}
}

uwb_network::Listener::Listener(const char * logPath_, unsigned int uiUseZk_, const char * zkPath_)
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	m_bRunning = false;
	m_usListenPort = 0;
	m_usPublishPort = 0;
	m_listenSock = INVALID_SOCKET;
	m_bConnectZk = false;
	m_zk = NULL;
	memset(m_szZkHome, 0, sizeof(m_szZkHome));

	m_ullLogInst = LOG_Init();
	pf_logger::LogConfig logConf;
	memset(&logConf.szLogPath, 0, sizeof(logConf.szLogPath));
	logConf.usLogType = pf_logger::eLOGTYPE_FILE;
	logConf.usLogPriority = pf_logger::eLOGPRIO_ALL;
	if (logPath_ && strlen(logPath_)) {
		strcpy_s(logConf.szLogPath, sizeof(logConf.szLogPath), logPath_);
	}
	else {
		sprintf_s(logConf.szLogPath, sizeof(logConf.szLogPath), ".//");
	}
	LOG_SetConfig(m_ullLogInst, logConf);

	if (uiUseZk_ > 0) {
		if (zkPath_ && strlen(zkPath_)) {
			strcpy_s(m_szZkHome, sizeof(m_szZkHome), zkPath_);
			m_zk = zookeeper_init(zkPath_, uwb_network::zk_server_watcher, 60000, NULL, this, 0);
			if (m_zk) {
				zoo_acreate(m_zk, "/uwb", "", 0, &ZOO_OPEN_ACL_UNSAFE, 0, 
					uwb_network::zk_create_node_completion, this);
				zoo_acreate(m_zk, "/uwb/netmodule", "", 0, &ZOO_OPEN_ACL_UNSAFE, 0, 
					uwb_network::zk_create_node_completion, this);
			}
			m_bUseZk = true;
		}
	}

}

uwb_network::Listener::~Listener()
{
	if (m_bRunning) {
		Stop();
	}
	if (m_ullLogInst) {
		LOG_Release(m_ullLogInst);
		m_ullLogInst = 0;
	}
	zsys_shutdown();
	WSACleanup();
}

int uwb_network::Listener::Start(unsigned short usListenPort_, unsigned short usPubPort_)
{
	int result = -1;
	if (!m_bRunning && usListenPort_ > 0) {
		SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock != INVALID_SOCKET) {
			unsigned int uiRcvBufLen = 512 * 1024;
			setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char *)&uiRcvBufLen, (int)sizeof(uiRcvBufLen));
			u_long ulNonBlock = 1;
			ioctlsocket(sock, FIONBIO, &ulNonBlock);
			sockaddr_in listenAddr;
			listenAddr.sin_family = AF_INET;
			listenAddr.sin_port = htons(usListenPort_);
			listenAddr.sin_addr.s_addr = htonl(INADDR_ANY);
			int nListenAddrLen = sizeof(listenAddr);
			if (bind(sock, (const sockaddr *)&listenAddr, nListenAddrLen) == 0) {
				m_listenSock = sock;
				m_usListenPort = usListenPort_;
				m_usPublishPort = usPubPort_;
				m_pub = zsock_new(ZMQ_PUB);
				do {
					if (zsock_bind(m_pub, "tcp://*:%hu", m_usPublishPort) != -1) {
						break;
					}
					else {
						m_usPublishPort += 1;
					}
				} while (1);
				m_bRunning = true;
				m_thdListenRecv = std::thread(uwb_network::listen_receive_thread, this);
				m_thdHandleNetDataQue = std::thread(uwb_network::handle_netdata_queue_thread, this);
				result = 0;
			}
			else {
				closesocket(sock);
			}
		}
	}
	return result;
}

int uwb_network::Listener::Stop()
{
	if (m_bRunning) {
		
		m_bRunning = false;
		m_thdListenRecv.join();

		m_cond4NetDataQue.notify_all();
		m_thdHandleNetDataQue.join();
		
		if (m_listenSock != INVALID_SOCKET) {
			closesocket(m_listenSock);
			m_listenSock = INVALID_SOCKET;
		}

		if (m_pub) {
			zsock_destroy(&m_pub);
		}

		clear_lingeDataList();
	}
	return 0;
}

int uwb_network::Listener::do_receive()
{
	int result = 0;
	char szLog[256] = { 0 };
	struct sockaddr_in clientAddr;
	int nClientAddrLen = sizeof(clientAddr);
	char szRecvBuffer[1400] = { 0 };
	int nRecvBufferLen = (int)sizeof(szRecvBuffer);
	do {
		if (m_listenSock != INVALID_SOCKET) {
			int nRetVal = recvfrom(m_listenSock, szRecvBuffer, nRecvBufferLen, 0, (sockaddr *)&clientAddr,
				&nClientAddrLen);
			if (nRetVal == 0) {
				//the connection has been gracefully closed 
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}
			else if (nRetVal == INVALID_SOCKET) {
				int nErr = WSAGetLastError();
				if (nErr == WSAEWOULDBLOCK || nErr == EWOULDBLOCK || nErr == EAGAIN) {
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
					continue;
				}
				else {
					sprintf_s(szLog, sizeof(szLog), "[listener]%s[%u]socket error=%d\n", __FUNCTION__, 
						__LINE__, nErr);
					LOG_Log(m_ullLogInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
					result = 1;
					break;
				}
			}
			else if (nRetVal > 0) {
				timeb t;
				ftime(&t);
				auto pNetData = new uwb_network::NetData();
				pNetData->usPort = ntohs(clientAddr.sin_port);
				inet_ntop(AF_INET, &clientAddr.sin_addr, pNetData->szIp, sizeof(pNetData->szIp));
				pNetData->uiDataLen = nRetVal;
				pNetData->pData = new unsigned char[nRetVal + 1];
				memcpy_s(pNetData->pData, nRetVal + 1, szRecvBuffer, nRetVal);
				pNetData->pData[nRetVal] = '\0';
				pNetData->ullTime = t.time * 1000 + t.millitm;
				if (!add_netdata(pNetData)) {
					if (pNetData->pData) {
						delete[] pNetData->pData;
						pNetData->pData = NULL;
					}
					delete pNetData;
					pNetData = NULL;
				}
				else {
					sprintf_s(szLog, sizeof(szLog), "[listener]%s[%u]recv %u data from %s:%hu, time=%llu\n", 
						__FUNCTION__, __LINE__, pNetData->uiDataLen, pNetData->szIp, pNetData->usPort, 
						pNetData->ullTime);
					LOG_Log(m_ullLogInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(0));
			}
		}
	} while (m_bRunning);
	return result;
}

void uwb_network::Listener::handle_netdata_queue()
{
	do {
		std::unique_lock<std::mutex> lock(m_mutex4NetDataQue);
		m_cond4NetDataQue.wait_for(lock, std::chrono::seconds(1), [&] {
			return (!m_bRunning || !m_netDataQue.empty());
		});
		if (!m_bRunning && m_netDataQue.empty()) {
			break;
		}
		if (!m_netDataQue.empty()) {
			auto pNetData = m_netDataQue.front();
			m_netDataQue.pop();
			lock.unlock();
			if (pNetData) {
				parse_netdata(pNetData);
				if (pNetData->pData) {
					delete[] pNetData->pData;
					pNetData->pData = NULL;
				}
				delete pNetData;
				pNetData = NULL;
			}
		}
	} while (1);
}

bool uwb_network::Listener::add_netdata(uwb_network::NetData * pNetData_)
{
	bool result = false;
	if (pNetData_) {
		std::lock_guard<std::mutex> lock(m_mutex4NetDataQue);
		m_netDataQue.emplace(pNetData_);
		if (m_netDataQue.size() == 1) {
			m_cond4NetDataQue.notify_one();
		}
		result = true;
	}
	return result;
}

void uwb_network::Listener::parse_netdata(const uwb_network::NetData * pNetData_)
{
	char szLog[256] = { 0 };
	if (pNetData_ && pNetData_->pData && pNetData_->uiDataLen > 0) {
		unsigned char * pBuf = NULL;
		unsigned int uiBufLen = 0;
		char szFrom[32] = { 0 };
		sprintf_s(szFrom, sizeof(szFrom), "%s:%hu", pNetData_->szIp, pNetData_->usPort);
		//std::string strLink = szFrom;
		bool bMalloced = false;
		if (strlen(szFrom) > 0){
			std::lock_guard<std::mutex> lock(m_mutex4LingeDataList);
			if (!m_netLingeDataList.empty()) {
				uwb_network::Listener::LingeDataList::iterator iter = m_netLingeDataList.find(szFrom);
				if (iter != m_netLingeDataList.end()) {
					auto pLingeData = iter->second;
					if (pLingeData) {
						uiBufLen = pLingeData->uiLingeDataLen + pNetData_->uiDataLen;
						pBuf = new unsigned char[uiBufLen + 1];
						memcpy_s(pBuf, uiBufLen + 1, pLingeData->pLingeData, pLingeData->uiLingeDataLen);
						memcpy_s(pBuf + pLingeData->uiLingeDataLen, pNetData_->uiDataLen + 1, pNetData_->pData,
							pNetData_->uiDataLen);
						pBuf[uiBufLen] = '\0';
						bMalloced = true;
						if (pLingeData->pLingeData) {
							delete[] pLingeData->pLingeData;
							pLingeData->pLingeData = NULL;
						}
						delete pLingeData;
						pLingeData = NULL;
						m_netLingeDataList.erase(iter);
					}
				}
			}
		}
		//not pre-combine data
		if (!bMalloced) {
			uiBufLen = pNetData_->uiDataLen;
			pBuf = new unsigned char[uiBufLen + 1];
			memcpy_s(pBuf, uiBufLen + 1, pNetData_->pData, uiBufLen);
			pBuf[uiBufLen] = '\0';
		}

		unsigned int idx = 0;
		unsigned int uiPackDataLen = sizeof(PacketData);
		while (idx < uiBufLen) {
			
			if (pBuf[idx] == uwb_network::HEAD) {
				if (idx + uiPackDataLen + 1 < uiBufLen) {
					if (pBuf[idx + uiPackDataLen + 1] == uwb_network::TAIL) {
						//ok
						PacketData packet;
						memcpy_s(&packet, uiPackDataLen, pBuf + idx + 1, uiPackDataLen);
						if (packet.packetType == 0x01) {
							uint32_t uiLocatorId = 0;
							memcpy_s(&uiLocatorId, sizeof(uint32_t), packet.locatorId, sizeof(packet.locatorId));
							uint64_t uiTimestamp = 0;
							memcpy_s(&uiTimestamp, sizeof(uint64_t), packet.timestamp, sizeof(packet.timestamp));
							uint32_t uiSequence = packet.sequence;
							char szSyncMsg[256] = { 0 };
							sprintf_s(szSyncMsg, sizeof(szSyncMsg), "{\"type\":1,\"locator\":%u,\"seq\":%u,\"timestamp\":%f}",
								uiLocatorId, uiSequence, uiTimestamp * uwb_network::TICK_COST);
							publish(szSyncMsg);
							sprintf_s(szLog, sizeof(szLog), "[listener]%s[%u]SYNC: locator=%u, seq=%u, timestamp=%f\n",
								__FUNCTION__, __LINE__, uiLocatorId, uiSequence, uiTimestamp * uwb_network::TICK_COST);
							LOG_Log(m_ullLogInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
						}
						else if (packet.packetType == 0x00) {
							uint32_t uiLocatorId = 0;
							memcpy_s(&uiLocatorId, sizeof(uint32_t), packet.locatorId, sizeof(packet.locatorId));
							uint32_t uiTagId = 0;
							memcpy_s(&uiTagId, sizeof(uint32_t), packet.tagId, sizeof(packet.tagId));
							uint64_t uiTimestamp = 0;
							memcpy_s(&uiTimestamp, sizeof(uint64_t), packet.timestamp, sizeof(packet.timestamp));
							uint32_t uiSequence = packet.sequence;
							char szLocMsg[256] = { 0 };
							sprintf_s(szLocMsg, sizeof(szLocMsg), "{\"type\":0,\"locator\":%u,\"tag\":%u,\"seq\":%u,"
								"\timestamp\":%f}", uiLocatorId, uiTagId, uiSequence, uiTimestamp * uwb_network::TICK_COST);
							publish(szLocMsg);
							sprintf_s(szLog, sizeof(szLog), "[listener]%s[%u]LOC: locator=%u, tag=%u, seq=%u, timestamp=%f\n",
								__FUNCTION__, __LINE__, uiLocatorId, uiTagId, uiSequence, uiTimestamp * uwb_network::TICK_COST);
							LOG_Log(m_ullLogInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
						}
						idx += uiPackDataLen + 2;
					}
					else {
						idx += 1;
					}
				}
				else {
					//not enough data
					unsigned int uiLeftDataLen = uiBufLen - idx;
					auto pLingeData = new uwb_network::LingeData();
					pLingeData->uiLingeDataLen = uiLeftDataLen;
					pLingeData->pLingeData = new unsigned char[uiLeftDataLen + 1];
					memcpy_s(pLingeData->pLingeData, uiLeftDataLen + 1, pBuf + idx, uiLeftDataLen);
					pLingeData->pLingeData[uiLeftDataLen] = '\0';
					pLingeData->usFromPort = pNetData_->usPort;
					strcpy_s(pLingeData->szFromIp, sizeof(pLingeData->szFromIp), pNetData_->szIp);
					if (!add_lingeData(pLingeData)) {
						if (pLingeData->pLingeData) {
							delete[] pLingeData->pLingeData;
							pLingeData->pLingeData = NULL;
						}
						delete pLingeData;
						pLingeData = NULL;
					}
					break;
				}
			}
			else {
				idx += 1;
			}
		}

		if (pBuf) {
			delete[] pBuf;
			pBuf = NULL;
		}

	}
}

bool uwb_network::Listener::add_lingeData(uwb_network::LingeData * pLingeData_)
{
	bool result = false;
	if (pLingeData_ && pLingeData_->pLingeData && pLingeData_->uiLingeDataLen > 0) {
		if (strlen(pLingeData_->szFromIp) && pLingeData_->usFromPort > 0) {
			char szFrom[32] = { 0 };
			sprintf_s(szFrom, sizeof(szFrom), "%s:%hu", pLingeData_->szFromIp, pLingeData_->usFromPort);
			std::lock_guard<std::mutex> lock(m_mutex4LingeDataList);
			m_netLingeDataList.emplace(szFrom, pLingeData_);
			result = true;
		}
	}
	return result;
}

void uwb_network::Listener::clear_lingeDataList()
{
	std::lock_guard<std::mutex> lock(m_mutex4LingeDataList);
	if (!m_netLingeDataList.empty()) {
		uwb_network::Listener::LingeDataList::iterator iter = m_netLingeDataList.begin();
		while (iter != m_netLingeDataList.end()) {
			auto pLinge = iter->second;
			if (pLinge) {
				if (pLinge->pLingeData) {
					delete[] pLinge->pLingeData;
					pLinge->pLingeData = NULL;
				}
				delete pLinge;
				pLinge = NULL;
			}
			iter = m_netLingeDataList.erase(iter);
		}
	}
}

void uwb_network::Listener::publish(const char * pMsg_)
{
	if (pMsg_ && strlen(pMsg_)) {
		zmsg_t * msg = zmsg_new();
		zmsg_addmem(msg, pMsg_, strlen(pMsg_));
		zmsg_send(&msg, m_pub);
	}
}


