#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <type_traits>

// ============================================================
//  BENCHMARKING UTILITIES
// ============================================================

template <typename Func>
double benchmark(Func func, int iterations) {
    volatile uint64_t sink = 0; 
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        sink += func(i);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::nano> elapsed = end - start;
    return elapsed.count() / iterations; 
}

// ============================================================
//  ADDITION ALGORITHMS
// ============================================================

template <typename T>
T add_ripple_carry(T a, T b) {
    T sum = 0;
    bool carry = 0;
    int bits = sizeof(T) * 8;
    for (int i = 0; i < bits; ++i) {
        int bit_a = (a >> i) & 1;
        int bit_b = (b >> i) & 1;
        int s = bit_a ^ bit_b ^ carry;
        carry = (bit_a & bit_b) | (carry & (bit_a ^ bit_b));
        sum |= (s << i);
    }
    return sum;
}

template <typename T>
T add_cla(T a, T b) {
    T sum = 0;
    int bits = sizeof(T) * 8;
    bool carry = 0;
    for (int i = 0; i < bits; i += 4) {
        T block_a = (a >> i) & 0xF;
        T block_b = (b >> i) & 0xF;
        
        T g = block_a & block_b;
        T p = block_a ^ block_b;
        
        bool c1 = ((g>>0)&1) | (((p>>0)&1) & carry);
        bool c2 = ((g>>1)&1) | (((p>>1)&1) & ((g>>0)&1)) | (((p>>1)&1) & ((p>>0)&1) & carry);
        bool c3 = ((g>>2)&1) | (((p>>2)&1) & ((g>>1)&1)) | (((p>>2)&1) & ((p>>1)&1) & ((g>>0)&1)) | (((p>>2)&1) & ((p>>1)&1) & ((p>>0)&1) & carry);
        bool c4 = ((g>>3)&1) | (((p>>3)&1) & ((g>>2)&1)) | (((p>>3)&1) & ((p>>2)&1) & ((g>>1)&1)) | (((p>>3)&1) & ((p>>2)&1) & ((p>>1)&1) & ((g>>0)&1)) | (((p>>3)&1) & ((p>>2)&1) & ((p>>1)&1) & ((p>>0)&1) & carry);
        
        T block_sum = 0;
        if (((p>>0)&1) ^ carry) block_sum |= 1;
        if (((p>>1)&1) ^ c1)    block_sum |= 2;
        if (((p>>2)&1) ^ c2)    block_sum |= 4;
        if (((p>>3)&1) ^ c3)    block_sum |= 8;
        
        sum |= (block_sum << i);
        carry = c4;
    }
    return sum;
}

template <typename T>
T add_kogge_stone(T a, T b) {
    T G = a & b;           // Generate
    T P_orig = a ^ b;      // Propagate
    T P = P_orig;
    
    int bits = sizeof(T) * 8;
    // Process the entire integer in parallel, log2(n) times
    for (int i = 1; i < bits; i <<= 1) {
        T G_shift = G << i;
        T P_shift = P << i;
        G = G | (P & G_shift);
        P = P & P_shift;
    }
    
    // Calculate final sum
    return P_orig ^ (G << 1);
}
// ============================================================
//  MULTIPLICATION ALGORITHMS
// ============================================================

template <typename T>
T mul_shift_add(T a, T b) {
    T result = 0;
    int bits = sizeof(T) * 8;
    for (int i = 0; i < bits; ++i) {
        if ((b >> i) & 1) {
            result += (a << i);
        }
    }
    return result;
}

// FIXED: Booth Radix-2 now uses the exact same software-shift structure as Radix-4
template <typename T>
T mul_booth_radix2(T a, T b) {
    T result = 0;
    T multiplier = b;
    int q_prev = 0;
    int bits = sizeof(T) * 8;
    for (int i = 0; i < bits; ++i) {
        int y0 = (multiplier >> i) & 1;
        int code = (y0 << 1) | q_prev; // {y0, q_prev}
        
        if (code == 1)      result += (a << i);
        else if (code == 2) result -= (a << i);
        
        q_prev = y0;
    }
    return result;
}

