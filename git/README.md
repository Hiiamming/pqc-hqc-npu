# HQC Concatenated Code Flow

README này mô tả flow của khối mã hóa/giải mã concatenated code trong HQC, đôi khi được viết tắt là `concated`, gồm 2 tầng:

1. Reed-Solomon (RS)
2. Duplicated Reed-Muller (RM)

Trong code C, flow nằm ở `hqc/src/common/code.c`. Trong phần hardware, flow encode nằm ở `pqc-hqc-hardware/hardware/encap/concat_code.v`, còn flow decode nằm ở `pqc-hqc-hardware/hardware/decap/hqc_decod_top.v`.

## Tham số chính

| Parameter set | `PARAM_K` / `K_BYTES` | `PARAM_N1` / `N1_BYTES` | `PARAM_N2` | Concatenated length |
| --- | ---: | ---: | ---: | ---: |
| HQC-128 / `hqc128` | 16 bytes | 46 bytes | 384 bits | 17,664 bits = 2,208 bytes |
| HQC-192 / `hqc192` | 24 bytes | 56 bytes | 640 bits | 35,840 bits = 4,480 bytes |
| HQC-256 / `hqc256` | 32 bytes | 90 bytes | 640 bits | 57,600 bits = 7,200 bytes |

Ý nghĩa:

- `K`: độ dài message gốc tính theo byte.
- `N1`: độ dài codeword sau RS tính theo byte.
- `N2`: số bit RM tạo ra cho mỗi byte RS.
- Concatenated codeword có độ dài `N1 * N2` bit.

## Đọc thêm trong paper

Repo này có 2 paper liên quan trực tiếp:

| File | Nên đọc phần nào | Liên quan trong README |
| --- | --- | --- |
| `hqc_specifications_2025_08_22.pdf` | Section 3.4, "Concatenated Reed-Muller and Reed-Solomon codes" | Định nghĩa concatenated code, shortened RS, duplicated RM, encode/decode toán học. |
| `hqc_specifications_2025_08_22.pdf` | Section 3.5, HQC-PKE | Chỗ `C.Encode(m)` và `C.Decode(...)` được dùng trong encrypt/decrypt. |
| `hqc_specifications_2025_08_22.pdf` | Section 4.1, parameter sets | Bảng `n`, `n1`, `n2`, `k`, `delta`, DFR. |
| `2022-1183.pdf` | Section 2.2, "Encode and Decode Modules" | Hardware concat encode/decode: RS trước RM khi encode, RM trước RS khi decode. |
| `2022-1183.pdf` | Appendix 1.A.2 | Matrix của duplicated RM và thuật toán HQC PKE/KEM tóm tắt. |
| `2022-1183.pdf` | Table 14 | Parameter hardware: `n1`, `n2`, multiplicity của RM. |

Lưu ý khi đọc paper:

- Specification paper ghi RS-S1 là `[46, 16, 31]`, trong đó `31` là minimum distance `dmin = 2*delta + 1`.
- Code/hardware thường dùng `PARAM_DELTA = 15` cho HQC-128, tức sửa được tối đa 15 symbol lỗi RS.
- Hardware paper Table 14 ghi Reed-Solomon `[46, 16, 15]`; con số cuối khớp `delta`, không khớp `dmin`. Vì vậy trong README này mình dùng cả 2 cách gọi rõ ràng: `dmin = 31`, `delta = 15`.

## Vì sao parity RS nhiều?

Parity đúng là overhead, nên về mặt kích thước thì muốn nhỏ. Nhưng với RS, parity chính là ngân sách sửa lỗi ở tầng byte.

Quy tắc nhanh:

```text
RS parity symbols = N1 - K = 2 * delta
RS sửa được tối đa delta symbol lỗi
1 symbol RS = 1 byte
```

Với HQC:

| Parameter set | `N1-K` parity bytes | `PARAM_DELTA` | Ý nghĩa |
| --- | ---: | ---: | --- |
| HQC-128 | 30 | 15 | RS sửa được tối đa 15 byte lỗi sau RM decode. |
| HQC-192 | 32 | 16 | RS sửa được tối đa 16 byte lỗi sau RM decode. |
| HQC-256 | 58 | 29 | RS sửa được tối đa 29 byte lỗi sau RM decode. |

