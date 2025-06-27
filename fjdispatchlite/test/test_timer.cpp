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

};

int FJTestHold::onHold(uint32_t msg, void* buf, uint32_t len)
{
    std::string s(static_cast<char *>(buf), len);
    std::cout << "onHold called for instance " << this << ": " << s << ": " << msg << std::endl;
    CreateTimer(&FJTestHold::onTimer, 500);
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

    fjt_handle_t t1 = timer->createTimer(C1, &FJTestHold::onTimer, 1000, __FUNCTION__, __LINE__);

    std::this_thread::sleep_for(std::chrono::seconds(5));  

    FJTimerLite::GetInstance()->removeTimer(t1);

    std::cout << "DELETE" << std::endl;
    delete C1;
    std::cout << "DELETE3" << std::endl;
    return 0;
}
