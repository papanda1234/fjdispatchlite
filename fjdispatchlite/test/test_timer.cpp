#include "fjdispatchlite.h"
#include "fjtimerlite.h"
#include "fjunitframes.h"

class FJTestHold : public FJUnitFrames {
public:
    enum {
	MID_ON_ONHOLD = 255,
    };


    virtual void run(char *buf, uint len);
    virtual int onHold(uint32_t msg, void* buf, uint32_t len);
    virtual int onTimer(fjt_handle_t handle, fjt_time_t now);

    BEGIN_MAP_MESSAGES( FJTestHold )
    MAP_MESSAGES( MID_ON_ONHOLD, FJTestHold::onHold )
    END_MAP_MESSAGES()

	bool running_;
};

int FJTestHold::onHold(uint32_t msg, void* buf, uint32_t len)
{
    std::string s(static_cast<char *>(buf), len);
    std::cout << "onHold called for instance " << this << ": " << s << ": " << msg << std::endl;
	if (running_) {
		CreateTimer(&FJTestHold::onTimer, 500);
	}
    return 0;
}

int FJTestHold::onTimer(fjt_handle_t timer, fjt_time_t now)
{
    std::cout << "***onHoldTimer***" << std::endl;
    SendMsgSelf_S( MID_ON_ONHOLD, C_MESSAGE_MID, NULL, 0 );
    return -1;
}

void FJTestHold::run(char *buf, uint len)
{
}

int main() {
    FJTimerLite* timer = FJTimerLite::GetInstance();
    FJDispatchLite* dispatch = FJDispatchLite::GetInstance();
    FJTestHold *C1 = new FJTestHold();

	C1->running_ = true;

    std::cerr << "START" << std::endl;

    fjt_handle_t t1 = timer->createTimer(C1, &FJTestHold::onTimer, 1000, __FUNCTION__, __LINE__);

    std::cerr << "RUN" << std::endl;

	sleep(5);

	timer->removeTimer(t1);
	C1->running_ = false;
	
	sleep(5);
   
	std::cout << "DELETE" << std::endl;
	delete C1;

	std::cout << "DELETE3" << std::endl;
	return 0;
}
