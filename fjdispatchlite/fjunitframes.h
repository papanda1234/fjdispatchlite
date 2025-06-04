#ifndef __FJUNITFRAMES_H__
#define __FJUNITFRAMES_H__

class FJDispatchLite;
class FJTimerLite;

#define BEGIN_MAP_MESSAGE(x)
#define MAP_MESSAGE(mid, func) static constexpr auto g_funcptr_##mid = &func;
#define END_MAP_MESSAGE()

#define _SendMsgSelf(mid, prio, buf, size) FJDispatchLite::GetInstance()->postQueue(this, g_funcptr_##mid, mid, buf, size, true, __FUNCTION__, __LINE__)
#define _SendMsgSelf_P(mid, prio, buf, size) FJDispatchLite::GetInstance()->postQueue(this, g_funcptr_##mid, mid, buf, size, false, __FUNCTION__, __LINE__)
#define _CreateTimer(mf, msec) FJTimerLite::GetInstance()->createTimer(this, mf, msec, __FUNCTION__, __LINE__)

/**
 * @enum 実行優先度
 * @note 現状は未使用。
 */
typedef enum {
    C_MESSAGE_HIGH,
    C_MESSAGE_MID,
    C_MESSAGE_LOW,
} EN_MSG_ID;

/**
 * @brief ディスパッチ用既定クラス
 */
class FJUnitFrames {
public:
    FJUnitFrames() {};
    virtual ~FJUnitFrames() {};
};

#endif