Vậy parity không phải "checksum nhỏ để phát hiện lỗi". Ở đây parity là phần dư có cấu trúc để vừa tìm vị trí byte lỗi, vừa tính giá trị lỗi cần XOR để sửa.

Toy intuition:

```text
Nếu chỉ có 2 parity byte:
  -> RS chỉ sửa được khoảng 1 byte lỗi.

Nếu có 30 parity byte:
  -> RS có đủ thông tin để sửa tối đa 15 byte lỗi.
```

HQC cần mức này vì trước RS là RM decode: RM đã sửa rất nhiều bit noise, nhưng vẫn có thể trả sai một vài byte RS. RS là tầng cuối để dọn các byte còn sai đó, nên parity nhìn nhiều nhưng là trade-off để đạt decryption failure rate rất thấp.

## Flow encode

Flow tổng quát:

```text
message m
  -> Reed-Solomon encode
  -> RS codeword tmp
  -> Reed-Muller encode
  -> concatenated codeword em
```

Trong C:

```c
void code_encode(uint64_t *em, const uint64_t *m) {
    uint64_t tmp[VEC_N1_SIZE_64] = {0};

    reed_solomon_encode(tmp, m);
    reed_muller_encode(em, tmp);
}
```

Trong hardware:

```text
concat_code.start
  -> reed_solomon_encode.start
  -> done_rs
  -> reed_muller_encode.start
  -> concat_code.done
```

### 1. Reed-Solomon encode

Module/API:

- C: `reed_solomon_encode(tmp, m)`
- Verilog: `reed_solomon_encode`

Input:

| Tên | Mô tả |
| --- | --- |
| `m` / `msg_in` | Message gốc, dài `K_BYTES`: 16/24/32 bytes tùy parameter set. |
| `start` | Tín hiệu bắt đầu trong hardware. |
| `clk`, `rst` | Clock/reset trong hardware. |

Output:

| Tên | Mô tả |
| --- | --- |
| `tmp` / `cdw_out` | RS codeword dài `N1_BYTES`: 46/56/90 bytes. |
| `done` | Hardware báo RS encode hoàn tất. |

Mục tiêu dễ hiểu:

- Input là message ngắn, ví dụ 16 byte với HQC-128.
- RS thêm các byte parity vào trước message.
- Output vẫn giữ nguyên message ở cuối, nên gọi là systematic codeword.
- Parity dùng để RS decode sửa lỗi nếu một vài byte bị sai sau này.

Chức năng trong code:

- RS encode là systematic encoding.
- Output RS codeword chứa phần message và parity.
- Trong C, message được copy vào cuối `cdw_bytes`: `cdw_bytes + PARAM_N1 - PARAM_K`.
- Trong Verilog, `cdw_out = {msg, cdw_bytes[N1-K-1:0]}` theo thứ tự bit bus của module.

Đối chiếu paper/code:

- Specification paper Section 3.4.2, "Encoding shortened Reed-Solomon codes": systematic form `c(x) = b(x) + x^(n-k)u(x)`, tức parity/remainder ở trước và message ở sau.
- C reference: `hqc/src/ref/reed_solomon.c`, hàm `reed_solomon_encode`, dùng shift register dài `PARAM_N1 - PARAM_K`.
- Hardware paper Section 2.2, "Encode Module": mô tả RS encode bằng LFSR với feedback theo generator polynomial.
- Verilog: `pqc-hqc-hardware/hardware/encap/reed_solomon_encode.v`.

Flow chi tiết, nhìn như shift register:

```text
message bytes m
  -> xem mỗi byte là 1 symbol trong GF(2^8)
  -> chạy shift-register RS với generator polynomial g(x)
  -> sinh parity bytes p
  -> ghép systematic codeword c = parity || message
```

Dạng dữ liệu:

```text
m = [m0, m1, ..., mK-1]
p = [p0, p1, ..., p(N1-K-1)]
c = [p0, p1, ..., p(N1-K-1), m0, m1, ..., mK-1]
```

Lưu ý về `GF(2^8)`:

- Một symbol RS chính là 1 byte.
- Cộng/trừ trong GF(2^8) là XOR.
- Nhân trong GF(2^8) không phải nhân integer bình thường; code dùng bảng/log hoặc module nhân GF để làm.

Dưới dạng polynomial trên GF(2^8), nếu cần đối chiếu với code:

