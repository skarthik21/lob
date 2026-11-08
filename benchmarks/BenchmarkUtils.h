#pragma once

#if defined(__APPLE__)
#include <pthread.h>
#endif

// Ask the OS to schedule the calling benchmark thread on a performance core.
// macOS: QoS USER_INTERACTIVE. Other platforms: no-op (pin with taskset/isolcpus instead).
inline void setBenchmarkThreadPriority() {
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
}
