#pragma once

#include "zmq.h"
#include "czmq.h"
#include "Eigen/Dense"
#include "json.hpp"

#include <sys/timeb.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <map>
#include <queue>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

using json = nlohmann::json;

namespace uwb_processor
{

	typedef struct tagLocatorInfo
	{
		unsigned int uiLocatorId;
		unsigned int uiZoneId;


	} UWBLocator;

	typedef struct tagZoneInfo
	{


	} UWBZone;

	typedef struct tagTagInfo
	{

	} UWBTag;



	class Precessor
	{
	public:
		explicit Precessor();
		~Precessor();
		int Start();
		int Stop();

	private:
		zloop_t * m_loop;
		zsock_t ** m_subList;
		unsigned int m_uiSubCount;
		bool m_bRunning;

	protected:

		friend int recvSubscriber(zloop_t * loop_, zsock_t *, void *);
		friend int timeCb(zloop_t *, int, void *);
		friend void startLoop(void *);


	};
}

