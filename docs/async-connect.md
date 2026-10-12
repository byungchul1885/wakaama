# 비동기 연결 준비

LwM2M 1.1 client의 `connectionPendingCallback`은 opt-in 연결 준비 hook이다. `lwm2m_connect_server()`가 NULL을 반환하면 core가 같은 MAIN 호출에서 Security IID와 context userData로 callback을 호출한다. callback은 worker pointer나 완료 결과를 가져오지 않고 현재 연결 요청이 준비 중인지 스칼라 상태만 읽는다. 기본 callback NULL과 LwM2M 1.0 경로는 기존 동기 계약을 유지한다.

true이면 등록 attempt와 Bootstrap 실패를 소비하지 않고 hold-off 상태에서 1초 뒤 다시 연결을 시도한다. 기존 transaction timer와 Observe는 계속 진행한다. false이면 준비가 끝난 연결 실패이며 등록은 설정된 attempt/retry 정책, Bootstrap은 기존 실패 정책을 적용한다. 연결 성공 뒤에는 일반 등록·Bootstrap packet을 생성한다.

실제 주소 조회·취소·cache·worker 종료는 transport/application owner가 책임진다. core에는 IO 구현이나 worker의 raw context/session 접근을 추가하지 않는다.