```text
m(x) = m0 + m1*x + ... + m(K-1)*x^(K-1)
g(x) = g0 + g1*x + ... + g(PARAM_G-1)*x^(PARAM_G-1)
c(x) = c0 + c1*x + ... + c(N1-1)*x^(N1-1)
```

Với HQC-128, `PARAM_G = 31`, generator polynomial trong `RS_POLY_COEFS` có coefficients:

```text
g = [
  89, 69, 153, 116, 176, 117, 111, 75,
  73, 233, 242, 233, 65, 210, 21, 139,
  103, 173, 67, 118, 105, 210, 174, 110,
  74, 69, 228, 82, 255, 181, 1
]
```

Với HQC-128:

```text
K = 16, N1 = 46, parity length = 30
c = [p0..p29, m0..m15]
c(x) = p0 + p1*x + ... + p29*x^29 + m0*x^30 + ... + m15*x^45
```

Toy example với message rất nhỏ:

Để dễ nhìn, giả sử có RS toy với:

```text
K = 3 bytes
N1 = 7 bytes
parity length = 4 bytes
message m = [11 22 33]
generator toy g = [g0 g1 g2 g3 01]
```

Output mong muốn có dạng:

```text
c = [p0 p1 p2 p3 11 22 33]
```

Các bước encode:

```text
Bước 1. Init parity
p = [00 00 00 00]

Bước 2. Đọc message byte đầu tiên
z = 0x11

Vì p ban đầu toàn 0, feedback đầu tiên có thể hiểu đơn giản là:
feedback = z xor p_last = 0x11 xor 0x00 = 0x11

Bước 3. Nhân feedback với generator
feedback * g = [
  0x11*g0,
  0x11*g1,
  0x11*g2,
  0x11*g3
]

Bước 4. Shift parity rồi XOR phần vừa tính vào
p cũ: [00 00 00 00]
p mới: [
  00 xor 0x11*g0,
  00 xor 0x11*g1,
  00 xor 0x11*g2,
  00 xor 0x11*g3
]

Bước 5. Đọc byte tiếp theo z = 0x22
feedback = z xor p_last
Sau đó lại nhân feedback với g, shift parity, XOR vào p.

Bước 6. Đọc byte cuối z = 0x33
Lặp lại y hệt.
```

Điểm cần nhớ:

- Encoder không “mã hóa từng bit message” riêng lẻ ở RS stage.
- Nó đọc từng byte `z` như một symbol trong GF(2^8).
- Mỗi byte message làm parity register thay đổi một lần.
- Sau khi đọc hết message, parity register chính là `[p0 p1 p2 p3]`.

Example HQC-128:

```text
Input message:
m = [01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10]

RS encoder xử lý giống toy example nhưng parity dài hơn:
1. init parity p = [00 ... 00] dài 30 bytes
2. đọc z = 0x01 trong GF(2^8), tính feedback từ z và byte cuối của parity
3. nhân feedback với 30 hệ số generator
4. shift/update parity bằng XOR
5. đọc z = 0x02, làm lại cùng công thức
6. tiếp tục tới z = 0x10

Output shape:
c = [p0 p1 ... p29 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10]
```

Với input toàn zero:

```text
m = [00 ... 00]
p = [00 ... 00]
c = [00 ... 00]
```

### 2. Reed-Muller encode

Module/API:

- C: `reed_muller_encode(em, tmp)`
- Verilog: `reed_muller_encode`, dùng `rm_encoder` cho từng byte.

Input:

| Tên | Mô tả |
| --- | --- |
| `tmp` / `rs_cdw_in` | RS codeword dài `N1_BYTES`. |
| `start` | Trong hardware, nhận từ `done_rs`. |
| `cdw_out_en`, `cdw_out_addr` | Hardware dùng để đọc output memory theo địa chỉ. |

Output:

| Tên | Mô tả |
| --- | --- |
| `em` | Concatenated codeword dài `N1 * N2` bit. |
| `cdw_out` | Hardware trả từng RM base block 128-bit từ RAM output. |
| `done` | Hardware báo RM encode hoàn tất, cũng là `concat_code.done`. |

Mục tiêu dễ hiểu:

- Input của RM là từng byte trong RS codeword.
- Mỗi byte biến thành một block 128 bit.
- Block 128 bit này được lặp lại 3 lần với HQC-128, hoặc 5 lần với HQC-192/HQC-256.
- Lặp lại giúp RM decode dùng vote/score để chịu được bit flip.

