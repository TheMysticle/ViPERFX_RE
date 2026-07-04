#include <log/log.h>
#include <string>

namespace android {
namespace hardware {
namespace details {
void errorWriteLog(int tag, const char* info) {
    ALOGE("FMQ Error: tag %d, info %s", tag, info);
}

void logError(const std::string& msg) {
    ALOGE("FMQ logError: %s", msg.c_str());
}
}
}
}
