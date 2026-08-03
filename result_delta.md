# Anh huong cua Delta decode den bang Simulation Results

Ngay do: 2026-07-22 (Asia/Bangkok)

## Ket luan ngan

- Du doan `Delta decodes = 512` la dung: voi corpus 256 fixture, cap `T=1` va `T=3` cho `(3 - 1) * 256 = 512` decode bo sung.
- Sweet spot de dua vao paper van la `T=1`/`T=3`. So voi `T=1`/`T=5`, sai khac lon nhat cua estimator `Pcycles/decode` chi la `0.000160%`, trong khi `Delta decodes` tang gap doi tu 512 len 1024.
- `T=1`/`T=2` la lua chon nhanh neu chi can probe: sai khac lon nhat so voi `T=5` la `0.014779%`. Tuy nhien no van co bias warm-up nho va nhat quan, dac biet tren fastest.
- Khi tang `T`, `Delta Pcycles` tang gan tuyen tinh theo `Delta decodes`; `Pcycles/decode` va speedup hoi tu. Tang tiep sau `T=3` khong dem lai cai thien co y nghia tren simulator deterministic nay.
- Phat hien quan trong hon anh huong cua `T`: cac dong fastest HQC-192 va HQC-256 trong paper cu dung corpus truoc fixture fix. Voi corpus hien tai, chi phi nhanh lan luot la khoang `65,880` va `133,523` Pcycles/decode, khong phai `50,223` va `80,655`.

## Phuong phap

Paper dung mot baseline `T=1`, sau do tru khoi mot run co upper `T` lon hon:

```text
Delta Pcycles(T) = Pcycles_T - Pcycles_1
Delta decodes(T) = (T - 1) * 256
Estimated Pcycles/decode(T) = Delta Pcycles(T) / Delta decodes(T)
Speedup(T) = Scalar Pcycles/decode(T) / Fastest Pcycles/decode(T)
```

Sweep trong lan nay:

```text
T = 1, 2, 3, 5
upper T = 2 -> Delta decodes = 256
upper T = 3 -> Delta decodes = 512
upper T = 5 -> Delta decodes = 1024
```

Lenh chay cho moi diem:

```sh
scripts/sim_decode.sh --variant scalar|fastest --level 128|192|256 \
  --bench decode --iters T
```

Moi run deu in `result=PASS`. Raw logs nam trong `results/sim_decode/20260722_delta_t/` (thu muc `results/` dang duoc git-ignore).

Moi truong:

- Commit: `a3ccf427d4d0cdea6c918d006c1a48249c30047b`
- Hexagon Clang: `19.0.04`
- Hexagon simulator API: `3.38.20250519`
- Target runner: `-mv75`, H2 booter, HVX 128B va HMX enabled
- Fixture count: 256 cho ca HQC-128, HQC-192 va HQC-256

`T` hien la compile-time macro (`HQC*_BENCH_ITERS`), nen moi diem duoc build thanh mot binary rieng. Day la cach runner/paper hien tai hoat dong. Neu can mot protocol chat hon nua, nen doi `T` thanh runtime argument de moi diem dung cung mot binary va loai kha nang code layout/codegen thay doi theo constant `T`.

## Raw simulator totals

Don vi trong bang la tong Hexagon Pcycles cua ca process simulator. Cac so nay chua duoc normalize va van chua fixed/setup/one-time initialization cost.

| HQC | Backend | T=1 | T=2 | T=3 | T=5 | Status |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 128 | Scalar | 247,685,868 | 491,851,047 | 736,012,680 | 1,224,339,492 | PASS |
| 128 | Fastest | 15,107,874 | 25,726,050 | 36,341,088 | 57,574,302 | PASS |
| 192 | Scalar | 343,964,565 | 683,366,148 | 1,022,764,593 | 1,701,564,729 | PASS |
| 192 | Fastest | 22,428,522 | 39,295,656 | 56,159,244 | 89,890,074 | PASS |
| 256 | Scalar | 708,376,005 | 1,410,931,968 | 2,113,484,655 | 3,518,593,092 | PASS |
| 256 | Fastest | 41,133,195 | 75,316,572 | 109,497,081 | 177,860,754 | PASS |

## Derived results khi thay doi Delta decode

Moi row deu dung cung baseline `T=1` cua chinh HQC/backend do.

| HQC | Upper T | Delta decodes | Scalar Delta Pcycles | Scalar Pcycles/decode | Fastest Delta Pcycles | Fastest Pcycles/decode | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 128 | 2 | 256 | 244,165,179 | 953,770.230 | 10,618,176 | 41,477.250 | 22.995x |
| 128 | 3 | 512 | 488,326,812 | 953,763.305 | 21,233,214 | 41,471.121 | 22.998x |
| 128 | 5 | 1,024 | 976,653,624 | 953,763.305 | 42,466,428 | 41,471.121 | 22.998x |
| 192 | 2 | 256 | 339,401,583 | 1,325,787.434 | 16,867,134 | 65,887.242 | 20.122x |
| 192 | 3 | 512 | 678,800,028 | 1,325,781.305 | 33,730,722 | 65,880.316 | 20.124x |
| 192 | 5 | 1,024 | 1,357,600,164 | 1,325,781.410 | 67,461,552 | 65,880.422 | 20.124x |
| 256 | 2 | 256 | 702,555,963 | 2,744,359.230 | 34,183,377 | 133,528.816 | 20.553x |
| 256 | 3 | 512 | 1,405,108,650 | 2,744,352.832 | 68,363,886 | 133,523.215 | 20.553x |
| 256 | 5 | 1,024 | 2,810,217,087 | 2,744,352.624 | 136,727,559 | 133,523.007 | 20.553x |