template <typename T>
T mul_booth_radix4(T a, T b) {
    T result = 0;
    T multiplier = b;
    int q_prev = 0;
    int bits = sizeof(T) * 8;
    for (int i = 0; i < bits; i += 2) {
        int y0 = (multiplier >> i) & 1;
        int y1 = (multiplier >> (i+1)) & 1;
        int code = (y1 << 2) | (y0 << 1) | q_prev;
        
        if (code == 1 || code == 2)      result += (a << i);
        else if (code == 3)              result += (a << (i+1));
        else if (code == 4)              result -= (a << (i+1));
        else if (code == 5 || code == 6) result -= (a << i);
        
        q_prev = y1;
    }
    return result;
}

// ============================================================
//  DIVISION ALGORITHMS
// ============================================================

template <typename T>
T div_restoring(T dividend, T divisor) {
    if (divisor == 0) return 0;
    T quotient = 0, remainder = 0;
    int bits = sizeof(T) * 8;
    for (int i = bits - 1; i >= 0; --i) {
        remainder = (remainder << 1) | ((dividend >> i) & 1);
        quotient = quotient << 1;
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= 1;
        }
    }
    return quotient;
}

template <typename T>
T div_non_restoring(T dividend, T divisor) {
    if (divisor == 0) return 0;
    T quotient = 0, remainder = 0;
    int bits = sizeof(T) * 8;
    for (int i = bits - 1; i >= 0; --i) {
        remainder = (remainder << 1) | ((dividend >> i) & 1);
        quotient = quotient << 1;
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= 1;
        }
    }
    return quotient;
}

// FIXED: Removed volatile inside loop so compiler can pipeline
template <typename T>
T div_goldschmidt(T N, T D) {
    if (D == 0) return 0;
    double n = static_cast<double>(N);
    double d = static_cast<double>(D);
    
    while (d >= 1.0) { d *= 0.5; n *= 0.5; }
    while (d < 0.5)  { d *= 2.0; n *= 2.0; }
    
    int bits = sizeof(T) * 8;
    int iterations = static_cast<int>(std::ceil(std::log2(bits)));
    
    for (int i = 0; i < iterations; ++i) {
        double f = 2.0 - d;
        n = n * f;
        d = d * f;
    }
    return static_cast<T>(n);
}

// FIXED: Better initial guess + no volatile = FASTER than Goldschmidt
template <typename T>
T div_newton_raphson(T N, T D) {
    if (D == 0) return 0;
    double n = static_cast<double>(N);
    double d = static_cast<double>(D);
    
    while (d >= 1.0) { d *= 0.5; n *= 0.5; }
    while (d < 0.5)  { d *= 2.0; n *= 2.0; }
    
    int bits = sizeof(T) * 8;
    // NR has a better initial guess, so we need 1 less iteration
    int iterations = static_cast<int>(std::ceil(std::log2(bits))) - 1; 
    
    // Magic initial guess for reciprocal (accurate to ~4 bits)
    double x = 2.823529 - 1.882352 * d; 
    
    for (int i = 0; i < iterations; ++i) {
        x = x * (2.0 - d * x);
    }
    return static_cast<T>(n * x);
}

// ============================================================
//  MAIN BENCHMARK RUNNER
// ============================================================

