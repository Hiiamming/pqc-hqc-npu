# Nghiên cứu sâu về HQC, decoder HQC, Reed–Muller, Reed–Solomon và triển khai hardware

## Tóm tắt điều hành

Trong ngữ cảnh mật mã hậu lượng tử, **HQC** được các nguồn chính thức của nhóm tác giả và NIST dùng theo nghĩa **Hamming Quasi-Cyclic**. Ở phiên bản đặc tả hiện hành, HQC là một KEM dựa trên bài toán **QCSD** và phần sửa sai của nó dùng **mã ghép** gồm **Reed–Solomon rút gọn** ở lớp ngoài và **Reed–Muller bậc một có lặp** ở lớp trong. Trang chính thức của HQC cũng công bố hiệu năng AVX2 cho ba bộ tham số HQC-1/HQC-3/HQC-5, với chi phí giải đóng gói hiện ở mức hàng trăm nghìn đến hơn một triệu chu kỳ CPU tùy mức an toàn. citeturn8view2turn19search4turn12view0

Nếu mục tiêu là **decoder HQC tối ưu trên phần cứng**, kết luận quan trọng nhất là: **tuyến chuẩn hiện nay** vẫn là **RM(1,7) bằng Fast Hadamard Transform** rồi đến **RS algebraic decoder**; đây cũng là hướng mà đặc tả HQC và các thiết kế HDL/RTL hiệu năng cao đang thực hiện. Ở hướng nghiên cứu mới hơn, bài báo năm 2026 về **GMD Reed–Solomon decoder cho HQC** là điểm nhấn đáng chú ý nhất: tác giả cho thấy với HQC-128 có thể giảm chiều dài RS từ 46 xuống 36, và toàn bộ khối giải mã HQC-128 đạt **giảm 20% độ trễ** và **15% diện tích** so với triển khai dùng hard-decision decoder. citeturn28view0turn28view1turn20view2

Với **Reed–Muller** nói chung, không có một decoder “thắng tuyệt đối” cho mọi tình huống. **Majority-logic** đơn giản và rất hợp phần cứng nhỏ; **FHT** là lựa chọn gần như mặc định cho **RM bậc một**; còn với mã ngắn và cần hiệu năng giải mã tốt, các họ **RPA**, **list-RPA**, **successive permutations**, và **FHT-FSCL** cho thấy chất lượng gần ML hoặc cải thiện rõ rệt so với các đường cơ sở SSC/FHT trước đây, đổi lại là mức song song hóa và bộ nhớ cao hơn. citeturn34view1turn42search13turn34view2turn34view3

Với **Reed–Solomon**, tuyến thực dụng nhất cho phần cứng vẫn là **Berlekamp–Massey hoặc inversionless BM** cộng với **Chien/Forney**, hoặc họ **extended Euclidean** khi ưu tiên tính đều đặn của datapath. **Gao** hấp dẫn cho hướng phần mềm/FFT vì tính trực tiếp trên thông điệp; **GMD** là điểm cân bằng tốt khi có soft information nhưng vẫn phải giữ phần cứng gọn; trong khi các decoder soft/list tổng quát hơn thường đắt hơn rõ rệt về độ trễ và bộ nhớ. citeturn38view0turn32search11turn20view2turn32search7

Nếu cần một lộ trình bắt đầu ngắn gọn, tôi đề xuất: đọc **đặc tả HQC 2025**, sau đó quay lại **paper gốc 2016 giới thiệu HQC**, rồi đọc **paper 2024 về mã sửa sai mới cho HQC**, tiếp theo là **survey 2020 về Reed–Muller**, và song song mở **official HQC code repository** để nắm layout mã nguồn, unit test và benchmark. citeturn19search4turn19search11turn20view1turn31search0turn12view0

## Phạm vi và cách hiểu HQC

Trong truy vấn này, “HQC” **không còn mơ hồ** ở mức kỹ thuật: nguồn chính thức của dự án viết rõ HQC là **Hamming Quasi-Cyclic**, một code-based KEM đã được NIST chọn trong mạch chuẩn hóa PQC. Vì vậy, trong toàn bộ báo cáo dưới đây tôi dùng HQC theo đúng nghĩa đó, không dùng nghĩa “không xác định”. citeturn8view2turn19search4

Cũng cần nói rõ rằng việc so sánh “độ chính xác” giữa các decoder cho RM/RS/HQC **không thể chỉ dùng một con số độc lập** với tham số mã, độ dài khối, kênh truyền và SNR. Do đó, trong các bảng dưới đây tôi dùng ba kiểu chỉ báo: **khả năng bounded-distance/hard-decision**, **kết quả BER/FER/DFR mà paper công bố**, hoặc **mô tả tương đối** như “gần ML”, “tốt hơn hard-decision”, “trễ thấp hơn nhưng tốn tài nguyên hơn”. Đây là cách trình bày phù hợp với chính cách các paper RM/HQC/RS gần đây báo cáo kết quả. citeturn20view1turn42search13turn20view2

Tài liệu **tiếng Việt** chuyên sâu đúng trọng tâm “decoder HQC + hardware” hiện còn rất hiếm. Trong lượt tìm kiếm này, tôi chỉ thấy hai nhóm bổ trợ ở mức nền tảng: một **bài giảng cơ sở mã hóa thông tin** có nhắc Reed–Muller và lịch sử ứng dụng, và một **tin mini-course của VIASM** có nêu code-based cryptography như một nhánh của PQC; chúng hữu ích để dựng nền nhưng **không đủ** cho thiết kế decoder hiệu năng cao. citeturn48search1turn48search2

### Năm tài liệu và code nên mở đầu tiên

