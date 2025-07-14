/**
 * Copyright 2025 FJD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __FJUNITFRAMES_H__
#define __FJUNITFRAMES_H__

class FJDispatchLite;
class FJTimerLite;

#ifndef MAP_MESSAGES
#define BEGIN_MAP_MESSAGES(x)
#define MAP_MESSAGES(mid, func) static constexpr auto g_funcptr_##mid = &func;
#define END_MAP_MESSAGES()
#endif //MAP_MESSAGES

#ifndef MAP_EVENTS
#define BEGIN_MAP_EVENTS(x)
#define MAP_EVENTS(mid, func) static constexpr auto g_funcptr_##mid = &func;
#define END_MAP_EVENTS()
#endif //MAP_EVENTS

#define SendEvtSelf_S(mid) FJDispatchLite::GetInstance()->postEvent(this, g_funcptr_##mid, mid, __PRETTY_FUNCTION__, __LINE__)
#define SendMsgSelf_S(mid, prio, buf, size) FJDispatchLite::GetInstance()->postQueue(this, g_funcptr_##mid, mid, buf, size, true, __PRETTY_FUNCTION__, __LINE__)
#define SendMsgSelf_P(mid, prio, buf, size) FJDispatchLite::GetInstance()->postQueue(this, g_funcptr_##mid, mid, buf, size, false, __PRETTY_FUNCTION__, __LINE__)
#define CreateTimer(mf, msec) FJTimerLite::GetInstance()->createTimer(this, mf, msec, __PRETTY_FUNCTION__, __LINE__)

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