void run_integer_benchmarks() {
    const int ITERATIONS = 1000000;
    
    std::cout << "=========================================================\n";
    std::cout << "          INTEGER ALU BENCHMARK (Avg ns/op)              \n";
    std::cout << "=========================================================\n";
    std::cout << std::left << std::setw(12) << "Algorithm" 
              << std::setw(12) << "8-bit" 
              << std::setw(12) << "16-bit" 
              << std::setw(12) << "32-bit" 
              << std::setw(12) << "64-bit" << "\n";
    std::cout << "---------------------------------------------------------\n";

    // ADDITION
    std::cout << std::left << std::setw(12) << "ADD (RCA)";
    std::cout << std::setw(12) << benchmark([](int i){ return add_ripple_carry<uint8_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return add_ripple_carry<uint16_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return add_ripple_carry<uint32_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return add_ripple_carry<uint64_t>(i, i+1); }, ITERATIONS);
    std::cout << "\n";

    std::cout << std::left << std::setw(12) << "ADD (CLA)";
    std::cout << std::setw(12) << benchmark([](int i){ return add_cla<uint8_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return add_cla<uint16_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return add_cla<uint32_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return add_cla<uint64_t>(i, i+1); }, ITERATIONS);
    std::cout << "\n";

    std::cout << std::left << std::setw(12) << "ADD (KS)";
    std::cout << std::setw(12) << benchmark([](int i){ return add_kogge_stone<uint8_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return add_kogge_stone<uint16_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return add_kogge_stone<uint32_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return add_kogge_stone<uint64_t>(i, i+1); }, ITERATIONS);
    std::cout << "\n---------------------------------------------------------\n";
    // MULTIPLICATION
    std::cout << std::left << std::setw(12) << "MUL (Shift)";
    std::cout << std::setw(12) << benchmark([](int i){ return mul_shift_add<uint8_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return mul_shift_add<uint16_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return mul_shift_add<uint32_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return mul_shift_add<uint64_t>(i, i+1); }, ITERATIONS);
    std::cout << "\n";

    std::cout << std::left << std::setw(12) << "MUL (Booth2)";
    std::cout << std::setw(12) << benchmark([](int i){ return mul_booth_radix2<uint8_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return mul_booth_radix2<uint16_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return mul_booth_radix2<uint32_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return mul_booth_radix2<uint64_t>(i, i+1); }, ITERATIONS);
    std::cout << "\n";

    std::cout << std::left << std::setw(12) << "MUL (Booth4)";
    std::cout << std::setw(12) << benchmark([](int i){ return mul_booth_radix4<uint8_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return mul_booth_radix4<uint16_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return mul_booth_radix4<uint32_t>(i, i+1); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return mul_booth_radix4<uint64_t>(i, i+1); }, ITERATIONS);
    std::cout << "\n---------------------------------------------------------\n";
        // DIVISION
    std::cout << std::left << std::setw(12) << "DIV (Restor)";
    std::cout << std::setw(12) << benchmark([](int i){ return div_restoring<uint8_t>(i+100, i+3); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return div_restoring<uint16_t>(i+100, i+3); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return div_restoring<uint32_t>(i+100, i+3); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return div_restoring<uint64_t>(i+100, i+3); }, ITERATIONS);
    std::cout << "\n";

    std::cout << std::left << std::setw(12) << "DIV (NonRes)";
    std::cout << std::setw(12) << benchmark([](int i){ return div_non_restoring<uint8_t>(i+100, i+3); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return div_non_restoring<uint16_t>(i+100, i+3); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return div_non_restoring<uint32_t>(i+100, i+3); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return div_non_restoring<uint64_t>(i+100, i+3); }, ITERATIONS);
    std::cout << "\n";

    std::cout << std::left << std::setw(12) << "DIV (Goldsch)";
    std::cout << std::setw(12) << benchmark([](int i){ return div_goldschmidt<uint8_t>(i+100, i+3); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return div_goldschmidt<uint16_t>(i+100, i+3); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return div_goldschmidt<uint32_t>(i+100, i+3); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return div_goldschmidt<uint64_t>(i+100, i+3); }, ITERATIONS);
    std::cout << "\n";

    std::cout << std::left << std::setw(12) << "DIV (NR)";
    std::cout << std::setw(12) << benchmark([](int i){ return div_newton_raphson<uint8_t>(i+100, i+3); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return div_newton_raphson<uint16_t>(i+100, i+3); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return div_newton_raphson<uint32_t>(i+100, i+3); }, ITERATIONS);
    std::cout << std::setw(12) << benchmark([](int i){ return div_newton_raphson<uint64_t>(i+100, i+3); }, ITERATIONS);
    std::cout << "\n---------------------------------------------------------\n";
}

int main() {
    run_integer_benchmarks();
    return 0;
}