Chức năng trong code:

- Mỗi byte của RS codeword được encode bằng RM(1,7) thành 128 bit.
- Trong C, 128-bit RM codeword này được copy/lặp `MULTIPLICITY` lần để tạo đủ `N2` bit:
  - HQC-128: `N2 = 384`, `MULTIPLICITY = 3`.
  - HQC-192/HQC-256: `N2 = 640`, `MULTIPLICITY = 5`.
- Vì vậy mỗi byte RS trở thành `N2` bit trong concatenated codeword.

Đối chiếu paper/code:

- Specification paper Section 3.4.3, "Duplicated Reed-Muller codes": RM(1,7) là `[128,8,64]`, rồi duplicate thành `[384,8,192]` hoặc `[640,8,320]`.
- Hardware paper Appendix 1.A.2, "Encode of duplicated Reed-Muller code": đưa đúng generator matrix các hàng `aaaa`, `cccc`, `f0f0`, ...
- C reference: `hqc/src/ref/reed_muller.c`, hàm `encode` và `reed_muller_encode`.
- Verilog: `pqc-hqc-hardware/hardware/encap/rm_encoder.v` và `reed_muller_encode.v`.

Hardware output:

- `reed_muller_encode` ghi 1 RM base block 128-bit cho mỗi byte RS vào `mem_single`.
- Địa chỉ RAM trong encoder có range `0 .. N1_BYTES-1`.
- Khi dùng trong `encrypt.v` / `encrypt_parallel.v`, phần lặp `MULTIPLICITY` được tạo ở phía đọc bằng `cdw_out_addr = xor_add_addr / COPIES_OF_CDW`, nên cùng một 128-bit block được đọc lại 3 hoặc 5 lần.
- `cdw_out_en = 1` cho phép đọc `cdw_out` tại `cdw_out_addr`.

Flow chi tiết:

```text
RS codeword c = [c0, c1, ..., c(N1-1)]
  -> lấy từng byte ci
  -> tách ci thành 8 bit: b0..b7
  -> chọn các hàng encoding matrix tương ứng bit = 1
  -> XOR các hàng được chọn để ra RM base codeword 128-bit wi
  -> lặp wi MULTIPLICITY lần
  -> concatenated codeword em
```

Dạng dữ liệu:

```text
ci = b0 + 2*b1 + 4*b2 + ... + 128*b7

wi = (b0 * M0) xor (b1 * M1) xor ... xor (b7 * M7)

em = [
  repeat(w0, MULTIPLICITY),
  repeat(w1, MULTIPLICITY),
  ...
  repeat(w(N1-1), MULTIPLICITY)
]
```

Trong `rm_encoder.v`, các matrix chính là:

```text
M0 = 128'haaaa...aaaa
M1 = 128'hcccc...cccc
M2 = 128'hf0f0...f0f0
M3 = 128'hff00...ff00
M4 = 128'hffff0000...ffff0000
M5 = 128'h00000000ffffffff00000000ffffffff
M6 = 128'h0000000000000000ffffffffffffffff
M7 = 128'hffffffffffffffffffffffffffffffff
```

Toy example 1: byte bằng 0

```text
Input 1 RS byte:
c0 = 0x00 = 0000_0000b

Selected matrix:
none

RM base codeword:
w0 = 128'h00000000000000000000000000000000

HQC-128 output for c0:
repeat(w0, 3) = [w0, w0, w0] = 384 bits
```

Toy example 2: byte chỉ bật bit thấp nhất

```text
Input 1 RS byte:
c1 = 0x01 = 0000_0001b

Selected matrix:
M0

RM base codeword:
w1 = M0 = 128'haaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa

HQC-192/HQC-256 output for c1:
repeat(w1, 5) = [w1, w1, w1, w1, w1] = 640 bits
```

Toy example 3: byte bật nhiều bit

```text
Input 1 RS byte:
c2 = 0x05 = 0000_0101b

Các bit bật:
bit 0 = 1
bit 2 = 1

Selected matrix:
M0 và M2

RM base codeword:
w2 = M0 xor M2
   = 128'haaaa...aaaa xor 128'hf0f0...f0f0
   = 128'h5a5a...5a5a

HQC-128 output cho byte này:
repeat(w2, 3) = [w2, w2, w2] = 384 bits
```