## Do hoi tu so voi T=5

Dung `T=5` lam reference cua sweep (khong coi no la ground truth tuyet doi):

| HQC | Backend | T=2 deviation | T=3 deviation | Nhan xet |
| --- | --- | ---: | ---: | --- |
| 128 | Scalar | +0.000726% | 0.000000% | T=3 va T=5 trung nhau |
| 128 | Fastest | +0.014779% | 0.000000% | Warm-up bias ro nhat tai T=2 |
| 192 | Scalar | +0.000454% | -0.000008% | T=3 da hoi tu |
| 192 | Fastest | +0.010353% | -0.000160% | Sai khac T=3/T=5 khoang 1.6 ppm |
| 256 | Scalar | +0.000241% | +0.000008% | T=3 da hoi tu |
| 256 | Fastest | +0.004351% | +0.000156% | Sai khac T=3/T=5 khoang 1.6 ppm |

Worst case cua `T=3` so voi `T=5` la fastest HQC-192: `0.000160%`. Day nho hon rat nhieu so voi bien dong do source, fixture corpus hoac backend data-dependent.

## Fixed overhead va ly do khong chia raw T=1

Neu dung slope `T=1 -> T=5` de uoc luong, fixed/setup/one-time cost nam trong raw `T=1` vao khoang 3.5-7.0 trieu Pcycles tuy backend/parameter set. Vi vay chia truc tiep raw `T=1` cho 256 se overestimate:

| HQC | Backend | Raw T=1 / decode | Paired estimate | Raw overestimate |
| --- | --- | ---: | ---: | ---: |
| 128 | Scalar | 967,522.922 | 953,763.305 | 1.443% |
| 128 | Fastest | 59,015.133 | 41,471.121 | 42.304% |
| 192 | Scalar | 1,343,611.582 | 1,325,781.410 | 1.345% |
| 192 | Fastest | 87,611.414 | 65,880.422 | 32.986% |
| 256 | Scalar | 2,767,093.770 | 2,744,352.624 | 0.829% |
| 256 | Fastest | 160,676.543 | 133,523.007 | 20.336% |

Fastest co bias raw lon hon vi ngoai simulator/program startup, code con lazy-initialize GF multiply table, RM expansion tables, transposed syndrome powers va RS support powers trong lan dung dau. Paired subtraction loai cac one-time cost nay khoi steady-state decode estimate.

## Sweet spot

### Khuyen nghi cho paper: T=1 va T=3

Ly do:

1. `Delta decodes=512` da du de estimator hoi tu den muc worst-case khoang 1.6 ppm so voi `T=5`.
2. `T=5` gap doi so decode bo sung nhung khong doi ket luan Pcycles/decode hay speedup.
3. `T=2` re hon, nhung van con bias duong nho tren ca 6 cau hinh; fastest HQC-128 lech nhieu nhat, khoang `0.0148%`.
4. Simulator la deterministic, nen sau khi da vuot warm-up transient, tang them `T` khong tao loi ich thong ke nhu benchmark tren hardware that.

Neu chi can smoke test/regression nhanh, `T=1`/`T=2` la chap nhan duoc. Neu dua so vao bang/bao cao, giu `T=1`/`T=3`.

## Canh bao: bang paper HQC-192/HQC-256 da superseded boi fixture fix

Paper ghi rang bang dung "current 256-fixture corpus", nhung hai dong fastest HQC-192/HQC-256 trong bang trung voi raw logs truoc fixture correction. Repo da ghi ro tai `docs/archive/README_result_whole.md`:

- Fixture cu chi flip mot byte trong moi RM block.
- RM decode sua duoc thay doi do, nen RS cua HQC-192/HQC-256 thay clean syndromes.
- Fastest la branchy/non-CT, nen clean-syndrome workload re hon workload RS-error hien tai.
- Scalar gan fixed-flow nen cac so scalar khong thay doi dang ke theo noi dung fixture.

So sanh cap `T=1`/`T=3`:

| HQC | Metric | Paper cu | Current corpus | Thay doi |
| --- | --- | ---: | ---: | ---: |
| 128 | Fastest Pcycles/decode | 41,471 | 41,471.121 | Khop |
| 192 | Fastest Pcycles/decode | 50,223 | 65,880.316 | +31.175% |
| 192 | Speedup | 26.40x | 20.124x | Giam |
| 256 | Fastest Pcycles/decode | 80,655 | 133,523.215 | +65.548% |
| 256 | Speedup | 34.03x | 20.553x | Giam |

Vi vay:

- Ket luan ve `Delta decode`/sweet spot `T=3` van dung.
- Neu cap nhat Table 3 theo checkout va fixture corpus hien tai, can thay hai dong fastest HQC-192/HQC-256 va speedup tuong ung.
- Khac biet lon nay den tu fixture workload, khong den tu viec chon `Delta decodes=512` hay `1024`.

## Ghi chu ve wall time

Wall time host khong duoc dung trong ket luan tren. Mot so runner session cua batch dau co overlap, nen cac file `*.wall_seconds` khong phai mot serial timing study sach. Dieu nay khong anh huong Hexagon Pcycles: day la simulated processor-cycle count, va raw totals lap lai khop o cac run trung cau hinh. Chi phi thuc te cua sweep van tang gan theo tong so decode mo phong; rieng `T=5` xu ly gap doi so decode bo sung cua `T=3`.
