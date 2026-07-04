sed -i '/mState = State::PROCESSING;/a \
            uint32_t replySize = sizeof(int32_t);\
            int32_t replyData = 0;\
            viperContext.handleCommand(EFFECT_CMD_ENABLE, 0, nullptr, &replySize, &replyData);\
' /home/Mysticle/ViPERFX_RE_AIDL2/src/ViPER4Aidl.cpp

sed -i '/mState = State::IDLE;/a \
            uint32_t replySize = sizeof(int32_t);\
            int32_t replyData = 0;\
            viperContext.handleCommand(EFFECT_CMD_DISABLE, 0, nullptr, &replySize, &replyData);\
' /home/Mysticle/ViPERFX_RE_AIDL2/src/ViPER4Aidl.cpp