Toy example 4: ghép nhiều byte RS

```text
Giả sử RS codeword toy:
c = [00 01 05]

RM encode từng byte:
00 -> w0 = 0000...0000
01 -> w1 = aaaa...aaaa
05 -> w2 = 5a5a...5a5a

Nếu MULTIPLICITY = 3:
em = [
  w0, w0, w0,
  w1, w1, w1,
  w2, w2, w2
]
```

## Flow decode

Flow decode đi ngược lại encode:

```text
received concatenated codeword em
  -> Reed-Muller decode
  -> recovered RS codeword tmp
  -> Reed-Solomon decode
  -> recovered message m
```

Trong C:

```c
void code_decode(uint64_t *m, const uint64_t *em) {
    uint64_t tmp[VEC_N1_SIZE_64] = {0};

    reed_muller_decode(tmp, em);
    reed_solomon_decode(m, tmp);
}
```

Trong hardware:

```text
hqc_decod_top.start_i
  -> hqc_rmdecod_top
  -> rm_dout/rm_dout_valid/rm_dout_done
  -> hqc_rsdecod_top
  -> dout_o/dout_valid_o
```

### 3. Reed-Muller decode

Module/API:

- C: `reed_muller_decode(tmp, em)`
- Verilog: `hqc_rmdecod_top`

Input:

| Tên | Mô tả |
| --- | --- |
| `em` | Concatenated codeword dài `N1 * N2` bit. |
| `ram_din_i` | Hardware input từng block 128-bit. |
| `start_i` | Tín hiệu bắt đầu decode. |

Output:

| Tên | Mô tả |
| --- | --- |
| `tmp` | RS codeword đã recover, dài `N1_BYTES`. |
| `ram_dout_o` | Hardware output từng byte RS. |
| `ram_dout_wr_o` | Valid/write enable cho từng byte output. |
| `ram_dout_addr_o` | Địa chỉ byte output. |
| `done_o` | Báo RM decode xong toàn bộ `N1` byte. |

Mục tiêu dễ hiểu:

- Input là các block 128 bit đã bị noise làm flip một số bit.
- Vì encode đã lặp cùng một block nhiều lần, decode có thể cộng điểm theo từng vị trí bit.
- Sau đó Hadamard/find peak chọn lại pattern gần nhất.
- Output của mỗi nhóm block là 1 byte RS.

Đối chiếu paper/code:

- Specification paper Section 3.4.3, "Decoding Duplicated Reed-Muller codes": 3 bước là transform `F`, Hadamard transform, rồi tìm peak.
- Hardware paper Section 2.2, "Decode Module": `expand and sum`, `hadamard transformation`, `find peak`.
- C reference: `hqc/src/ref/reed_muller.c`, các hàm `expand_and_sum`, `hadamard`, `find_peaks`.
- Verilog: `hqc_rmdecod_expnsum.v`, `hqc_rmdecod_hadamard.v`, `hqc_rmdecod_findpeaks.v`.

Flow chi tiết:

```text
received RM blocks for byte i
  -> expand_and_sum
  -> vector score 128 phần tử
  -> Hadamard transform
  -> find peak lớn nhất theo trị tuyệt đối
  -> recover lại 1 byte RS ci
```

Các bước nội bộ, đọc theo nghĩa đơn giản:

1. `expand_and_sum`: gom các bản lặp 128-bit của một RM codeword và cộng từng bit.
2. `hadamard`: đổi vector điểm thành dạng dễ tìm pattern RM nhất.
3. `find_peaks`: tìm pattern mạnh nhất để recover lại 1 byte RS.

Dạng dữ liệu:

```text
Input cho 1 byte:
[w_i_copy0, w_i_copy1, ..., w_i_copy(MULTIPLICITY-1)]

Sau expand_and_sum:
s = [s0, s1, ..., s127]
trong đó sj là tổng số bit 1 tại vị trí j qua các copy

Sau Hadamard:
h = H(s)

Output byte:
ci = peak_position_low_7_bits + sign_bit_as_bit_7
```

Toy example không có lỗi:

