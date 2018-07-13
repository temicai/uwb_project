#include "precessor.h"

int uwb_processor::recvSubscriber(zloop_t * loop_, zsock_t * reader_, void * param_)
{
	auto pInst = (uwb_processor::Precessor *)param_;
	if (pInst) {
		if (pInst->m_bRunning) {
			zmsg_t * pub_msg = NULL;
			zsock_recv(reader_, "m", &pub_msg);
			if (pub_msg) {
				if (zmsg_size(pub_msg) == 1) {
					zframe_t * frame = zmsg_pop(pub_msg);
					char szMsg[256] = { 0 };
					memcpy_s(szMsg, sizeof(szMsg), zframe_data(frame), zframe_size(frame));
					zframe_destroy(&frame);
					
					auto j = json::parse(szMsg);
					if (!j["type"].is_null()) {
						if (j["type"].is_number()) {
							uint32_t nType = (uint32_t)j["type"];

							j["locator"]



						}
					}


				}
				zmsg_destroy(&pub_msg);
			}
		}
	}
	return -1;
}

int uwb_processor::timeCb(zloop_t * loop_, int timerId_, void * param_)
{
	auto pInst = (uwb_processor::Precessor *)param_;
	if (pInst) {
		if (!pInst->m_bRunning) {
			for (uint32_t i = 0; i < pInst->m_uiSubCount; i++) {
				if (pInst->m_subList[i]) {
					zloop_reader_end(loop_, pInst->m_subList[i]);
				}
			}
			zloop_timer_end(loop_, timerId_);
			return -1;
		}
	}
	return 0;
}

void uwb_processor::startLoop(void * param_)
{
	auto pInst = (uwb_processor::Precessor *)param_;
	if (pInst) {
		zloop_start(pInst->m_loop);
	}
}

uwb_processor::Precessor::Precessor()
{

}

uwb_processor::Precessor::~Precessor()
{

}

int uwb_processor::Precessor::Start()
{

}

int uwb_processor::Precessor::Stop()
{

}



