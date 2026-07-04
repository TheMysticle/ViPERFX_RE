#define LOG_TAG "ViPER4AIDL"
#include "ViPER4Aidl.h"
#include "ViPER4Android.h"
#include "AidlUtils.h"
#include <log/log.h>
#include <aidl/android/hardware/audio/effect/DefaultExtension.h>
#include <system/audio_effect.h>
static constexpr uint32_t kEventFlagDataMqUpdate = 0x1 << 12;
using aidl::android::hardware::audio::effect::CommandId;
using aidl::android::hardware::audio::effect::DefaultExtension;
using aidl::android::hardware::audio::effect::Descriptor;
using aidl::android::hardware::audio::effect::Flags;
using aidl::android::hardware::audio::effect::IEffect;
using aidl::android::hardware::audio::effect::Parameter;
using aidl::android::hardware::audio::effect::RetCode;
using aidl::android::hardware::audio::effect::State;
using aidl::android::hardware::audio::effect::VendorExtension;
using aidl::android::media::audio::common::AudioUuid;
using aidl::android::media::audio::common::PcmType;
using android::hardware::EventFlag;
using ViPER4Android::getFrameSizeInBytes;
using ViPER4Android::stringToUuid;
static const AudioUuid kType = stringToUuid(ViPER4Android::kTypeString);
static const AudioUuid kUuid = stringToUuid(ViPER4Android::kUuidString);
static const Descriptor kDescriptor = {
        .common = {
                .id = {
                        .type = kType,
                        .uuid = kUuid,
                        .proxy = std::nullopt
                },
                .flags = {
                        .type = Flags::Type::INSERT,
                        .insert = Flags::Insert::LAST,
                },
                .name = ViPER4Android::kName,
                .implementor = ViPER4Android::kImplementor,
        },
};
ndk::ScopedAStatus ViPER4AIDL::open(const Parameter::Common &common,
                                    const std::optional<Parameter::Specific> &specific,
                                    IEffect::OpenEffectReturn *oer) {
    ALOGI("open: input channelMask tag=%d, output channelMask tag=%d",
          static_cast<int>(common.input.base.channelMask.getTag()),
          static_cast<int>(common.output.base.channelMask.getTag()));
    ALOGI("open: input channelCount=%d, output channelCount=%d",
          (int)ViPER4Android::getChannelCount(common.input.base.channelMask),
          (int)ViPER4Android::getChannelCount(common.output.base.channelMask));
    ALOGI("open: input sampleRate=%d, output sampleRate=%d",
          common.input.base.sampleRate, common.output.base.sampleRate);
    ALOGI("open: input frameCount=%d, output frameCount=%d",
          (int)common.input.frameCount, (int)common.output.frameCount);
    ALOGI("open: input format=%s, output format=%s",
          toString(common.input.base.format.pcm).c_str(),
          toString(common.output.base.format.pcm).c_str());
    if (common.input.base.format.pcm != PcmType::FLOAT_32_BIT ||
        common.output.base.format.pcm != PcmType::FLOAT_32_BIT) {
        ALOGE("open: unsupported PCM type (input: %s, output: %s)",
              toString(common.input.base.format.pcm).c_str(),
              toString(common.output.base.format.pcm).c_str());
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    std::lock_guard lg(mImplMutex);
    if (mState != State::INIT) {
        ALOGD("open: already opened");
        return ndk::ScopedAStatus::ok();
    }
    size_t inputFrameSize = getFrameSizeInBytes(common.input.base.format, common.input.base.channelMask);
    size_t outputFrameSize = getFrameSizeInBytes(common.output.base.format, common.output.base.channelMask);
    ALOGI("open: inputFrameSize=%d, outputFrameSize=%d", (int)inputFrameSize, (int)outputFrameSize);
    /* EffectContext constructor start */
            effect_config_t effectConfig;
            memset(&effectConfig, 0, sizeof(effect_config_t));
            effectConfig.inputCfg.samplingRate = common.input.base.sampleRate;
            effectConfig.inputCfg.channels = 3; /* AUDIO_CHANNEL_OUT_STEREO */
            effectConfig.inputCfg.format = 5; /* AUDIO_FORMAT_PCM_FLOAT */
            effectConfig.inputCfg.buffer.frameCount = common.input.frameCount;
            effectConfig.outputCfg.samplingRate = common.output.base.sampleRate;
            effectConfig.outputCfg.channels = 3;
            effectConfig.outputCfg.format = 5;
            effectConfig.outputCfg.buffer.frameCount = common.output.frameCount;
            uint32_t replySizeCmd = sizeof(int32_t);
            int32_t replyDataCmd = 0;
            viperContext.handleCommand(EFFECT_CMD_SET_CONFIG, sizeof(effect_config_t), &effectConfig, &replySizeCmd, &replyDataCmd);
    mCommon = common;
    size_t inBufferSizeInFloat = common.input.frameCount * inputFrameSize / sizeof(float);
    size_t outBufferSizeInFloat = common.output.frameCount * outputFrameSize / sizeof(float);
    ALOGI("open: inBufferSizeInFloat=%d, outBufferSizeInFloat=%d", (int)inBufferSizeInFloat, (int)outBufferSizeInFloat);
    // only status FMQ use the EventFlag
    mStatusMQ = std::make_shared<StatusMQ>(1, true /* configureEventFlagWord */);
    mInputMQ = std::make_shared<DataMQ>(inBufferSizeInFloat);
    mOutputMQ = std::make_shared<DataMQ>(outBufferSizeInFloat);
    if (!mStatusMQ->isValid() || !mInputMQ->isValid() || !mOutputMQ->isValid()) {
        ALOGE("open: invalid FMQs (status: %d, input: %d, output: %d)",
              mStatusMQ->isValid(), mInputMQ->isValid(), mOutputMQ->isValid());
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    android::status_t status = EventFlag::createEventFlag(mStatusMQ->getEventFlagWord(), &mEventFlag);
    if (status != android::OK || mEventFlag == nullptr) {
        ALOGE("open: failed to create event flag");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    mWorkBuffer.resize(std::max(inBufferSizeInFloat, outBufferSizeInFloat));
    /* EffectContext constructor end */
    mState = State::IDLE;
    {
        uint32_t replySize = sizeof(int32_t);
        int32_t replyData = 0;
        viperContext.handleCommand(EFFECT_CMD_DISABLE, 0, nullptr, &replySize, &replyData);
    }
    dupeFmq(oer);
    if (createThread(ViPER4Android::kName) != RetCode::SUCCESS) {
        ALOGE("open: failed to create thread");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    return ndk::ScopedAStatus::ok();
}
ndk::ScopedAStatus ViPER4AIDL::close() {
    {
        std::lock_guard lg(mImplMutex);
        
        if (mState == State::INIT) {
            ALOGD("close: already closed");
            return ndk::ScopedAStatus::ok();
        }
        if (mState == State::PROCESSING) {
            ALOGE("close: cannot close while processing");
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        mState = State::INIT;
    }
    if (notifyEventFlag(mDataMqNotEmptyEf) != RetCode::SUCCESS) {
        ALOGE("close: failed to notify not empty event flag");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    if (destroyThread() != RetCode::SUCCESS) {
        ALOGE("close: failed to destroy thread");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    {
        std::lock_guard lg(mImplMutex);
        EventFlag::deleteEventFlag(&mEventFlag);
    }
    return ndk::ScopedAStatus::ok();
}
ndk::ScopedAStatus ViPER4AIDL::getDescriptor(Descriptor *descriptor) {
    if (descriptor == nullptr) {
        ALOGE("getDescriptor: descriptor is null");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    *descriptor = kDescriptor;
    return ndk::ScopedAStatus::ok();
}
ndk::ScopedAStatus ViPER4AIDL::command(CommandId commandId) {
    std::lock_guard lg(mImplMutex);
    if (mState == State::INIT) {
        ALOGE("command: instance not open");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    switch (commandId) {
        case CommandId::START: {
            if (mState == State::PROCESSING) {
                ALOGD("command: already started");
                return ndk::ScopedAStatus::ok();
            }
            mState = State::PROCESSING;
            {
                uint32_t replySize = sizeof(int32_t);
                int32_t replyData = 0;
                viperContext.handleCommand(EFFECT_CMD_ENABLE, 0, nullptr, &replySize, &replyData);
            }
            if (notifyEventFlag(mDataMqNotEmptyEf) != RetCode::SUCCESS) {
                ALOGE("command: failed to notify not empty event flag");
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
            }
            startThread();
            break;
        }
        case CommandId::STOP: {
            if (mState == State::IDLE) {
                ALOGD("command: already stopped");
                return ndk::ScopedAStatus::ok();
            }
            mState = State::IDLE;
            {
                uint32_t replySize = sizeof(int32_t);
                int32_t replyData = 0;
                viperContext.handleCommand(EFFECT_CMD_DISABLE, 0, nullptr, &replySize, &replyData);
            }
            if (notifyEventFlag(mDataMqNotEmptyEf) != RetCode::SUCCESS) {
                ALOGE("command: failed to notify not empty event flag");
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
            }
            stopThread();
            break;
        }
        case CommandId::RESET: {
            mState = State::IDLE;
            {
                uint32_t replySize = sizeof(int32_t);
                int32_t replyData = 0;
                viperContext.handleCommand(EFFECT_CMD_DISABLE, 0, nullptr, &replySize, &replyData);
            }
            if (notifyEventFlag(mDataMqNotEmptyEf) != RetCode::SUCCESS) {
                ALOGE("command: failed to notify not empty event flag");
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
            }
            stopThread();
            resetBuffer();
            break;
        }
        default:
            ALOGE("command: unknown commandId (%s)", toString(commandId).c_str());
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    return ndk::ScopedAStatus::ok();
}
ndk::ScopedAStatus ViPER4AIDL::getState(State *state) {
    if (state == nullptr) {
        ALOGE("getState: state is null");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    *state = mState;
    return ndk::ScopedAStatus::ok();
}
ndk::ScopedAStatus ViPER4AIDL::setParameter(const Parameter &parameter) {
    std::lock_guard lg(mImplMutex);
    const auto &tag = parameter.getTag();
    switch (tag) {
        case Parameter::Tag::common: {
            auto common = parameter.get<Parameter::Tag::common>();
            if (common.input.base.format.pcm != PcmType::FLOAT_32_BIT ||
                common.output.base.format.pcm != PcmType::FLOAT_32_BIT) {
                ALOGE("setParameter: common: unsupported PCM type (input: %s, output: %s)",
                      toString(common.input.base.format.pcm).c_str(),
                      toString(common.output.base.format.pcm).c_str());
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
#if VIPER_AIDL_VERSION >= 2
            if (!mWorkBuffer.empty() && mInputMQ != nullptr && mOutputMQ != nullptr) {
                size_t prevInputFrameSize = getFrameSizeInBytes(
                    mCommon.input.base.format, mCommon.input.base.channelMask);
                size_t prevOutputFrameSize = getFrameSizeInBytes(
                        mCommon.output.base.format, mCommon.output.base.channelMask);
                size_t inputFrameSize = getFrameSizeInBytes(
                        common.input.base.format, common.input.base.channelMask);
                size_t outputFrameSize = getFrameSizeInBytes(
                        common.output.base.format, common.output.base.channelMask);
                bool needUpdateMq = false;
                if (inputFrameSize != prevInputFrameSize || mCommon.input.frameCount != common.input.frameCount) {
                    mInputMQ.reset();
                    needUpdateMq = true;
                }
                if (outputFrameSize != prevOutputFrameSize ||
                    mCommon.output.frameCount != common.output.frameCount) {
                    mOutputMQ.reset();
                    needUpdateMq = true;
                }
                if (needUpdateMq && mEventFlag->wake(kEventFlagDataMqUpdate) != ::android::OK) {
                    ALOGE("setParameter: common: failed to wake event flag");
                    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
                }
            }
#endif
            effect_config_t effectConfig;
            memset(&effectConfig, 0, sizeof(effect_config_t));
            effectConfig.inputCfg.samplingRate = common.input.base.sampleRate;
            effectConfig.inputCfg.channels = 3; /* AUDIO_CHANNEL_OUT_STEREO */
            effectConfig.inputCfg.format = 5; /* AUDIO_FORMAT_PCM_FLOAT */
            effectConfig.inputCfg.buffer.frameCount = common.input.frameCount;
            effectConfig.outputCfg.samplingRate = common.output.base.sampleRate;
            effectConfig.outputCfg.channels = 3;
            effectConfig.outputCfg.format = 5;
            effectConfig.outputCfg.buffer.frameCount = common.output.frameCount;
            uint32_t replySize = sizeof(int32_t);
            int32_t replyData = 0;
            viperContext.handleCommand(EFFECT_CMD_SET_CONFIG, sizeof(effect_config_t), &effectConfig, &replySize, &replyData);
            mCommon = common;
            return ndk::ScopedAStatus::ok();
        }
        case Parameter::Tag::specific: {
            auto specific = parameter.get<Parameter::Tag::specific>();
            if (specific.getTag() != Parameter::Specific::Tag::vendorEffect) {
                ALOGE("setParameter: specific: unsupported tag (%s)", toString(specific.getTag()).c_str());
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
            
            auto vendorEffect = specific.get<Parameter::Specific::Tag::vendorEffect>();
            std::optional<DefaultExtension> defaultExtension;
            if (vendorEffect.extension.getParcelable(&defaultExtension) != STATUS_OK || !defaultExtension.has_value()) {
                ALOGE("setParameter: specific: failed to get default extension");
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
            auto cmd = defaultExtension->bytes;
            int32_t replyData = 0;
            uint32_t replySize = sizeof(replyData);
            if (viperContext.handleCommand(EFFECT_CMD_SET_PARAM,
                                           cmd.size(), cmd.data(), &replySize, &replyData) != 0 || replyData != 0) {
                ALOGE("setParameter: specific: failed to handle command");
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
            return ndk::ScopedAStatus::ok();
        }
        default:
            ALOGE("setParameter: unsupported parameter tag (%s)", toString(tag).c_str());
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
}
ndk::ScopedAStatus ViPER4AIDL::getParameter(const Parameter::Id &parameterId, Parameter *param) {
    if (param == nullptr) {
        ALOGE("getParameter: param is null");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    std::lock_guard lg(mImplMutex);
    const auto &tag = parameterId.getTag();
    switch (tag) {
        case Parameter::Id::commonTag: {
            auto commonTag = parameterId.get<Parameter::Id::Tag::commonTag>();
            if (commonTag != Parameter::Tag::common) {
                ALOGE("getParameter: commonTag: unsupported tag (%s)", toString(commonTag).c_str());
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
            param->set<Parameter::Tag::common>(mCommon);
            return ndk::ScopedAStatus::ok();
        }
        case Parameter::Id::vendorEffectTag: {
            const auto &vendorEffectTag = parameterId.get<Parameter::Id::Tag::vendorEffectTag>();
            std::optional<DefaultExtension> cmdDefaultExtension;
            if (vendorEffectTag.extension.getParcelable(&cmdDefaultExtension) != STATUS_OK || !cmdDefaultExtension.has_value()) {
                ALOGE("getParameter: vendorEffectTag: failed to get default extension");
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
            const auto &cmd = cmdDefaultExtension->bytes;
            if (cmd.size() < sizeof(effect_param_t)) {
                ALOGE("getParameter: vendorEffectTag: cmd size too small");
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
            uint32_t replySize = 4096;
            std::vector<uint8_t> reply(replySize, 0);
            if (viperContext.handleCommand(EFFECT_CMD_GET_PARAM,
                                           cmd.size(), (void*)cmd.data(), &replySize, reply.data()) != 0) {
                ALOGE("getParameter: vendorEffectTag: failed to handle command");
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
            reply.resize(replySize);
            DefaultExtension replyDefaultExtension;
            replyDefaultExtension.bytes = std::move(reply);
            VendorExtension replyVendorExtension;
            replyVendorExtension.extension.setParcelable(replyDefaultExtension);
            Parameter::Specific specific;
            specific.set<Parameter::Specific::vendorEffect>(replyVendorExtension);
            param->set<Parameter::Tag::specific>(specific);
            return ndk::ScopedAStatus::ok();
        }
        default:
            ALOGD("getParameter: unsupported parameter tag (%s)", toString(tag).c_str());
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
}
#if VIPER_AIDL_VERSION >= 2
ndk::ScopedAStatus ViPER4AIDL::reopen(IEffect::OpenEffectReturn *oer) {
    std::lock_guard lg(mImplMutex);
    if (mState == State::INIT) {
        ALOGE("reopen: already closed");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    dupeFmqWithReopen(oer);
    return ndk::ScopedAStatus::ok();
}
#endif
void ViPER4AIDL::process() {
    /**
     * wait for the EventFlag without lock, it's ok because the mEventFlag pointer will not change
     * in the life cycle of workerThread (threadLoop).
     */
    uint32_t efState = 0;
    if (!mEventFlag ||
        mEventFlag->wait(mDataMqNotEmptyEf, &efState, 0 /* no timeout */, true /* retry */) != ::android::OK ||
        !(efState & mDataMqNotEmptyEf)) {
        return;
    }
    {
        std::lock_guard lg(mImplMutex);
#if VIPER_AIDL_VERSION >= 3
        if (mState != State::PROCESSING && mState != State::DRAINING) {
#else
        if (mState != State::PROCESSING) {
#endif
            return;
        }
        if (!mInputMQ || !mOutputMQ) {
            return;
        }
        assert(mWorkBuffer.size() >= std::max(mInputMQ->availableToRead(), mOutputMQ->availableToWrite()));
        auto processSamples = std::min(mInputMQ->availableToRead(), mOutputMQ->availableToWrite());
        if (processSamples) {
            auto buffer = static_cast<float *>(mWorkBuffer.data());
            mInputMQ->read(buffer, processSamples);
            IEffect::Status status = effectProcessImpl(buffer, buffer, static_cast<int32_t>(processSamples));
            mOutputMQ->write(buffer, status.fmqProduced);
            mStatusMQ->writeBlocking(&status, 1);
        }
    }
}
void ViPER4AIDL::dupeFmq(IEffect::OpenEffectReturn* oer) {
    if (oer && mStatusMQ && mInputMQ && mOutputMQ) {
        oer->statusMQ = mStatusMQ->dupeDesc();
        oer->inputDataMQ = mInputMQ->dupeDesc();
        oer->outputDataMQ = mOutputMQ->dupeDesc();
    }
}
void ViPER4AIDL::dupeFmqWithReopen(IEffect::OpenEffectReturn* oer) {
    size_t inputFrameSize = getFrameSizeInBytes(mCommon.input.base.format, mCommon.input.base.channelMask);
    size_t outputFrameSize = getFrameSizeInBytes(mCommon.output.base.format, mCommon.output.base.channelMask);
    const size_t inBufferSizeInFloat = mCommon.input.frameCount * inputFrameSize / sizeof(float);
    const size_t outBufferSizeInFloat = mCommon.output.frameCount * outputFrameSize / sizeof(float);
    const size_t bufferSize = std::max(inBufferSizeInFloat, outBufferSizeInFloat);
    if (!mInputMQ) {
        mInputMQ = std::make_shared<DataMQ>(inBufferSizeInFloat);
    }
    if (!mOutputMQ) {
        mOutputMQ = std::make_shared<DataMQ>(outBufferSizeInFloat);
    }
    if (mWorkBuffer.size() != bufferSize) {
        mWorkBuffer.resize(bufferSize);
    }
    dupeFmq(oer);
}
RetCode ViPER4AIDL::notifyEventFlag(uint32_t flag) {
    if (!mEventFlag) {
        ALOGE("notifyEventFlag: StatusEventFlag invalid");
        return RetCode::ERROR_EVENT_FLAG_ERROR;
    }
    if (mEventFlag->wake(flag) != ::android::OK) {
        ALOGE("notifyEventFlag: failed to wake event flag");
        return RetCode::ERROR_EVENT_FLAG_ERROR;
    }
    return RetCode::SUCCESS;
}
void ViPER4AIDL::resetBuffer() {
    // reset buffer status by abandon input data in FMQ
    if (mStatusMQ) {
        std::vector<IEffect::Status> status(mStatusMQ->availableToRead());
        mStatusMQ->read(status.data(), status.size());
    }
    if (mInputMQ) {
        auto buffer = static_cast<float*>(mWorkBuffer.data());
        mInputMQ->read(buffer, mInputMQ->availableToRead());
    }
}
IEffect::Status ViPER4AIDL::effectProcessImpl(float *in, float *out, int32_t samples) {
    static int logCounter = 0;
    if (logCounter % 500 == 0) {
        // Log first 4 samples (2 stereo frames) before processing
        if (samples >= 4) {
            ALOGI("effectProcessImpl[%d]: samples=%d, IN L0=%.6f R0=%.6f L1=%.6f R1=%.6f",
                  logCounter, samples, in[0], in[1], in[2], in[3]);
        }
    }
    viperContext.process(in, out, samples);
    if (logCounter % 500 == 0) {
        // Log first 4 samples (2 stereo frames) after processing
        if (samples >= 4) {
            ALOGI("effectProcessImpl[%d]: samples=%d, OUT L0=%.6f R0=%.6f L1=%.6f R1=%.6f",
                  logCounter, samples, out[0], out[1], out[2], out[3]);
        }
    }
    logCounter++;
    return {
        .status = STATUS_OK,
        .fmqConsumed = samples,
        .fmqProduced = samples
    };
}
extern "C" binder_exception_t queryEffect(const AudioUuid *audioUuid, Descriptor *descriptor) {
    ALOGD("ViPER4AIDL queryEffect called");
    if (audioUuid == nullptr || descriptor == nullptr) {
        ALOGE("queryEffect: audioUuid or descriptor is null");
        return EX_ILLEGAL_ARGUMENT;
    }
    if (*audioUuid != kUuid) {
        ALOGE("queryEffect: invalid uuid");
        return EX_ILLEGAL_ARGUMENT;
    }
    ALOGD("ViPER4AIDL queryEffect success");
    *descriptor = kDescriptor;
    return EX_NONE;
}
extern "C" binder_exception_t createEffect(const AudioUuid *audioUuid,
                                           std::shared_ptr<IEffect> *instanceSp) {
    if (audioUuid == nullptr || instanceSp == nullptr) {
        ALOGE("createEffect: audioUuid or instanceSp is null");
        return EX_ILLEGAL_ARGUMENT;
    }
    *instanceSp = ndk::SharedRefBase::make<ViPER4AIDL>();
    return EX_NONE;
}
extern "C" binder_exception_t destroyEffect(const std::shared_ptr<IEffect> &instanceSp) {
    if (!instanceSp) {
        ALOGE("destroyEffect: instanceSp is null");
        return EX_ILLEGAL_ARGUMENT;
    }
    Descriptor descriptor;
    ndk::ScopedAStatus status = instanceSp->getDescriptor(&descriptor);
    if (!status.isOk()) {
        ALOGE("destroyEffect: failed to get descriptor, status: %s", status.getDescription().c_str());
        return EX_ILLEGAL_STATE;
    }
    State state;
    status = instanceSp->getState(&state);
    if (!status.isOk()) {
        ALOGE("destroyEffect: failed to get state, status: %s", status.getDescription().c_str());
        return EX_ILLEGAL_STATE;
    }
#if VIPER_AIDL_VERSION >= 3
    instanceSp->command(CommandId::RESET);
    instanceSp->close();
#else
    if (state != State::INIT) {
        ALOGE("destroyEffect: can not destroy instance in state: %s", toString(state).c_str());
        return EX_ILLEGAL_STATE;
    }
#endif
    return EX_NONE;
}