```text
Input:
3 copy của cùng RM block encode từ ci = 0x01
[M0, M0, M0]

expand_and_sum:
các vị trí bit 1 của M0 có score = 3
các vị trí bit 0 của M0 có score = 0

hadamard + find_peaks:
peak trỏ về pattern của M0

Output:
ci = 0x01
```

Toy example có lỗi nhẹ:

```text
Input:
[M0, M0 with vài bit bị flip, M0]

expand_and_sum:
đa số vị trí vẫn giữ score đúng vì 2/3 copy còn sạch

hadamard + find_peaks:
peak vẫn gần pattern M0 nhất

Output:
ci = 0x01
```

Toy example nhìn theo từng bit position:

```text
Giả sử chỉ nhìn 8 vị trí đầu của block 128 bit.
M0 bắt đầu bằng pattern toy:
M0[0..7] = [1 0 1 0 1 0 1 0]

Input 3 copy, copy thứ 2 bị flip ở vị trí 2:
copy0 = [1 0 1 0 1 0 1 0]
copy1 = [1 0 0 0 1 0 1 0]
copy2 = [1 0 1 0 1 0 1 0]

expand_and_sum cộng theo cột:
s[0..7] = [3 0 2 0 3 0 3 0]

Ý nghĩa:
- score 3: cả 3 copy đều nói bit đó là 1
- score 2: đa số vẫn nói bit đó là 1
- score 0: cả 3 copy đều nói bit đó là 0

Hadamard/find_peaks sẽ thấy pattern này gần M0 nhất.
Output byte vẫn là 0x01.
```

Kết quả của RM decode là input trực tiếp cho RS decode.

### 4. Reed-Solomon decode

Module/API:

- C: `reed_solomon_decode(m, tmp)`
- Verilog: `hqc_rsdecod_top`

Input:

| Tên | Mô tả |
| --- | --- |
| `tmp` / `din_i` | RS codeword/recovered vector dài `N1_BYTES`. Hardware nhận từng byte. |
| `din_valid_i` | Valid cho từng byte input từ RM decode. |
| `din_done_i` | Báo RM đã gửi xong byte cuối. |
| `start_i` | Tín hiệu bắt đầu decode. |

Output:

| Tên | Mô tả |
| --- | --- |
| `m` / `dout_o` | Message recover dài `K_BYTES`. |
| `done_o` | Báo RS decode hoàn tất. |
| `busy_o`, `last_busy_o` | Trạng thái xử lý trong hardware. |

Mục tiêu dễ hiểu:

- Input là RS codeword sau RM decode, tức là một dãy byte.
- Nếu vài byte vẫn sai, RS decode sẽ tìm byte nào sai và sai bao nhiêu.
- Sau khi sửa, message được lấy từ phần cuối của systematic codeword.

Đối chiếu paper/code:

- Specification paper Section 3.4.2, "Decoding shortened Reed-Solomon codes": syndrome, error-location polynomial `sigma(x)`, roots, `Z(x)`, error values, correction.
- C reference: `hqc/src/ref/reed_solomon.c`, các hàm `compute_syndromes`, `compute_elp`, `compute_roots`, `compute_z_poly`, `compute_error_values`, `correct_errors`.
- Hardware paper Section 2.2, "Decode Module": mô tả cùng pipeline RS decode sau RM decode.
- Verilog: `hqc_rsdecod_syndromes.v`, `hqc_rsdecod_elp.v`, `hqc_rsdecod_roots.v`, `hqc_rsdecod_zpoly.v`, `hqc_rsdecod_err_val.v`.

Flow chi tiết:

```text
received RS codeword r
  -> compute syndromes S
  -> compute error locator polynomial sigma(x)
  -> find error locations bằng roots của sigma(x)
  -> compute z(x) / error evaluator polynomial
  -> compute error values
  -> correct r thành c
  -> lấy message từ systematic part của c
```

Các bước nội bộ, đọc theo nghĩa đơn giản:

1. Compute syndromes: kiểm tra codeword có lỗi không. Nếu toàn bộ syndrome bằng 0 thì coi như không có lỗi RS.
2. Compute ELP: tạo `sigma(x)`, hiểu đơn giản là object giữ thông tin “có lỗi ở vị trí nào”.
3. Compute roots: dùng `sigma(x)` để tìm index byte bị lỗi.
4. Compute z polynomial: tạo `z(x)`, hiểu đơn giản là object phụ để tính “byte đó sai giá trị bao nhiêu”.
5. Compute error values: tính mask lỗi `ei` cho từng vị trí byte.
6. Correct errors: sửa bằng XOR, `ci = ri xor ei`.
7. Extract message: copy `PARAM_K` byte message từ codeword đã sửa.

