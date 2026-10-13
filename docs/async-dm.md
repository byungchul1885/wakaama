# DM 지연 IO의 소유권과 재개

완료한 별도 CON 응답 MID는 context가 peer 주소값/세대별 lazy 해시와 만료 순서 목록으로
소유한다. 응답 하나당 필요한 항목만 할당하고 exchange lifetime 이전에는 덮어쓰지 않는다.
`WAKAAMA_COAP_RESPONSE_HISTORY_SIZE`는 peer/세대별 1~65,536개(기본 256),
`WAKAAMA_COAP_RESPONSE_HISTORY_TOTAL_SIZE`는 context 전체 peer 상한~1,048,576개(기본 4,096)이다.
PUBLIC 정의로 모든 consumer에 전파하며 두 상한을 함께 적용한다. ACK 송신 중에는 중복 ACK도
processing 예약을 고정하여 재진입·만료에서 항목을 해제하지 않는다. ACK 송신 실패의 새 예약은
회수하고, 이미 처리한 중복 응답의 기록은 보존한다. context close에서 모든 항목을 해제한다.
시험용 stack context도 `response_history_clear`로 context 소유 할당을 회수해야 한다.

조립된 iPATCH 입력의 suspended 사본은 context의 Composite Write 입력 한도를 사용한다. 기타 입력은 1MiB를 넘기지 않는다. CMake의 `WAKAAMA_COAP_BLOCK1_HISTORY_SIZE`는 1~65,536개 범위에서 모든 consumer에 같은 context layout을 전파한다. 기본값은 1,024개이며 제품은 입력 크기·동시 교환과 고정 메모리 비용을 함께 고려해 설정한다.

`lwm2m_set_dm_async_callback`은 Linux 제품의 protocol owner가 선택적으로 연결하는 경계다. 기존 Execute 전용 deferred API와 동기 Object callback은 그대로 동작한다.

외부 DM callback에서 IO를 접수하기 전에 `lwm2m_suspend_current_request`를 호출한다. core는 현재 header, 조립된 payload, 논리 MID, 실제 수신 MID, Token, 서버 ID와 세대를 복사한다. callback의 임시 오류 반환은 최종 응답으로 보내지 않으며 CON에는 빈 ACK를 보낸다. NON은 대기 중 응답하지 않는다. 현재 입력 pointer는 callback 밖에서 빌리지 않는다.

IO 완료 후 같은 protocol owner thread가 `lwm2m_resume_suspended_request`를 호출한다. core는 현재 서버 목록에서 같은 ID와 세대를 다시 찾고 복사한 요청으로 동일 callback을 재개한다. 제품 owner는 완료한 IO를 불변 단계별 결과로 보존해야 한다. 이미 실행한 mutation을 새 요청 ID로 다시 실행하면 안 된다. callback 재개 시 바뀐 입력이나 단계 순서는 거절한다.

최종 callback 출력의 header와 할당된 payload는 추가 할당 없이 core가 소유한다. 직렬화/transaction 준비 실패에도 이 결과를 보존하므로 최종 callback을 다시 실행하지 않는다. 최종 응답은 별도 CON transaction에 인계하고, 그 뒤 전송 재시도는 transaction owner가 수행한다. 정상 Read/Block2/관찰 제출 통지는 기존 packet 경계를 사용한다. IO를 기다리며 생성한 미제출 Composite snapshot 후보는 폐기한다.

세대 교체, session close, 취소와 context close는 RELEASE로 복사본을 해제한다. 진행 중인 외부 영속 IO의 원자적 중단을 보장하지 않으므로 제품은 원래 operation ID로 응답 유실·불확정 결과를 복구한다. worker는 Wakaama context/object/session/transaction pointer에 접근하지 않는다.

준비 IO를 마친 Execute는 같은 ID를 기존 deferred 실행 owner에게 인계할 수 있다. 일반 suspended 요청을 Execute용 completion API로 조기 완료하는 것은 거절한다. 재개 입력은 transport가 session을 닫아도 현재 호출이 소유하며, core의 요청 노드는 stable ID로 다시 찾는다.

NSTART 보류와 transport 오류는 Read 제출 성공으로 취급하지 않는다. 미제출 transaction은 회수하고 최종 Write 결과는 다시 실행하지 않는다. Observe의 실패한 일시 준비는 읽기 전용 callback으로 다시 만들며 제품은 완료 IO cache를 보존한다. 실제 제출 뒤에는 transaction owner가 CON 재전송을 담당한다.

Linux 직접 회귀는 `tests/core_management_deferred_tests.c`의 suspended Read/Write, 세대 교체, 전송 재시도, 최종 출력 할당 실패와 Execute owner 인계를 포함한다. 이 시험은 실제 DTLS peer·저장장치 E2E를 대체하지 않는다.
