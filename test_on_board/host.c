#include <stdio.h>
#include "adder.h"

int main() {
    int a = 69, b = 96, res = 0;
    printf("ARM: Gui lenh xuong DSP tinh %d + %d\n", a, b);

    // Hung ma loi tra ve vao bien ret
    int ret = adder_sum(a, b, &res);

    if (ret == 0) {
        printf("DSP: Tra ve ket qua = %d\n", res);
        printf("Ngon cmnl, duong ong thong roi!\n");
    } else {
        // In ra ma loi o dang Hex (0x8000xxxx) de de debug
        printf("Loi cmnr! Ma loi FastRPC: 0x%x\n", ret);
        
        if (ret == 0x80000406) {
            printf("Goi y: DSP khong tim thay file libadder_skel.so. Check lai ADSP_LIBRARY_PATH!\n");
        } else if (ret == 0x80000403) {
            printf("Goi y: Loi Signature (hang lau). DSP tu choi load file .so cua may!\n");
        }
    }
    return 0;
}
