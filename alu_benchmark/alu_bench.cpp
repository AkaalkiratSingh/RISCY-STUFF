// bench.cpp - software ADD / MUL / DIV algorithm benchmark.
// Build: g++ -O2 -std=c++17 -Wall -Wextra bench.cpp -o bench && ./bench
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const uint64_t MASK48 = (1ULL << 48) - 1;
static inline uint64_t mask48(uint64_t x) { return x & MASK48; }

template <int W>
constexpr uint64_t wmask() { return W >= 64 ? ~0ULL : ((1ULL << W) - 1); }

// ---------------------------------------------------------------- ADD ----

// Ripple carry: one full adder per bit, carry rippled serially.
template <typename T, int W = int(sizeof(T) * 8)>
T add_ripple_carry(T a_, T b_) {
    uint64_t a = a_, b = b_, s = 0, c = 0;
    for (int k = 0; k < W; ++k) {
        uint64_t x = (a >> k) & 1, y = (b >> k) & 1;
        s |= (x ^ y ^ c) << k;
        c = (x & y) | (c & (x ^ y));
    }
    return (T)(s & wmask<W>());
}

// 4-bit block carry-lookahead: carries inside a block come from g/p
// equations (no branches); the block carry-out feeds the next block.
template <typename T, int W = int(sizeof(T) * 8)>
T add_cla(T a_, T b_) {
    uint64_t a = a_, b = b_, s = 0, c = 0;
    for (int k = 0; k < W; k += 4) {
        uint64_t x = (a >> k) & 0xF, y = (b >> k) & 0xF;
        uint64_t g = x & y, p = x ^ y;
        uint64_t g0 = g & 1, g1 = (g >> 1) & 1, g2 = (g >> 2) & 1, g3 = (g >> 3) & 1;
        uint64_t p0 = p & 1, p1 = (p >> 1) & 1, p2 = (p >> 2) & 1, p3 = (p >> 3) & 1;
        uint64_t c1 = g0 | (p0 & c);
        uint64_t c2 = g1 | (p1 & g0) | (p1 & p0 & c);
        uint64_t c3 = g2 | (p2 & g1) | (p2 & p1 & g0) | (p2 & p1 & p0 & c);
        uint64_t c4 = g3 | (p3 & g2) | (p3 & p2 & g1) | (p3 & p2 & p1 & g0) |
                      (p3 & p2 & p1 & p0 & c);
        uint64_t carries = c | (c1 << 1) | (c2 << 2) | (c3 << 3);
        s |= ((p ^ carries) & 0xF) << k;
        c = c4;
    }
    return (T)(s & wmask<W>());
}

// Kogge-Stone parallel-prefix adder: log2(W) generate/propagate stages.
template <typename T, int W = int(sizeof(T) * 8)>
T add_kogge_stone(T a_, T b_) {
    uint64_t a = a_, b = b_;
    uint64_t p0 = a ^ b, g = a & b, p = p0;
    for (int d = 1; d < W; d <<= 1) {
        g |= p & (g << d);
        p &= (p << d);
    }
    return (T)((p0 ^ (g << 1)) & wmask<W>());
}

// ---------------------------------------------------------------- MUL ----

// Booth radix-2: recode bit pairs (b[k-1], b[k]) into digits {-1,0,+1}.
template <typename T, int W = int(sizeof(T) * 8)>
T mul_booth_radix2(T a_, T b_) {
    uint64_t a = a_, b = b_, acc = 0, prev = 0;
    for (int k = 0; k < W; ++k) {
        uint64_t bit = (b >> k) & 1;
        uint64_t sh = a << k;
        acc += sh & (0 - (prev & ~bit & 1));  // digit +1
        acc -= sh & (0 - (bit & ~prev & 1));  // digit -1
        prev = bit;
    }
    return (T)(acc & wmask<W>());
}

// Booth radix-4: recode bit triples into digits {-2..+2}, two bits per step.
template <typename T, int W = int(sizeof(T) * 8)>
T mul_booth_radix4(T a_, T b_) {
    uint64_t a = a_, b2 = (uint64_t)b_ << 1, acc = 0;
    for (int k = 0; k < W; k += 2) {
        uint64_t x = (b2 >> k) & 7;
        uint64_t y0 = x & 1, y1 = (x >> 1) & 1, y2 = (x >> 2) & 1;
        uint64_t one = y0 ^ y1;
        uint64_t two = (y2 & ~y1 & ~y0 & 1) | (~y2 & y1 & y0 & 1);
        uint64_t term = ((a << k) & (0 - one)) | ((a << (k + 1)) & (0 - two));
        uint64_t neg = y2;
        acc += (term ^ (0 - neg)) + neg;  // conditional two's-complement negate
    }
    return (T)(acc & wmask<W>());
}