| Mục | Vì sao nên đọc đầu tiên | Nguồn |
|---|---|---|
| **HQC specifications 2025** | Tài liệu định nghĩa chuẩn hiện hành của HQC, mô tả trực tiếp pipeline RS/RM, tham số, mã giả và yêu cầu tương thích. | citeturn19search4turn28view0turn28view1 |
| **Aguilar et al., 2016, Efficient Encryption from Random Quasi-Cyclic Codes** | Paper gốc công khai đầu tiên giới thiệu HQC trong khung QC code-based encryption. | citeturn19search11turn20view0 |
| **Aguilar-Melchor et al., 2024, Efficient error-correcting codes for the HQC post-quantum cryptosystem** | Paper quyết định cho việc chuyển từ BCH⊗repetition sang RS⊗duplicated-RM trong HQC hiện đại. | citeturn20view1 |
| **Abbe, Shpilka, Ye, 2020, Reed-Muller Codes: Theory and Algorithms** | Survey tốt nhất để nối lý thuyết RM với decoder hiện đại như recursive, list, projection-aggregation. | citeturn31search0turn31search4 |
| **[`pqc-hqc/hqc` GitLab](https://gitlab.com/pqc-hqc/hqc/)** | Official implementation: có cấu trúc ref/x86_64/avx256, unit test, KAT, benchmark, CMake. | citeturn12view0turn12view1 |

## HQC

### Paper gốc và overview quan trọng

Paper gốc quan trọng nhất cho HQC là **Aguilar, Blazy, Deneuville, Gaborit, Zémor (2016)**, nơi tác giả đưa ra khung “efficient encryption from random quasi-cyclic codes” và instantiation HQC trong Hamming metric. Đây là tài liệu nên đọc nếu muốn hiểu **động cơ thiết kế**, **mô hình an toàn**, và vì sao HQC chọn đi theo hướng code-based dùng quasi-cyclic structure thay vì che giấu cấu trúc bằng ma trận công khai kiểu cổ điển. citeturn19search11turn20view0

Ở mức “overview chuẩn hóa”, nên ưu tiên **website chính thức** và **HQC specifications 2025**. Website chính thức xác nhận HQC là Hamming Quasi-Cyclic, công bố kích thước khóa/bản mã và benchmark AVX2 trên Intel Core i7-11850H; đặc tả 2025 là nguồn trực tiếp cho pipeline giải mã và tham số RS/RM hiện hành. citeturn8view2turn19search4

Paper **Aguilar-Melchor et al. (2024)** là paper quan trọng nhất nếu mục tiêu là **decoder HQC** chứ không chỉ “HQC như một KEM”. Paper này giải thích vì sao HQC chuyển từ **BCH ⊗ repetition** sang **RS ⊗ duplicated RM**, chứng minh/ước lượng DFR và cho thấy thay đổi đó giúp **giảm kích thước vòng đa thức**, nhờ đó tăng hiệu năng toàn hệ. citeturn20view1

Về hướng tối ưu mới, hai paper năm 2025–2026 đáng chú ý là **OptHQC** trên CPU và **GMD RS decoder for HQC** trên hardware. OptHQC phân tích từng khối tính toán trong HQC và báo **tăng tốc trung bình 55%** trên CPU so với bản tham chiếu; paper GMD 2026 thì đi thẳng vào decryption path và cho thấy mềm hóa lớp RS là cách có ý nghĩa nhất để rút ngắn codeword mà vẫn giữ yêu cầu DFR. citeturn13search2turn14view3turn20view2

### Decoder của HQC và các biến thể tối ưu

Về mặt cấu trúc, decoder HQC hiện hành làm việc theo chuỗi: **giải mã mã nội Reed–Muller có lặp** trước, sau đó dùng kết quả để **giải mã mã ngoài Reed–Solomon rút gọn**. Đặc tả HQC nêu rõ HQC dùng **RM(1,7) = [128,8,64]**, rồi nhân bản lên **[384,8,192]** cho HQC-1 hoặc **[640,8,320]** cho HQC-3/HQC-5; còn lớp RS tương ứng là **[46,16,31]**, **[56,24,33]** và **[90,32,59]**. citeturn28view0turn25view4

Ở lớp RM, decoder chuẩn là **Fast Hadamard Transform** cho mã bậc một. Đây là lựa chọn rất hợp lý vì RM nội của HQC là **first-order code cố định**, nên FHT cho độ trễ thấp, đường dữ liệu đều, và rất dễ pipeline hóa trên FPGA/ASIC. Nói ngắn gọn: với HQC, **RM decoder tối ưu không phải RPA**, mà là **FHT chuyên dụng cho RM(1,7)**. citeturn28view0turn34view1

Ở lớp RS, decoder chuẩn vẫn là decoder đại số kiểu **syndrome → error locator/evaluator → Chien/Forney/correction**. Trong thiết kế RTL hiện đại cho HQC, lớp này thường được tối ưu bằng những biến thể như **enhanced parallel inversionless Berlekamp–Massey** và **enhanced Chien Search/Error Evaluation**, đặc biệt hiệu quả vì các mã RS trong HQC đều là **mã ngắn, low-rate, cố định**. citeturn28view1turn26view4

Điểm mới nổi bật nhất hiện nay là thay hard-decision RS bằng **GMD decoder**. Bài báo của **Cai và Zhang (2026)** lập luận rằng trong bối cảnh HQC, GMD tận dụng soft information từ tầng RM tốt hơn so với cách chỉ dùng erasure-only trước đó; kết quả là với HQC-128 có thể hạ RS từ 46 xuống 36 ký hiệu, và kiến trúc duro hóa của họ còn cho **giảm 20% latency** và **15% area** ở toàn khối decryption HQC-128 so với thiết kế hard-decision. Đây là hướng nghiên cứu tôi đánh giá “đáng đầu tư nhất” nếu bạn muốn **vượt qua decoder chuẩn hiện tại**. citeturn20view2

### So sánh các hướng decoder HQC

| Hướng | Ý tưởng chính | Độ chính xác / DFR | Độ trễ | Bộ nhớ | Nhận định |
|---|---|---|---|---|---|
| **Chuẩn HQC** | FHT cho duplicated RM(1,7) + algebraic RS ngắn | Chuẩn hiện hành, tương thích đặc tả; đáp ứng mục tiêu DFR của HQC theo bộ tham số chuẩn | Thấp đến trung bình; rất thuận pipeline | Thấp đến trung bình | Phù hợp nhất nếu mục tiêu là **tương thích chuẩn/NIST**. citeturn28view0turn28view1turn20view1 |
| **Hard-decision hardware-optimized** | FHT RM chuyên dụng + ePIBMA/eCSEE cho RS | Không đổi mô hình mã; tối ưu vi kiến trúc | Rất thấp ở block decoder chuyên dụng | Nhỏ gọn nhờ mã ngắn cố định | Đây là nhánh đang cho **best practical RTL** cho HQC chuẩn. citeturn26view4turn27view0 |
| **Soft-RS kiểu GMD** | Trích soft information từ RM, chạy nhiều test vector error/erasure | Tốt hơn hard-decision trong ngữ cảnh HQC; cho phép rút ngắn codeword | Chi phí từng test vector tăng, nhưng paper 2026 cho thấy **latency toàn hệ lại giảm** nhờ code ngắn hơn | Cao hơn hard-decision nếu giữ cùng code; nhưng có thể bù nhờ codeword ngắn hơn | Lựa chọn tốt nhất nếu chấp nhận **không còn bám chặt decoder chuẩn** mà ưu tiên area/latency tối ưu hơn. citeturn20view2 |

### Code nguồn và repository quan trọng cho HQC

Bản **official implementation** hiện có trên GitLab của nhóm HQC. README cho biết repo có **reference implementation**, **x86_64 optimized**, **AVX-256/AVX2 backend**, **unit tests**, **API tests**, **KAT tests** và **benchmarks**; build bằng **CMake**, yêu cầu **CMake ≥ 3.21**, **C compiler hỗ trợ C11**, và bản quyền là **public domain**. citeturn12view0turn12view1

Ngoài official repo, có hai nhánh tối ưu thực nghiệm rất đáng chú ý. Thứ nhất là **[`myhoon/FAFFT_HQC`](https://github.com/myhoon/FAFFT_HQC)** cho Cortex-M4, public domain, tích hợp vào **PQM4** và nhắm đến bo **NUCLEO-L4R5ZI**. Thứ hai là **[`ChunTaoPengim/HQC_with_addFFT_tches2026`](https://github.com/ChunTaoPengim/HQC_with_addFFT_tches2026)**, dùng **CC0-1.0**, có các target cho **x86_64**, **GFNI**, **Apple M1**, **Arm Cortex-A72**, có benchmark script `new_bench.sh`, và nói rõ thư mục `code_x86` chứa **RS encoder** và **RM decoder** tối ưu cho x86. citeturn14view5turn14view4turn15view2

Paper Cortex-M4 năm 2025 còn trỏ tới repo **`kindongsy/ICTE-HQC_Cortex-M4`**, nhưng ở thời điểm truy xuất hiện tại trang repo GitHub hiển thị **“This repository is empty.”** Nếu bạn cần tái lập đúng kết quả paper này thì nên coi repo đó là **nguồn chưa sẵn sàng**, còn paper thì vẫn đủ giá trị về benchmark và ý tưởng tối ưu. citeturn17search2turn18view0

| Repository / code source | Ngôn ngữ | Trạng thái tôi quan sát được | License | Build / chạy | Benchmark / ghi chú |
|---|---|---:|---|---|---|
| [`pqc-hqc/hqc` GitLab](https://gitlab.com/pqc-hqc/hqc/) | C | Official implementation | Public domain | CMake; `HQC_ARCH=ref` hoặc `x86_64`, backend `avx256` | Có unit/API/KAT/bench; cấu trúc rất sạch để bắt đầu audit decoder. citeturn12view0turn12view1 |
| [`myhoon/FAFFT_HQC`](https://github.com/myhoon/FAFFT_HQC) | C | Repo có README và hướng dẫn tích hợp PQM4 | Public domain | Sao chép vào `pqm4/crypto_kem`, sau đó dùng workflow PQM4 | Hướng đến Cortex-M4; hữu ích cho thử nghiệm FAFFT. citeturn14view5 |
| [`ChunTaoPengim/HQC_with_addFFT_tches2026`](https://github.com/ChunTaoPengim/HQC_with_addFFT_tches2026) | C | Repo nghiên cứu tối ưu đa nền tảng | CC0-1.0 | `make TARGET=... PROJ=hqc-1/3/5`; có script `new_bench.sh` | Có tối ưu cho x86/GFNI/M1/A72; nói rõ khối `code_x86` có RS encoder + RM decoder tối ưu. citeturn14view4turn15view2 |
| `kindongsy/ICTE-HQC_Cortex-M4` | chưa xác thực được vì repo trống | Repo paper trỏ tới nhưng hiện trống | không xác định | không thể build từ repo hiện tại | Dùng paper làm nguồn chính, không dùng repo làm nguồn tái lập. citeturn17search2turn18view0 |

### Phần cứng cho HQC

Paper **Deshpande et al.** là mốc đầu cho triển khai phần cứng HQC theo hướng **constant-time**. Bài báo tập trung vào toàn bộ KEM HQC trên **Artix-7**, báo cáo cả thiết kế một-clock lẫn dual-clock, và so sánh với các công trình trước. Ở bảng so sánh mức an toàn 128-bit, cấu hình **HighSpeed-DC** của họ dùng khoảng **23,585 LUT**, **10,436 FF**, **48.5 BRAM**, chạy ở khoảng **194 MHz**, với thời gian cỡ **0.18 ms encapsulation**, **0.26 ms decapsulation** và **0.11 ms key generation**; các cấu hình “Balanced” nhỏ hơn nhưng chậm hơn đôi chút. citeturn13search16turn28view3

Nếu quan tâm **decoder HQC** hơn là toàn bộ KEM, dữ liệu có giá trị nhất tôi tìm thấy nằm trong luận án và bài IEEE TC 2025 của **Antognazza–Barenghi–Pelosi**. Họ trình bày các khối **RM/RS encoder–decoder** riêng, rồi ghép thành accelerator hỗ trợ cả HQC-128/192/256. Riêng **RM/RS decoder** trên Artix-7, họ báo cáo cho **hqc128**: **5,896 LUT**, **3,364 FF**, **2.5 BRAM**, **212 MHz**, **1,293 chu kỳ**, tương đương **6.10 µs**; còn **hqc256** là **10,090 LUT**, **4,472 FF**, **2.5 BRAM**, **225 MHz**, **2,705 chu kỳ**, khoảng **12.02 µs**. So với decoder của Deshpande, thiết kế này dùng nhiều LUT hơn nhưng **nhanh hơn 3.36× đến 3.71×** và hiệu quả Area-Time tốt hơn gần **2×** theo chính tác giả. citeturn23view2turn27view0turn27view2

Một chi tiết rất quan trọng cho thiết kế hệ thống là lịch điều độ bộ nhớ. Luận án của Antognazza cho biết một lịch vận hành thực tế của HQC có thể được tổ chức quanh **năm true dual-port RAM**, vì ở mức song song hóa đó có thể xếp chồng một phép nhân đa thức với việc lấy mẫu đa thức Hamming-weight cố định mà vẫn cân bằng băng thông đọc của multiplier. Đây là thông tin rất hữu dụng nếu bạn đang làm floorplanning hoặc phân hoạch BRAM. citeturn27view3

Cuối cùng, ở hướng “hardware-aware algorithm design”, paper **GMD RS for HQC (2026)** cần được xem là nhánh nối giữa thuật toán và RTL: nó không chỉ nói rằng GMD tốt hơn hard-decision, mà còn trình bày **efficient hardware architectures** được tối ưu cho các mã RS ngắn, low-rate của HQC. Nếu mục tiêu của bạn là “decoder HQC mới trên mạch” chứ không chỉ tái tạo chuẩn, đây là paper phải đọc. citeturn20view2

## Reed–Muller

### Paper gốc và survey/overview cho Reed–Muller

Hai paper khai sinh Reed–Muller là **Muller (1954)** về biểu diễn Boolean/switching và **Reed (1954)** về lớp mã sửa sai nhiều lỗi và thủ tục giải mã. Một lợi thế của repo MATLAB `reed-muller-codes-matlab` là README của nó dẫn rất gọn cả hai paper gốc này cùng các paper Dumer 2006, Ye–Abbe 2020, và survey Abbe–Shpilka–Ye 2020. citeturn43view0turn49view2

Survey/overview hiện đại tốt nhất là **Abbe, Shpilka, Ye (2020), “Reed-Muller Codes: Theory and Algorithms.”** Tài liệu này bao phủ từ tính chất trọng số, quan hệ với polarization/thresholds của Boolean functions, cho tới các decoder có bảo đảm và decoder thực dụng tốt nhất ở miền mã ngắn. Nếu bạn chỉ chọn một survey cho RM, đây là lựa chọn nên ưu tiên. citeturn31search0turn31search4

### Các decoder hiệu năng cao cho Reed–Muller

**Majority-logic** là đường cơ sở cổ điển nhất. Nó giải mã được RM(r,m) theo thủ tục kiểu Reed, rất hợp cho phần cứng cỡ nhỏ hoặc môi trường PUF/embedded vì datapath cực kỳ đơn giản. Đổi lại, nó là hard-decision và thường kém các hướng soft/near-ML hiện đại về hiệu năng lỗi ở mã ngắn. citeturn43view0turn43view2

Với **RM(1,m)**, decoder chuẩn hiệu năng cao là **Fast Hadamard Transform**. Tài liệu benchmark của ISAE-SUPAERO nêu khá rõ: với first-order RM, có thể đạt **maximum-likelihood decoding** bằng “Green Machine” / FHT với độ phức tạp cỡ **O(m2^m)** trên soft values, tương đương **O(n log n)** theo chiều dài khối. Đây chính là lý do FHT gần như luôn xuất hiện ở RM bậc một trong HQC và trong các thiết kế FPGA/ASIC chuyên dụng. citeturn34view1turn28view0

Khi đi lên RM bậc cao hơn, các họ **recursive decoder của Dumer** và đặc biệt là **Recursive Projection–Aggregation** trở thành lựa chọn thực tế hơn. Paper **Ye–Abbe (2020)** cho thấy RPA hoạt động rất tốt ở miền mã ngắn (độ dài đến 1024), trên cả BSC và AWGN, vượt decoder polar SCL+CRC ở nhiều miền tốc độ, và có đặc tính đặc biệt hấp dẫn với phần cứng là **song song hóa tự nhiên** vì mỗi projection tạo ra các bài toán con độc lập hơn. citeturn42search13

Ở nhánh mới hơn, hai paper của nhóm Gross/Mondelli/Hashemi/Doan về **Successive Codeword Permutations** và **FHT-FSCL** cho thấy vẫn còn không gian lớn để giảm chi phí hệ thống mà không làm sụt mạnh hiệu năng lỗi. Cụ thể, với mã RM độ dài **512**, **46 bit thông tin**, kiến trúc **20 decoder FHT-FSCL song song với L=4** giảm **72% độ phức tạp tính toán**, **22% độ trễ**, **84% bộ nhớ** so với baseline SSC-FHT trong khi giữ hiệu năng gần như tương đương tại FER mục tiêu **10^-4**. Với mã dài **256**, **163 bit thông tin**, kỹ thuật successive permutations giảm khoảng **6% độ phức tạp** và **22% độ trễ** so với semi-parallel SSC-FHT dùng 96 permutations ở FER **10^-3**. citeturn34view2turn34view3

### So sánh decoder Reed–Muller

| Decoder RM | Độ phức tạp | Hiệu năng lỗi | Độ trễ | Bộ nhớ | Khi nào nên dùng |
|---|---|---|---|---|---|
| **Majority-logic** | Thấp, cấu trúc logic rất đơn giản | Kém hơn soft/near-ML ở mã ngắn | Thấp | Rất thấp | FPGA/ASIC nhỏ, PUF, logic kiểm thử. citeturn43view0turn43view2 |
| **FHT cho RM(1,m)** | **O(m2^m)** trên soft values | Rất mạnh; ML cho first-order RM | Rất thấp khi pipeline tốt | Thấp đến trung bình | Tốt nhất cho RM bậc một, đặc biệt trong HQC. citeturn34view1turn28view0 |
| **Recursive Dumer / list** | Trung bình đến cao hơn FHT | Tốt hơn majority-logic, nhất là khi list | Trung bình | Trung bình | Tổng quát hơn FHT, dùng khi mã không phải bậc một. citeturn43view0turn49view2 |
| **RPA / list-RPA** | Cao hơn majority/FHT, nhưng rất song song | Gần ML ở nhiều miền mã ngắn; mạnh trên BSC/AWGN | Trung bình đến cao nếu tuần tự; thấp nếu song song | Trung bình đến cao | Lựa chọn mạnh nhất khi cần quality-of-decoding cho RM ngắn. citeturn42search13 |
| **SP / FHT-FSCL** | List + permutation nên đắt hơn FHT đơn thuần, nhưng paper cho thấy giảm mạnh so với baseline SSC-FHT | Giữ hiệu năng gần như không đổi ở FER mục tiêu | Giảm **22%** trong hai cấu hình công bố | Giảm mạnh ở FHT-FSCL | Rất hấp dẫn cho decoder hiệu năng cao có ngân sách phần cứng vừa/khá. citeturn34view2turn34view3 |

### Repository và code nguồn cho Reed–Muller

Repo dễ dùng nhất cho nghiên cứu thuật toán là **[`benhuryuval/reed-muller-codes-matlab`](https://github.com/benhuryuval/reed-muller-codes-matlab)**. Nó có cả **Reed decoder (majority-logic)**, **FHT unique/list**, **Dumer recursive unique/list**, và **RPA unique/list**, chạy trên MATLAB, được tác giả nói đã test trên **MATLAB R2020a**, và script chính là `Tests.m`. Tôi không thấy license hiển thị rõ trong phiên truy xuất hiện tại, nên nên coi đây là **license chưa xác minh** trước khi tái sử dụng trong dự án công nghiệp. citeturn45view1turn49view2

Nếu mục tiêu là RM hiện đại với trọng tâm **RPA/SRPA**, repo đáng xem nhất là **[`kit-cel/sdss-rpa`](https://github.com/kit-cel/sdss-rpa)**. Đây là package Python có thể cài trực tiếp bằng `pip install .`, cấu hình qua `SimulationConfig`, và dùng **MIT license**. Về giá trị học thuật, repo này đặc biệt tốt vì nó phản chiếu trực tiếp paper 2023 về **semi-deterministic subspace selection** cho sparse RPA. citeturn43view1

Nếu bạn muốn một code nhỏ, dễ đọc, phù hợp dạy học hoặc sanity-check thuật toán majority-logic, **[`sraaphorst/reed-muller-python`](https://github.com/sraaphorst/reed-muller-python)** là lựa chọn tốt. README nêu trạng thái “complete”, hỗ trợ **Python 2 và 3**, và raw license cho thấy nó dùng **Apache License 2.0**. citeturn43view3turn44view0

Ở hướng phần cứng, **[`piliguori/Reed-Muller-Decoder`](https://github.com/piliguori/Reed-Muller-Decoder)** là một repo VHDL gọn, GPL-3.0, mô tả decoder dựa trên **majority voter** với parallel counter/adder/comparator. Repo này không phải state-of-the-art về performance, nhưng lại hữu ích để dựng nhanh một prototype majority-logic decoder trên FPGA. citeturn43view2

| Repository | Ngôn ngữ | Decoder có sẵn | License | Build / chạy | Ghi chú |
|---|---|---|---|---|---|
| [`benhuryuval/reed-muller-codes-matlab`](https://github.com/benhuryuval/reed-muller-codes-matlab) | MATLAB | Reed / FHT / Dumer / list / RPA / list-RPA | **Chưa thấy rõ trong phiên truy xuất** | Cần MATLAB; chạy qua `Tests.m` | Repo đầy đủ nhất để so sánh nhiều họ decoder RM trong cùng một baseline. citeturn45view1turn49view2 |
| [`kit-cel/sdss-rpa`](https://github.com/kit-cel/sdss-rpa) | Python | RPA, SRPA, SDSS-SRPA | MIT | `pip install .` hoặc `pip install -e .` | Tốt cho nghiên cứu decoder projection/aggregation hiện đại. citeturn43view1 |
| [`sraaphorst/reed-muller-python`](https://github.com/sraaphorst/reed-muller-python) | Python | majority-logic | Apache-2.0 | Package Python với `setup.py` | Nhỏ, dễ đọc, phù hợp test nhanh. citeturn43view3turn44view0 |
| [`piliguori/Reed-Muller-Decoder`](https://github.com/piliguori/Reed-Muller-Decoder) | VHDL | majority-voter hardware decoder | GPL-3.0 | RTL/VHDL | Hữu ích cho prototype RM decoder phần cứng. citeturn43view2 |

### Hardware cho Reed–Muller

Paper phần cứng RM nổi bật nhất trong tập nguồn truy xuất được là **Hashemipour-Nazari, Goossens, Balatsoukas-Stimming (ICASSP 2021)** về **simplified iterative projection-aggregation**. Tác giả chuyển RPA đệ quy sang cấu trúc lặp thuận phần cứng hơn, chấp nhận suy giảm hiệu năng nhỏ khoảng **0.005** về crossover probability trên RM(7,3), trong khi giảm **đến 40%** số phép tính trung bình. RTL trên **Virtex-7 xc7vx1140T** cho **RM(6,3)** đạt **80 MHz**, **171 Mb/s throughput tối thiểu**, và **284 Mb/s throughput trung bình** tại FER = 10^-3, nhưng phải trả giá bằng footprint rất lớn: khoảng **602,111 LUT** và **65,699 FF**. citeturn34view0turn35view1

Bài học rất rõ ở đây là: **RPA cực mạnh ở mức thuật toán nhưng có thể trở thành “LUT-eater” khi fully parallel**. Điều này giải thích vì sao trong HQC – nơi RM chỉ là **RM(1,7)** – các nhóm thiết kế phần cứng nghiêng mạnh sang **FHT chuyên dụng** thay vì mang nguyên triết lý RPA vào trong đường giải chuẩn. citeturn35view1turn28view0turn26view1

## Reed–Solomon

### Paper gốc và overview quan trọng

Paper gốc của Reed–Solomon là **Reed và Solomon (1960), “Polynomial Codes Over Certain Finite Fields.”** Đây là cột mốc nền tảng nhất cho toàn bộ họ mã RS và vẫn là điểm bắt đầu đúng nhất nếu bạn muốn đi từ định nghĩa mã sang thuật toán giải mã. citeturn29search9

Trong nhánh decoder đại số hiện đại, **Massey (1969), “Shift-register synthesis and BCH decoding”** là nguồn cổ điển gắn trực tiếp với **Berlekamp–Massey**, còn **Gao (2002), “A New Algorithm for Decoding Reed-Solomon Codes”** là paper quan trọng cho hướng giải mã dựa trên FFT/trực tiếp theo message polynomial. Ở phía hardware, bài **Sarwate–Shanbhag (2001)** là paper rất đáng đọc vì nó đi thẳng vào bottleneck của BM và tái cấu trúc thành kiến trúc systolic tốc độ cao. citeturn38view0turn32search11

Về overview/tự học, **McEliece (1988)** là một tutorial kiểu JPL rất tốt để hiểu “The Decoding of Reed-Solomon Codes”, còn các luận văn/tổng quan như **Czynszak (2011)** hay thesis RS 2023–2025 hữu ích hơn cho việc so sánh nhiều decoder thực hành và nhìn bài toán từ góc kỹ sư thiết kế. citeturn32search7turn32search0turn32search18turn38view2

### Các decoder hiệu năng cao cho Reed–Solomon

Trong thực tế kỹ thuật, đường cơ sở mạnh nhất vẫn là **Berlekamp–Massey + Chien Search + Forney** hoặc biến thể inversionless. Paper của Sarwate–Shanbhag phân tích rất rõ rằng bottleneck tốc độ nằm ở **tính discrepancy** và **cập nhật error-locator polynomial**, rồi đưa ra một kiến trúc mới biến khối đó thành **fully systolic architecture** với đường tới hạn chỉ qua **một multiplier và một adder**; đây là lý do BM vẫn quá sống khỏe ở phần cứng, dù extended Euclidean cũng rất mạnh. citeturn38view0

Họ **extended Euclidean** vẫn đáng lưu ý khi ưu tiên datapath đều và dễ pipeline. Ngay trong phần giới thiệu paper 2001, tác giả nêu rằng phần lớn triển khai tốc độ cao khi đó dùng kiến trúc dựa trên **eE**, với lợi thế chính là **regularity** và đường tới hạn đủ ngắn cho nhiều ứng dụng thực tế. Vì thế, nếu bạn cần một decoder RS đa dụng, dễ scale và dễ formal verification, eE vẫn là phương án rất nghiêm túc. citeturn38view0

**Gao decoder** đại diện cho trường phái khác: thay vì đi qua error-locator/error-magnitude một cách tường minh, nó trực tiếp tính toán lại message symbols, dùng FFT, hỗ trợ cả errors-and-erasures trong bán kính giải mã, và theo abstract gốc còn có khả năng phát hiện mọi lỗi vượt ngoài bán kính đó. Đây là lựa chọn nên xem xét nếu mục tiêu của bạn nghiêng sang **software hiệu năng cao** hoặc môi trường đã có FFT/NTT engine tốt. citeturn32search11

Nếu có **soft information**, hướng nên ưu tiên trước tiên không phải decoder list quá phức tạp, mà là **GMD**. Bài báo GMD cho HQC năm 2026 cho thấy trong ngữ cảnh mã RS ngắn, low-rate và có reliability đầu vào hữu ích, GMD là mức cân bằng rất đẹp giữa **coding gain**, **phần cứng thân thiện**, và **lợi ích thực sự ở latency/area toàn hệ**. Đây là thông điệp rất có giá trị vượt ra ngoài riêng HQC. citeturn20view2

### So sánh decoder Reed–Solomon

| Decoder RS | Độ chính xác / bán kính giải | Độ trễ | Bộ nhớ | Phù hợp |
|---|---|---|---|---|
| **BM + Chien + Forney** | Bounded-distance chuẩn; vẫn là baseline công nghiệp mạnh nhất | Thấp đến trung bình; dễ pipeline | Thấp đến trung bình | FPGA/ASIC chuẩn, decoder RS nhúng trong modem/storage/PQC. citeturn38view0turn26view4 |
| **Extended Euclidean** | Cùng lớp bounded-distance, mạnh về tính đều đặn kiến trúc | Thấp đến trung bình | Trung bình | Khi muốn datapath đều, dễ layout và dễ formalize. citeturn38view0 |
| **Gao** | Trong bán kính chuẩn; paper gốc nhấn mạnh tính trực tiếp trên thông điệp và hỗ trợ errors/erasures | Thường phù hợp hơn với phần mềm/FFT stack tốt | Trung bình | CPU/GPU/accelerator đã có FFT/NTT. citeturn32search11 |
| **GMD** | Tận dụng soft info tốt hơn hard-decision; trong HQC cho phép rút ngắn code | Cao hơn từng lượt decode, nhưng có thể giảm latency toàn hệ | Trung bình đến cao | Rất đáng cân nhắc nếu có reliabilities từ tầng trước. citeturn20view2 |

### Repository và code nguồn cho Reed–Solomon

Với mục tiêu “dùng ngay”, repo tốt nhất là **[`tomerfiliba-org/reedsolomon`](https://github.com/tomerfiliba-org/reedsolomon)**. Đây là một **Pythonic universal errors-and-erasures RS codec**, có cả bản **pure Python** và tùy chọn **Cython/C extension**, README rất đầy đủ, có notebook speed tests, và license cho phép chọn **Unlicense** hoặc **MIT-0**. Tôi đánh giá repo này là điểm bắt đầu tốt nhất nếu bạn cần kiểm chứng ý tưởng decoder RS trước khi đẩy xuống C/RTL. citeturn47view0

Nếu cần thư viện “nặng đô” hơn ở phía C++, repo **[`ArashPartow/schifra`](https://github.com/ArashPartow/schifra)** và website chính thức **Schifra** là lựa chọn rất mạnh. Mô tả chính thức nói rõ Schifra là một thư viện RS **rất tối ưu**, **cấu hình cao**, hỗ trợ **standard/shortened/punctured RS**, **product codes**, **interleaving**, có **example** và file **speed evaluation** ngay trong repo; website còn nêu Schifra có cả hướng **software lẫn IP-core/VHDL applications**. Tuy nhiên, trong phiên truy xuất này tôi **không bóc tách được license cụ thể** của repo GitHub bằng cách machine-readable, nên nếu dùng cho thương mại bạn nên kiểm tra trực tiếp trên site/license page của Schifra. citeturn47view1turn47view2

| Repository / code source | Ngôn ngữ | License | Build / chạy | Benchmark / ghi chú |
|---|---|---|---|---|
| [`tomerfiliba-org/reedsolomon`](https://github.com/tomerfiliba-org/reedsolomon) | Python + Cython/C | Unlicense **hoặc** MIT-0 | `pip`/PyPI; optional speed-optimized extension | README rất đầy đủ; có notebook speed tests; tốt cho prototype và validation. citeturn47view0 |
| [`ArashPartow/schifra`](https://github.com/ArashPartow/schifra) / [schifra.com](https://www.schifra.com/) | C++; site chính thức nêu cả software/IP core based applications | **Cần kiểm tra lại trực tiếp trên site** | Ví dụ C++ và file speed evaluation có sẵn trong repo | Rất phù hợp nếu cần thư viện RS tối ưu và cấu hình rộng; site chính thức còn nêu cả VHDL/IP-core orientation. citeturn47view1turn47view2 |

### Hardware cho Reed–Solomon

Paper **Sarwate–Shanbhag (2001)** vẫn là mốc kinh điển cho phần cứng RS. Tác giả chỉ ra rằng kiến trúc BM mới của họ tiết kiệm khoảng **25% số multiplier** so với kiến trúc extended Euclidean phổ biến, และ với block-interleaved RS còn có thể đưa đường tới hạn xuống chỉ còn cỡ **một XOR và một multiplexer**, cho tốc độ tăng **đến một bậc độ lớn** so với kiến trúc truyền thống. Đây không chỉ là một paper “lịch sử”, mà vẫn là nền tảng tư duy cho thiết kế datapath RS hiện đại. citeturn38view0

Ở mặt tổng hợp hiện đại, thesis **O’Neill (2025)** so sánh bốn kiến trúc RS-FEC decoder từ HLS đến logic synthesis. Kết quả của tác giả cho thấy decoder dùng **basic Berlekamp–Massey** đạt tần số lớn nhất, khoảng **727.8 MHz**, với diện tích vào khoảng **309,935 gate equivalents**; trong khi các biến thể **riBM** giảm diện tích nhưng không thắng ở tần số tối đa sau logic synthesis thực tế. Tôi xem đây là dữ kiện rất hữu ích cho trade-off “BM thô nhưng nhanh” so với “inversionless gọn hơn”. citeturn38view2turn40view2

Ngoài ra còn có một hướng ASIC đáng chú ý từ paper **Ji et al. (2016)** về recursive enhanced parallel inversionless BM. Trong kết quả tóm tắt mà tôi truy xuất được, thiết kế này đạt khoảng **575 MHz** và **4.6 Gb/s** trên **SMIC 0.18 µm CMOS**, với hiệu quả tốt hơn khoảng **28.15%** so với công trình trước đó. Vì số liệu này tôi đọc từ snippet của nguồn tổng hợp chứ không từ full paper open-access trong phiên này, nên nên coi nó là **mốc tham khảo hợp lý**, không phải số liệu duy nhất để ra quyết định mua/bán tài nguyên silicon. citeturn37search8turn37search5

## Bảng so sánh tóm tắt

### Bảng paper, code và hardware theo từng mã

| Mã | Paper / overview nên ưu tiên | Code nên mở đầu | Hardware đại diện | Kết luận ngắn |
|---|---|---|---|---|
| **HQC** | `HQC specifications 2025`; `Efficient Encryption from Random Quasi-Cyclic Codes` (2016); `Efficient error-correcting codes for the HQC post-quantum cryptosystem` (2024); `GMD RS decoder for HQC` (2026) | Official GitLab `pqc-hqc/hqc`; `FAFFT_HQC`; `HQC_with_addFFT_tches2026` | Deshpande 2022 cho full-KEM constant-time; Antognazza 2025 cho RM/RS decoder accelerator rất mạnh | Nếu cần **chuẩn + nhanh**, bám pipeline FHT-RM + algebraic RS; nếu được phép nghiên cứu sâu hơn, **GMD-RS** là nhánh đáng theo nhất. citeturn19search4turn19search11turn20view1turn20view2turn12view0turn14view5turn14view4turn28view3turn27view0 |
| **Reed–Muller** | Muller 1954; Reed 1954; Dumer 2006; Ye–Abbe 2020; Abbe–Shpilka–Ye 2020 | `reed-muller-codes-matlab`; `sdss-rpa`; `reed-muller-python`; `Reed-Muller-Decoder` | ICASSP 2021 IPA/RPA on Virtex-7 | Với **RM(1,m)**, FHT là vua; với RM bậc cao/mã ngắn, RPA và biến thể permutation/list mới là nhánh hiệu năng mạnh hơn. citeturn43view0turn49view2turn31search0turn43view1turn43view3turn43view2turn34view0turn35view1 |
| **Reed–Solomon** | Reed–Solomon 1960; Massey 1969; Sarwate–Shanbhag 2001; Gao 2002; McEliece 1988 tutorial | `reedsolomon`; `schifra` | Sarwate–Shanbhag 2001 architecture; O’Neill 2025 HLS/logic synthesis; Ji 2016 riBM ASIC | BM/eE vẫn là baseline phần cứng; Gao hợp software/FFT; GMD hợp khi có soft info và muốn thêm coding gain mà chưa chạm tới decoder list phức tạp. citeturn29search9turn38view0turn32search11turn32search7turn47view0turn47view1turn40view2turn37search8 |

### Bảng so sánh tài nguyên và tốc độ phần cứng tiêu biểu

| Thiết kế | Loại | Nền tảng | Tài nguyên chính | Tốc độ | Độ trễ / throughput | Power |
|---|---|---|---|---|---|---|
| **Antognazza 2025, HQC RM/RS decoder, hqc128** | Decoder HQC chuyên dụng | Artix-7 | 5,896 LUT, 3,364 FF, 2.5 BRAM | 212 MHz | 1,293 cc, ~6.10 µs | Không nêu trong đoạn truy xuất. citeturn27view0 |
| **Antognazza 2025, HQC RM/RS decoder, hqc256** | Decoder HQC chuyên dụng | Artix-7 | 10,090 LUT, 4,472 FF, 2.5 BRAM | 225 MHz | 2,705 cc, ~12.02 µs | Không nêu trong đoạn truy xuất. citeturn27view0 |
| **Deshpande 2022, HQC HighSpeed-DC, mức 128-bit** | Full HQC KEM | Artix-7 `xc7a200t` | ~23,585 LUT, ~10,436 FF, ~48.5 BRAM | ~194 MHz | ~0.18 ms encap, ~0.26 ms decap, ~0.11 ms keygen | Không nêu trong bảng chụp lại. citeturn28view3 |
| **Hashemipour-Nazari 2021, RM(6,3) IPA** | Standalone RM decoder | Virtex-7 `xc7vx1140T` | 602,111 LUT, 65,699 FF | 80 MHz | 171 Mb/s min, 284 Mb/s avg @ FER 10^-3 | Không nêu. citeturn35view1 |
| **O’Neill 2025, basic BM RS decoder** | RS-FEC decoder logic synthesis | ASIC library | ~309,935 GE | 727.8 MHz | Tác giả nhấn mạnh đây là tần số cao nhất trong 4 kiến trúc so sánh | Có đánh giá power tương đối, nhưng không có số tuyệt đối ở đoạn tóm tắt tôi trích dùng. citeturn40view2turn39view3 |

## Kiến trúc decoder tối ưu đề xuất và khuyến nghị triển khai hardware

Nếu mục tiêu của bạn là **một decoder HQC thực dụng, nhanh, dễ đóng timing và còn giữ tính tương thích với chuẩn**, tôi khuyến nghị kiến trúc như sau: giữ nguyên **RM nội = duplicated RM(1,7)**, dùng **khối de-duplication cộng song song** rồi qua **FHT pipeline 7 tầng**, sinh ra **hard decision** và đồng thời rút ra **độ tin cậy**; sau đó ghép thành ký hiệu RS và đưa vào **RS engine** dùng **ePIBMA/eCSEE**. Bố cục này bám sát hướng của đặc tả HQC và các thiết kế HQC phần cứng tốt nhất hiện có. citeturn28view0turn28view1turn26view1turn26view4

Nếu mục tiêu là **tối ưu hơn chuẩn** và bạn chấp nhận một đường nghiên cứu không còn “drop-in compatible” theo decoder chính thức, hãy thêm một nhánh **soft-information path** sau RM, rồi thay hard RS bằng **GMD RS decoder**. Paper 2026 cho thấy hướng này không những hợp lý về coding gain, mà còn có thể kéo **latency** và **area** toàn khối decryption đi xuống nhờ dùng codeword ngắn hơn. Đó là lý do tôi xem GMD là “đòn bẩy” lớn nhất cho thế hệ decoder HQC tiếp theo. citeturn20view2

Về **pipelining**, nên ưu tiên pipeline mạnh ở **FHT layer** và ở **Chien/evaluator** hơn là cố song song hóa vô điều kiện toàn bộ hệ. Lý do là dữ liệu thực nghiệm cho thấy fully parallel projection-style RM decoder có thể ngốn LUT cực lớn, trong khi các HQC decoder hiệu quả hơn lại thu được phần lớn lợi ích từ **khối RM bậc một chuyên biệt** và **RS short-code engine**. citeturn35view1turn27view0

Về **parallelism**, HQC nhìn chung không có nhiều cơ hội song song hóa cấp thuật toán; đây là nhận định được nói thẳng trong phần lịch điều độ của luận án Antognazza. Thay vì tìm “task-level parallelism” quá nhiều, hiệu quả hơn là tổ chức tốt **memory bandwidth**, dùng **true dual-port BRAM**, và xếp chồng những tác vụ có truy cập bộ nhớ không xung đột. Mốc “**năm dual-port RAM**” từ lịch điều độ của họ là một gợi ý rất thực tế. citeturn27view3

Về **fixed-point so với floating-point**, tất cả thiết kế HQC/RM/RS phần cứng mà tôi truy xuất được đều đi theo hướng **finite-field + integer datapath**, không phải floating-point. Với riêng RM(1,7) của HQC, đây cũng là lựa chọn tự nhiên: đầu vào sau de-duplication chỉ nằm trên tập giá trị nhỏ, còn FHT chỉ là chuỗi cộng/trừ. Từ tham số trong đặc tả, có thể **suy ra** rằng với multiplicity 5 và độ dài 128, biên độ cực đại của tổng Hadamard nằm cỡ **5 × 128 = 640**, nghĩa là một datapath signed khoảng **11 bit** đã đủ với một biên an toàn nhỏ; vì vậy dùng floating-point sẽ là chi phí không cần thiết. Đây là **suy luận kỹ thuật từ đặc tả**, không phải con số tác giả nêu trực tiếp. citeturn28view0turn27view0

Về **resource mapping**, với RS trên GF(2^8) tôi nghiêng về hai lựa chọn: hoặc **LUT/composite-field multipliers** nếu bạn tối ưu logic, hoặc **BRAM log/antilog tables** nếu bạn tối ưu thời gian phát triển và chấp nhận đổi bộ nhớ lấy đơn giản datapath. Các bảng tài nguyên trong HQC hardware cho thấy nhiều thiết kế hiệu quả có thể đạt đích với **0 DSP** hoặc gần như không cần DSP cho khối GF/logic mã, nên không nên mặc định đẩy bài toán này sang DSP slices. citeturn28view2turn27view0turn26view2

Về **constant-time/security**, đây là điểm đặc biệt quan trọng nếu bạn làm HQC dùng trong mật mã chứ không phải chỉ M&C. Bài báo Cortex-M4 2025 nhấn mạnh rõ việc họ chọn **dense-dense multiplication** và **constant-time algorithms** để giảm nguy cơ lộ thông tin triển khai. Suy ra, mọi tối ưu kiểu “early stop”, “skip branch do ít lỗi”, “sparse-dependent memory traffic” nên được xem xét rất chặt dưới lăng kính side-channel trước khi đem vào decapsulation path. citeturn14view2turn15view5

### Sơ đồ kiến trúc decoder tối ưu đề xuất

```mermaid
flowchart TD
    A[Ciphertext HQC / noisy codeword] --> B[Polynomial ops và tách payload]
    B --> C[Phân khối theo mã nội RM]
    C --> D[De-duplication buffer]
    D --> E[FHT pipeline cho RM(1,7)]
    E --> F[Hard decision bits]
    E --> G[Trích độ tin cậy]
    F --> H[Ghép symbol RS]
    G --> I{Chế độ decoder}
    I -->|Chuẩn / tương thích spec| J[RS hard-decision engine]
    I -->|Nghiên cứu tối ưu| K[GMD test-vector generator]
    K --> L[RS GMD engine]
    H --> J
    H --> L
    J --> M[Correction + recover message]
    L --> M
    M --> N[Verify / re-encrypt check / hash check]
    N --> O[Thông điệp hoặc shared secret]
```

Về quyết định thiết kế, tôi khuyến nghị như sau. **Nếu bạn cần một decoder thực thi chuẩn HQC ngay bây giờ**, hãy chọn nhánh **J** trong sơ đồ: FHT-RM + hard-decision RS. **Nếu bạn đang làm paper, thesis, hoặc accelerator mới**, đáng thử nhất là nhánh **K–L**: giữ nguyên RM đầu vào nhưng đưa thêm reliability path để bật **GMD RS**. Đó là điểm có bằng chứng tốt nhất hiện nay cho lợi ích **thuật toán + phần cứng** cùng lúc. citeturn20view2turn26view4turn27view0