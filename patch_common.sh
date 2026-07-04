sed -i '/mCommon = common;/i \
            effect_config_t effectConfig;\
            memset(&effectConfig, 0, sizeof(effect_config_t));\
            effectConfig.inputCfg.samplingRate = common.input.base.sampleRate;\
            effectConfig.inputCfg.channels = 3; /* AUDIO_CHANNEL_OUT_STEREO */\
            effectConfig.inputCfg.format = 5; /* AUDIO_FORMAT_PCM_FLOAT */\
            effectConfig.inputCfg.buffer.frameCount = common.input.base.frameRate;\
            effectConfig.outputCfg.samplingRate = common.output.base.sampleRate;\
            effectConfig.outputCfg.channels = 3;\
            effectConfig.outputCfg.format = 5;\
            effectConfig.outputCfg.buffer.frameCount = common.output.base.frameRate;\
            uint32_t replySize = sizeof(int32_t);\
            int32_t replyData = 0;\
            viperContext.handleCommand(EFFECT_CMD_SET_CONFIG, sizeof(effect_config_t), &effectConfig, &replySize, &replyData);\
' /home/Mysticle/ViPERFX_RE_AIDL2/src/ViPER4Aidl.cpp
