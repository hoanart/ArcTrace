# ArcSim — 탄도 시뮬레이터 (Python)

ArcTrace 재연용 기록을 만드는 외부 생성기. 언리얼은 이 출력 파일만 읽는다. 형식 명세는 [FORMAT.md](FORMAT.md).

## 실행
```
pip install -r requirements.txt
python run_sim.py scenarios/sample_salvo.json        # -> ../../Data/Records/sample_salvo/
python run_sim.py scenarios/stress_600.json          # 600발 부하 시나리오
python plot_record.py ../../Data/Records/sample_salvo # 지상궤적·고도 그래프(overview.png)
python -m unittest discover -s tests -v               # 검증 테스트
```

## 모델
- 좌표: WGS84 ECEF(지구 고정, 회전 좌표계)에서 적분. 중력 = 점질량 + J2, 코리올리·원심 항 포함.
- 적분: RK4, 고정 dt 10ms, 시간은 정수 마이크로초. 기록은 100ms 격자 + 발사·탄착 시각.
- 항력: ISA 표준대기 밀도·음속 + 마하수별 항력계수 표. 바람 없음.
- 추진: 일정 추력, 연소시간 동안 질량 선형 감소, 추력 방향 = 속도 방향(중력 선회).
- 탄착: 지면 위 높이가 0 이하가 된 스텝 안에서 선형 근사. 지형은 `height`(평면 또는 고도 격자 파일).
- 투사체 제원(`arcsim/projectiles.py`)은 교과서 수준 일반값과 가상 명칭이다.

## 시나리오 JSON
- `sites`: 발사 지점 이름·위경도.
- `salvos`: 지점, 종류, 발수, 시작 시각, 발사 간격, 방위각·고각, 분산(방위·고각 표준편차, 속도 비율, 위치 퍼짐).
- `seed`: 분산 난수 시드. 같은 시나리오·시드 → 동일 출력(해시가 `meta.json`에 기록됨).
- `height`: `{"flat_height_m": 0}` 또는 `{"grid": "파일.json"}` (언리얼 고도 캐시 굽기 도구 출력).

## 검증 항목 (tests/)
- 좌표 변환 왕복 오차, ISA 기준값
- 진공·비회전 조건 에너지 보존, 단거리 포물선 해석해 비교
- 결정성(두 번 실행 해시 동일), 트랙별 시간 단조 증가, 마지막 샘플 = 탄착, 트랙 우선 정렬
- 고도 격자 쌍선형 조회