// ---------------------------------------------------------------- DIV ----

// Restoring: shift in a bit, trial-subtract, keep the result only if >= 0.
template <typename T, int W = int(sizeof(T) * 8)>
T div_restoring(T n_, T d_) {
    uint64_t n = n_ & wmask<W>(), d = d_, q = 0, r = 0;
    for (int k = W - 1; k >= 0; --k) {
        r = (r << 1) | ((n >> k) & 1);
        uint64_t m = 0 - (uint64_t)(r >= d);
        r -= d & m;
        q |= (1ULL << k) & m;
    }
    return (T)q;
}

// Non-restoring: signed partial remainder, add or subtract d by its sign,
// quotient bit = (remainder >= 0), single remainder correction at the end.
template <typename T, int W = int(sizeof(T) * 8)>
T div_non_restoring(T n_, T d_) {
    uint64_t n = n_ & wmask<W>(), q = 0;
    int64_t D = (int64_t)d_, r = 0;
    for (int k = W - 1; k >= 0; --k) {
        r = r * 2 + (int64_t)((n >> k) & 1);
        r += (r < 0) ? D : -D;
        q |= (uint64_t)(r >= 0) << k;
    }
    return (T)q;
}

// Split d into m * 2^e with m in [0.5,1); returns m and sc = 2^-e (exact).
static inline void split_divisor(uint64_t d, double& m, double& sc) {
    double dd = (double)d;
    uint64_t b;
    std::memcpy(&b, &dd, sizeof b);
    uint64_t ex = (b >> 52) & 0x7FF;
    uint64_t mb = (b & ~(0x7FFULL << 52)) | (0x3FEULL << 52);
    uint64_t sb = (2045 - ex) << 52;
    std::memcpy(&m, &mb, sizeof m);
    std::memcpy(&sc, &sb, sizeof sc);
}

// FP refinement steps needed grow with width: each step squares the error
// (1/17 -> 3.5e-3 -> 1.2e-5 -> 1.4e-10 -> 2e-20), so 8/16/32/48-bit need 1/2/3/4.
template <int W>
constexpr int fp_iters() { return W <= 8 ? 1 : (W <= 16 ? 2 : (W <= 32 ? 3 : 4)); }

// Integer fix-up so the FP estimate becomes exactly floor(n/d).
static inline uint64_t fix_quotient(uint64_t n, uint64_t d, uint64_t q) {
    int64_t r = (int64_t)(n - q * d);
    while (r < 0) { --q; r += (int64_t)d; }
    while ((uint64_t)r >= d) { ++q; r -= (int64_t)d; }
    return q;
}

// Goldschmidt: scale N and D by F = 2 - D each step until D -> 1, N -> n/d.
template <typename T, int W = int(sizeof(T) * 8)>
T div_goldschmidt(T n_, T d_) {
    uint64_t n = n_ & wmask<W>(), d = d_;
    if (d == 0) return 0;
    double m, sc;
    split_divisor(d, m, sc);
    double x0 = 48.0 / 17.0 - (32.0 / 17.0) * m;
    double N = (double)n * sc * x0, D = m * x0;
    for (int i = 0; i < fp_iters<W>(); ++i) {
        double F = 2.0 - D;
        N *= F;
        D *= F;
    }
    return (T)fix_quotient(n, d, (uint64_t)(N + 0.5));
}

// Newton-Raphson: refine x ~ 1/m with x *= (2 - m*x), then q = n*x.
template <typename T, int W = int(sizeof(T) * 8)>
T div_newton_raphson(T n_, T d_) {
    uint64_t n = n_ & wmask<W>(), d = d_;
    if (d == 0) return 0;
    double m, sc;
    split_divisor(d, m, sc);
    double x = 48.0 / 17.0 - (32.0 / 17.0) * m;
    for (int i = 0; i < fp_iters<W>(); ++i) x = x * (2.0 - m * x);
    return (T)fix_quotient(n, d, (uint64_t)((double)n * sc * x + 0.5));
}

// -------------------------------------------------------- CORRECTNESS ----

static void require(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "correctness check failed: %s\n", what);
        std::exit(1);
    }
}

