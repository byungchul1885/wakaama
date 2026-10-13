#ifndef LWM2M_RESPONSE_HISTORY_H
#define LWM2M_RESPONSE_HISTORY_H

#include "liblwm2m.h"

/* packet owner가 ACK 송신 중 예약을 고정한다. owner/links는 이력 모듈만 수정한다. */
struct _lwm2m_response_history_ {
    lwm2m_response_peer_t *owner;
    lwm2m_response_history_t *hashNext, *older, *newer;
    time_t receivedAt;
    uint16_t mid;
    bool processing;
};

lwm2m_response_history_t *response_history_acquire(lwm2m_context_t *context,
    uintptr_t session, uint64_t generation, uint16_t mid, bool *duplicate);
/* entry는 이 호출에서 해제된다. 이후 dereference하지 않는다. */
void response_history_release(lwm2m_context_t *context, lwm2m_response_history_t *entry);
void response_history_clear(lwm2m_context_t *context);
#ifdef WAKAAMA_TEST_FAULTS
size_t response_history_test_probes(void);
#endif

#endif