Dạng dữ liệu:

```text
r = [r0, r1, ..., r(N1-1)]
r(x) = r0 + r1*x + ... + r(N1-1)*x^(N1-1)

S = [S0, S1, ..., S(2*DELTA-1)]
sigma(x) = 1 + sigma1*x + sigma2*x^2 + ...
z(x) = z0 + z1*x + z2*x^2 + ...

error_values = [e0, e1, ..., e(N1-1)]
c = r xor error_values
m = c[(PARAM_G-1) .. (PARAM_G-1 + PARAM_K - 1)]
```

Trong C, message được lấy bằng:

```c
memcpy(msg, cdw_bytes + (PARAM_G - 1), PARAM_K);
```

Toy example không có lỗi:

```text
Received RS codeword:
r = [p0 p1 p2 p3 11 22 33]

Syndrome:
S = [00 00 ... 00]

RS decode kết luận:
không cần sửa gì

Output message:
m = [11 22 33]
```

Toy example có 1 byte lỗi:

```text
Original RS codeword:
c = [p0 p1 ... p29 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10]

Received RS codeword sau RM decode, có 1 lỗi tại byte 5:
r = [p0 p1 p2 p3 p4 (p5 xor e5) p6 ... p29 01 02 ... 10]

RS decode:
1. syndrome S != 0 nên biết có lỗi
2. sigma(x) giữ thông tin vị trí lỗi
3. roots chỉ ra lỗi tại index 5
4. z(x) + sigma(x) giúp tính error value = e5
5. sửa bằng XOR: r5 = r5 xor e5 = p5

Corrected codeword:
c = [p0 p1 ... p29 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10]

Output message:
m = [01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10]
```

Toy example lỗi nằm trong message part:

```text
Original corrected shape:
c = [p0 p1 ... p29 01 02 03 04 ... 10]

Received, byte message 0x03 bị sai thành 0x83:
r = [p0 p1 ... p29 01 02 83 04 ... 10]

RS decode tìm được:
error index = 32
error value = 0x80

Correct:
0x83 xor 0x80 = 0x03

Sau đó mới extract message:
m = [01 02 03 04 ... 10]
```

## Tóm tắt input/output toàn pipeline

### Encode

| Stage | Input | Output |
| --- | --- | --- |
| Concatenated encode | `m`: `K_BYTES` | `em`: `N1 * N2` bit |
| RS encode | `m`: `K_BYTES` | `tmp`: `N1_BYTES` |
| RM encode | `tmp`: `N1_BYTES` | `em`: `N1 * N2` bit |

### Decode

| Stage | Input | Output |
| --- | --- | --- |
| Concatenated decode | `em`: `N1 * N2` bit | `m`: `K_BYTES` |
| RM decode | `em`: `N1 * N2` bit | `tmp`: `N1_BYTES` |
| RS decode | `tmp`: `N1_BYTES` | `m`: `K_BYTES` |

## Mapping file quan trọng

| File | Vai trò |
| --- | --- |
| `hqc/src/common/code.c` | C wrapper cho concatenated encode/decode. |
| `hqc/src/ref/reed_solomon.c` | Reference RS encode/decode. |
| `hqc/src/ref/reed_muller.c` | Reference RM encode/decode. |
| `pqc-hqc-hardware/hardware/encap/concat_code.v` | Hardware top cho concatenated encode. |
| `pqc-hqc-hardware/hardware/encap/reed_solomon_encode.v` | Hardware RS encoder. |
| `pqc-hqc-hardware/hardware/encap/reed_muller_encode.v` | Hardware RM encoder. |
| `pqc-hqc-hardware/hardware/encap/rm_encoder.v` | Encode 1 byte thành RM 128-bit codeword. |
| `pqc-hqc-hardware/hardware/decap/hqc_decod_top.v` | Hardware top cho concatenated decode. |
| `pqc-hqc-hardware/hardware/decap/hqc_rmdecod_top.v` | Hardware RM decoder top. |
| `pqc-hqc-hardware/hardware/decap/hqc_rsdecod_top.v` | Hardware RS decoder top. |
