#include "fjdispatchlite.h"
#include "fjunitframes.h"

class FJTestCall : public FJUnitFrames {
public:
    enum {
	MID_ON_ONCALL,
    };

    virtual int onCall(uint32_t msg, void* buf, uint32_t len); 

    BEGIN_MAP_MESSAGES( FJTestCall )
    MAP_MESSAGES( MID_ON_ONCALL, FJTestCall::onCall )
    END_MAP_MESSAGES()
};

int FJTestCall::onCall(uint32_t msg, void* buf, uint32_t len)
{
    std::string s(static_cast<char *>(buf), len);
    std::cout << "onCall called for instance " << this << ": " << s << ": " << msg << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 擬似処理時間
    return -55;
}

class FJTestHold : public FJUnitFrames {
public:
    enum {
	MID_ON_ONHOLD = 255,
    };

    virtual void run(char *buf, uint len);
    virtual int onHold(uint32_t msg, void* buf, uint32_t len);

    BEGIN_MAP_MESSAGES( FJTestHold )
    MAP_MESSAGES( MID_ON_ONHOLD, FJTestHold::onHold )
    END_MAP_MESSAGES()

};

int FJTestHold::onHold(uint32_t msg, void* buf, uint32_t len)
{
    std::string s(static_cast<char *>(buf), len);
    std::cout << "onHold called for instance " << this << ": " << s << ": " << msg << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 擬似処理時間
    return 112;
}

void FJTestHold::run(char *buf, uint len)
{
    SendMsgSelf_S( MID_ON_ONHOLD, C_MESSAGE_MID, buf, len );
}

int main() {
    FJDispatchLite* dispatch = FJDispatchLite::GetInstance();
    FJTestCall* A1 = new FJTestCall();
    FJTestCall* B1 = new FJTestCall();
    FJTestHold* C1 = new FJTestHold();

    char buf1[] = "a1";
    char buf2[] = "a2";
    char buf3[] = "a3";
    char buf4[] = "b1";
    char buf5[] = "b2";
    char buf6[] = "b3";
    char buf7[] = "c1";
    char buf8[] = "c2";
    char buf9[] = "c3";

    dispatch->postQueue(A1, &FJTestCall::onCall, 1, buf1, 2, true, __FUNCTION__, __LINE__);
    dispatch->postQueue(B1, &FJTestCall::onCall, 2, buf4, 2, true, __FUNCTION__, __LINE__);

    C1->run(buf7, 2);

    dispatch->postQueue(A1, &FJTestCall::onCall, 3, buf2, 2, false, __FUNCTION__, __LINE__);
    dispatch->postQueue(B1, &FJTestCall::onCall, 4, buf5, 2, true, __FUNCTION__, __LINE__);

    C1->run(buf8, 2);

    dispatch->postQueue(A1, &FJTestCall::onCall, 5, buf3, 2, false, __FUNCTION__, __LINE__);
    fjt_handle_t b1_6 = dispatch->postQueue(B1, &FJTestCall::onCall, 6, buf6, 2, true, __FUNCTION__, __LINE__);

    C1->run(buf9, 2);

    std::cout << "POSTEND" << std::endl;

    int result = -1;
    if (dispatch->waitResult(b1_6, 8000, result) == true) {
	std::cout << "B1_6 result: " << result << std::endl;
    } else {
	std::cout << "B1_6 TIMEOUT" << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));  

    std::cout << "DELETE" << std::endl;
    delete A1;
    std::cout << "DELETE1" << std::endl;
    delete B1;
    std::cout << "DELETE2" << std::endl;
    delete C1;
    std::cout << "DELETE3" << std::endl;
    return 0;
}