template <typename T, int W>
static void check_width() {
    const uint64_t M = wmask<W>();
    const uint64_t vals[] = {0, 1, 2, 3, 7, 10, 12, 99, 100, 0xFF, 123456789ULL,
                             0xFFFFFFFFULL, 0xDEADBEEFULL, M - 1, M, 1ULL << (W - 1)};
    const uint64_t divs[] = {1, 2, 3, 7, 99, 100};
    for (uint64_t x : vals) {
        for (uint64_t y : vals) {
            T a = (T)(x & M), b = (T)(y & M);
            uint64_t ea = ((uint64_t)a + b) & M, em = ((uint64_t)a * b) & M;
            require((uint64_t)add_ripple_carry<T, W>(a, b) == ea, "add RCA");
            require((uint64_t)add_cla<T, W>(a, b) == ea, "add CLA");
            require((uint64_t)add_kogge_stone<T, W>(a, b) == ea, "add KS");
            require((uint64_t)mul_booth_radix2<T, W>(a, b) == em, "mul Booth2");
            require((uint64_t)mul_booth_radix4<T, W>(a, b) == em, "mul Booth4");
        }
        for (uint64_t y : divs) {
            T n = (T)(x & M), d = (T)y;
            uint64_t e = (uint64_t)n / (uint64_t)d;
            require((uint64_t)div_restoring<T, W>(n, d) == e, "div Restoring");
            require((uint64_t)div_non_restoring<T, W>(n, d) == e, "div NonRestoring");
            require((uint64_t)div_goldschmidt<T, W>(n, d) == e, "div Goldschmidt");
            require((uint64_t)div_newton_raphson<T, W>(n, d) == e, "div Newton-Raphson");
        }
    }
}

static void check_correctness() {
    check_width<uint8_t, 8>();
    check_width<uint16_t, 16>();
    check_width<uint32_t, 32>();
    check_width<uint64_t, 48>();
}

// ------------------------------------------------------------ HARNESS ----

static volatile uint64_t g_sink;

template <typename F>
static double bench(F f) {
    const int outer = 100, inner = 1000, trials = 15;
    double best = 1e300;
    for (int t = 0; t < trials; ++t) {
        auto t0 = std::chrono::high_resolution_clock::now();
        uint64_t acc = 0;
        for (int o = 0; o < outer; ++o)
            for (int j = 0; j < inner; ++j) acc += f(o * inner + j);
        g_sink = acc;
        auto t1 = std::chrono::high_resolution_clock::now();
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / (outer * inner);
        if (ns < best) best = ns;
    }
    return best;
}

template <typename T, int, T (*F)(T, T)>
static double cell_binop() {
    return bench([](int i) {
        T a = (T)mask48((uint64_t)i), b = (T)mask48((uint64_t)(i + 1));
        return (uint64_t)F(a, b);
    });
}

template <typename T, int, T (*F)(T, T)>
static double cell_div() {
    return bench([](int i) {
        T n = (T)mask48((uint64_t)(i + 100)), d = (T)(((i + 3) % 100) + 1);
        return (uint64_t)F(n, d);
    });
}

static void print_row(const char* name, const double* v) {
    std::printf("%-16s%-10.2f%-10.2f%-10.2f%-10.2f\n", name, v[0], v[1], v[2], v[3]);
}

#define ROW(NAME, CELL, FN)                                                        \
    do {                                                                           \
        double v_[4] = {CELL<uint8_t, 8, FN<uint8_t, 8>>(),                        \
                        CELL<uint16_t, 16, FN<uint16_t, 16>>(),                    \
                        CELL<uint32_t, 32, FN<uint32_t, 32>>(),                    \
                        CELL<uint64_t, 48, FN<uint64_t, 48>>()};                   \
        print_row(NAME, v_);                                                       \
    } while (0)

static const char* RULE_EQ = "=================================================================";
static const char* RULE_DASH = "-----------------------------------------------------------------";

int main() {
    check_correctness();
    (void)bench([](int i) { return (uint64_t)add_kogge_stone<uint32_t, 32>((uint32_t)i, 7u); });  // warm-up

    std::printf("%s\n", RULE_EQ);
    std::printf("   INTEGER ALU BENCHMARK  (ns/op, min of 15 trials, 100x1000 batch)\n");
    std::printf("%s\n", RULE_EQ);
    std::printf("[correctness checks passed]\n");
    std::printf("%-16s%-10s%-10s%-10s%-10s\n", "Algorithm", "8-bit", "16-bit", "32-bit", "48-bit");
    std::printf("%s\n", RULE_DASH);
    ROW("ADD (RCA)", cell_binop, add_ripple_carry);
    ROW("ADD (CLA)", cell_binop, add_cla);
    ROW("ADD (KS)", cell_binop, add_kogge_stone);
    std::printf("%s\n", RULE_DASH);
    ROW("MUL (Booth2)", cell_binop, mul_booth_radix2);
    ROW("MUL (Booth4)", cell_binop, mul_booth_radix4);
    std::printf("%s\n", RULE_DASH);
    ROW("DIV (Restor)", cell_div, div_restoring);
    ROW("DIV (NonRes)", cell_div, div_non_restoring);
    ROW("DIV (Goldsch)", cell_div, div_goldschmidt);
    ROW("DIV (NR)", cell_div, div_newton_raphson);
    std::printf("%s\n", RULE_DASH);
    return 0;
}
