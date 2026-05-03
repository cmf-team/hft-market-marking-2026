#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backtest/orders/limit_order.hpp"
#include "backtest/strategy/strategy.hpp"
#include "data_ingestion/data_types.hpp"

// ── Matrix helpers (nm ≤ 50, so plain dense row-major is fine) ────────────────

namespace mp_detail {

using Matrix = std::vector<double>;  // nm×nm row-major

inline Matrix mat_zeros(int n) { return Matrix(n * n, 0.0); }

inline Matrix mat_identity(int n) {
    Matrix m(n * n, 0.0);
    for (int i = 0; i < n; ++i) m[i * n + i] = 1.0;
    return m;
}

inline double mat_get(const Matrix& M, int n, int r, int c) { return M[r * n + c]; }
inline void   mat_set(Matrix& M, int n, int r, int c, double v) { M[r * n + c] = v; }

// Returns (I - A)
inline Matrix mat_sub_identity(const Matrix& A, int n) {
    Matrix R = mat_identity(n);
    for (int i = 0; i < n * n; ++i) R[i] -= A[i];
    return R;
}

// Square matrix multiplication C = A * B
inline Matrix mat_mul(const Matrix& A, const Matrix& B, int n) {
    Matrix C(n * n, 0.0);
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k) {
            double aik = A[i * n + k];
            if (aik == 0.0) continue;
            for (int j = 0; j < n; ++j)
                C[i * n + j] += aik * B[k * n + j];
        }
    return C;
}

// Matrix × column vector
inline std::vector<double> mat_vec_mul(const Matrix& A, const std::vector<double>& v, int n) {
    std::vector<double> out(n, 0.0);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            out[i] += A[i * n + j] * v[j];
    return out;
}

// Rectangular matrix (nrows × ncols) × column vector (ncols)
inline std::vector<double> rect_vec_mul(const std::vector<double>& R,
                                        int nrows, int ncols,
                                        const std::vector<double>& v) {
    std::vector<double> out(nrows, 0.0);
    for (int i = 0; i < nrows; ++i)
        for (int j = 0; j < ncols; ++j)
            out[i] += R[i * ncols + j] * v[j];
    return out;
}

// Gauss-Jordan inversion with partial pivoting.
// Returns empty vector if A is singular (pivot < 1e-14).
inline Matrix mat_inv(Matrix A, int n) {
    Matrix R = mat_identity(n);
    for (int col = 0; col < n; ++col) {
        // find pivot row
        int pivot = col;
        for (int row = col + 1; row < n; ++row)
            if (std::abs(A[row * n + col]) > std::abs(A[pivot * n + col])) pivot = row;

        if (std::abs(A[pivot * n + col]) < 1e-14) return {};  // singular

        // swap rows
        for (int k = 0; k < n; ++k) {
            std::swap(A[col * n + k],  A[pivot * n + k]);
            std::swap(R[col * n + k],  R[pivot * n + k]);
        }

        double inv_diag = 1.0 / A[col * n + col];
        // eliminate all other rows
        for (int row = 0; row < n; ++row) {
            if (row == col) continue;
            double factor = A[row * n + col] * inv_diag;
            if (factor == 0.0) continue;
            for (int k = 0; k < n; ++k) {
                A[row * n + k] -= factor * A[col * n + k];
                R[row * n + k] -= factor * R[col * n + k];
            }
        }
        // normalize pivot row
        for (int k = 0; k < n; ++k) {
            A[col * n + k] *= inv_diag;
            R[col * n + k] *= inv_diag;
        }
    }
    return R;
}

}  // namespace mp_detail

// ── Strategy ──────────────────────────────────────────────────────────────────

struct MicroPriceParams {
    int    calib_snapshots   = 5000;  // LOB snapshots used for calibration
    int    n_imbal_buckets   = 10;    // n: imbalance discretization buckets
    int    n_spread_levels   = 5;     // m: spread levels (1..m ticks, clamped)
    double order_qty         = 1.0;   // order size in native units
    double quote_half_spread = 0.0;   // 0 → auto (1 tick after calibration)
    double max_inventory     = 10.0;  // halt quoting if |inventory| exceeds this
    int    quote_every       = 1;     // requote every N snapshots
    bool   verbose           = false;
};

class MicroPriceStrategy final : public strategy::IStrategy {
public:
    explicit MicroPriceStrategy(MicroPriceParams params = {}) : params_(params) {
        assert(params_.n_imbal_buckets > 0);
        assert(params_.n_spread_levels > 0);
        assert(params_.calib_snapshots > 0);
    }

    void set_order_submitter(strategy::SubmitOrderFn fn) override { submit_ = std::move(fn); }
    void set_order_canceller(strategy::CancelOrderFn fn) override { cancel_ = std::move(fn); }
    void on_trade(const data::Trade&) override {}

    void on_order_book(const data::OrderBookSnapshot& book) override {
        if (book.bids[0].price == 0 || book.asks[0].price == 0) return;

        const double bid_d  = book.bids[0].price * 1e-9;
        const double ask_d  = book.asks[0].price * 1e-9;
        if (ask_d <= bid_d) return;

        const double mid    = (bid_d + ask_d) * 0.5;
        const double spread = ask_d - bid_d;
        const double bid_q  = book.bids[0].amount * 1e-9;
        const double ask_q  = book.asks[0].amount * 1e-9;
        const double total_q = bid_q + ask_q;
        const double imbal   = (total_q > 0.0) ? bid_q / total_q : 0.5;

        const uint64_t spread_sc = book.asks[0].price - book.bids[0].price;
        // Track running tick estimate (min non-zero spread seen so far)
        if (spread_sc > 0) {
            if (tick_sc_running_ == 0 || spread_sc < tick_sc_running_) {
                tick_sc_running_ = spread_sc;
                tick_running_    = tick_sc_running_ * 1e-9;
                half_tick_running_ = tick_running_ * 0.5;
            }
            observed_spreads_sc_.push_back(spread_sc);
        }

        const int state = compute_state(imbal, spread);

        if (calibrating_) {
            if (prev_state_ >= 0) {
                const double dm = mid - prev_mid_;
                transitions_.push_back({prev_state_, state, dm});
                // Symmetrize: flip imbalance bucket, negate dm
                transitions_.push_back({mirror_state(prev_state_), mirror_state(state), -dm});
            }
            prev_state_ = state;
            prev_mid_   = mid;
            last_mid_   = mid;
            last_ts_    = book.local_timestamp;

            if (++snap_count_ >= params_.calib_snapshots) {
                calibrating_ = false;
                build_model();
            }
            return;
        }

        last_mid_ = mid;
        last_ts_  = book.local_timestamp;

        if (!calibrated_) return;

        ++lob_count_;
        if (lob_count_ % params_.quote_every != 0) return;

        const double adj         = G_star_[state];
        const double micro_price = mid + adj;

        if (params_.verbose) {
            std::printf("[snap=%d] mid=%.8f imbal=%.4f spread=%.0ftk adj=%.2e mp=%.8f q=%.2f\n",
                        lob_count_, mid, imbal,
                        tick_running_ > 1e-15 ? spread / tick_running_ : 0.0,
                        adj, micro_price, inventory_);
        }

        cancel_active_quotes();

        if (std::abs(inventory_) < params_.max_inventory) {
            const double bp = micro_price - effective_half_spread_;
            const double ap = micro_price + effective_half_spread_;
            if (bp > 0.0) submit_quote(backtest::Side::Buy,  bp, book.local_timestamp);
            if (ap > 0.0) submit_quote(backtest::Side::Sell, ap, book.local_timestamp);
        }
    }

    void on_order_update(const backtest::Order& order) override {
        using backtest::OrderStatus;

        if (order.status() == OrderStatus::Cancelled) {
            if (order.id() == active_bid_id_) active_bid_id_ = 0;
            if (order.id() == active_ask_id_) active_ask_id_ = 0;
            return;
        }
        if (order.status() != OrderStatus::Filled &&
            order.status() != OrderStatus::PartiallyFilled) return;

        const uint64_t prev     = fill_seen_[order.id()];
        const uint64_t delta_sc = order.filled_qty() - prev;
        fill_seen_[order.id()]  = order.filled_qty();

        const auto&  lo         = static_cast<const backtest::LimitOrder&>(order);
        const double fill_price = lo.price() * 1e-9;
        const double fill_qty   = delta_sc   * 1e-9;

        if (order.side() == backtest::Side::Buy) {
            inventory_ += fill_qty;
            cash_      -= fill_price * fill_qty;
        } else {
            inventory_ -= fill_qty;
            cash_      += fill_price * fill_qty;
        }

        ++trade_count_;
        pnl_series_.push_back(cash_ + inventory_ * last_mid_);

        if (order.status() == OrderStatus::Filled) {
            if (order.id() == active_bid_id_) active_bid_id_ = 0;
            if (order.id() == active_ask_id_) active_ask_id_ = 0;
        }
    }

    strategy::Analytics calculate_analytics() const override {
        strategy::Analytics a;
        a.pnl    = cash_ + inventory_ * last_mid_;
        a.trades = trade_count_;

        if (pnl_series_.size() < 2) return a;

        std::vector<double> returns(pnl_series_.size() - 1);
        for (size_t i = 1; i < pnl_series_.size(); ++i)
            returns[i - 1] = pnl_series_[i] - pnl_series_[i - 1];

        double mean = 0.0;
        for (double r : returns) mean += r;
        mean /= static_cast<double>(returns.size());

        double var = 0.0;
        for (double r : returns) var += (r - mean) * (r - mean);
        var /= static_cast<double>(returns.size());

        a.sharpe = (var > 0.0) ? mean / std::sqrt(var) : 0.0;

        uint64_t wins = 0;
        for (double r : returns) wins += (r > 0.0);
        a.wins     = wins;
        a.win_rate = static_cast<double>(wins) / static_cast<double>(returns.size());

        return a;
    }

private:
    struct Transition { int prev; int next; double dm_native; };

    MicroPriceParams params_;
    strategy::SubmitOrderFn submit_;
    strategy::CancelOrderFn cancel_;

    // ── Calibration ──────────────────────────────────────────────────────────
    bool calibrating_ = true;
    bool calibrated_  = false;
    int  snap_count_  = 0;
    int  prev_state_  = -1;
    double prev_mid_  = 0.0;

    std::vector<Transition> transitions_;
    std::vector<uint64_t>   observed_spreads_sc_;

    // Running tick estimate (updated online, finalized in build_model)
    uint64_t tick_sc_running_  = 0;
    double   tick_running_     = 0.0;
    double   half_tick_running_ = 0.0;

    // Finalized tick (set in build_model)
    uint64_t tick_sc_   = 0;
    double   tick_      = 0.0;
    double   half_tick_ = 0.0;

    // K-set
    std::vector<int>    K_halfticks_;
    std::vector<double> K_vals_;

    // Model output
    std::vector<double> G_star_;
    double effective_half_spread_ = 0.0;

    // ── Trading ──────────────────────────────────────────────────────────────
    int      lob_count_      = 0;
    double   last_mid_       = 0.0;
    uint64_t last_ts_        = 0;
    uint64_t next_id_        = 1;
    uint64_t active_bid_id_  = 0;
    uint64_t active_ask_id_  = 0;
    double   inventory_      = 0.0;
    double   cash_           = 0.0;
    uint64_t trade_count_    = 0;
    std::vector<double>                   pnl_series_;
    std::unordered_map<uint64_t,uint64_t> fill_seen_;

    // ── Helpers ──────────────────────────────────────────────────────────────

    int compute_state(double imbal, double spread_native) const {
        int i = static_cast<int>(imbal * params_.n_imbal_buckets);
        i = std::clamp(i, 0, params_.n_imbal_buckets - 1);

        int s = 1;
        if (tick_running_ > 1e-15)
            s = static_cast<int>(std::round(spread_native / tick_running_));
        s = std::clamp(s, 1, params_.n_spread_levels);

        return i * params_.n_spread_levels + (s - 1);
    }

    int mirror_state(int state) const {
        const int i     = state / params_.n_spread_levels;
        const int s_idx = state % params_.n_spread_levels;
        return (params_.n_imbal_buckets - 1 - i) * params_.n_spread_levels + s_idx;
    }

    void build_model() {
        using namespace mp_detail;

        // 1. Finalize tick
        if (observed_spreads_sc_.empty()) { calibrated_ = false; return; }
        tick_sc_   = *std::min_element(observed_spreads_sc_.begin(), observed_spreads_sc_.end());
        tick_      = tick_sc_ * 1e-9;
        half_tick_ = tick_ * 0.5;

        // 2. Convert dm_native → dm_halfticks for all transitions
        std::vector<int> dm_ht(transitions_.size(), 0);
        for (size_t i = 0; i < transitions_.size(); ++i) {
            if (half_tick_ > 1e-20)
                dm_ht[i] = static_cast<int>(std::round(transitions_[i].dm_native / half_tick_));
        }

        // 3. Build K-set (unique non-zero half-tick integers, |k| ≤ 20)
        std::unordered_set<int> k_set;
        for (int k : dm_ht) if (k != 0 && std::abs(k) <= 20) k_set.insert(k);
        K_halfticks_.assign(k_set.begin(), k_set.end());
        std::sort(K_halfticks_.begin(), K_halfticks_.end());
        K_vals_.resize(K_halfticks_.size());
        for (size_t j = 0; j < K_halfticks_.size(); ++j)
            K_vals_[j] = K_halfticks_[j] * half_tick_;

        const int nm = params_.n_imbal_buckets * params_.n_spread_levels;
        const int nK = static_cast<int>(K_halfticks_.size());

        if (nK == 0) {
            std::fprintf(stderr, "[MicroPrice] WARNING: no mid-price changes observed in calibration data\n");
            calibrated_ = false;
            return;
        }

        // 4. Count transitions
        Matrix Q_cnt  = mat_zeros(nm);
        Matrix T_cnt  = mat_zeros(nm);
        std::vector<double> R_cnt(nm * nK, 0.0);
        std::vector<double> row_total(nm, 0.0);

        // Build K lookup: halftick integer → index
        std::unordered_map<int,int> k_idx_map;
        for (int j = 0; j < nK; ++j) k_idx_map[K_halfticks_[j]] = j;

        for (size_t i = 0; i < transitions_.size(); ++i) {
            const int x   = transitions_[i].prev;
            const int y   = transitions_[i].next;
            const int dht = dm_ht[i];
            row_total[x] += 1.0;
            if (dht == 0) {
                mat_set(Q_cnt, nm, x, y, mat_get(Q_cnt, nm, x, y) + 1.0);
            } else {
                mat_set(T_cnt, nm, x, y, mat_get(T_cnt, nm, x, y) + 1.0);
                auto it = k_idx_map.find(dht);
                if (it != k_idx_map.end())
                    R_cnt[x * nK + it->second] += 1.0;
            }
        }

        // 5. Normalize rows
        Matrix Q(nm * nm, 0.0), T(nm * nm, 0.0);
        std::vector<double> R(nm * nK, 0.0);

        for (int x = 0; x < nm; ++x) {
            if (row_total[x] > 0.0) {
                const double inv = 1.0 / row_total[x];
                for (int y = 0; y < nm; ++y) {
                    mat_set(Q, nm, x, y, mat_get(Q_cnt, nm, x, y) * inv);
                    mat_set(T, nm, x, y, mat_get(T_cnt, nm, x, y) * inv);
                }
                for (int k = 0; k < nK; ++k)
                    R[x * nK + k] = R_cnt[x * nK + k] * inv;
            }
            // Unvisited states: leave Q[x,:]=0 so (I-Q) row = e_x, giving G*[x]=0
        }

        // 6. G1 = (I−Q)^{-1} · R · K_vals
        const Matrix IminusQ   = mat_sub_identity(Q, nm);
        const Matrix invIQ     = mat_inv(IminusQ, nm);
        if (invIQ.empty()) {
            std::fprintf(stderr, "[MicroPrice] WARNING: (I-Q) is singular, strategy inactive\n");
            calibrated_ = false;
            return;
        }
        // RK_vec[x] = sum_k R[x,k] * K_vals[k]
        std::vector<double> RK_vec(nm, 0.0);
        for (int x = 0; x < nm; ++x)
            for (int k = 0; k < nK; ++k)
                RK_vec[x] += R[x * nK + k] * K_vals_[k];

        const std::vector<double> G1 = mat_vec_mul(invIQ, RK_vec, nm);

        // 7. B = (I−Q)^{-1} · T
        const Matrix B = mat_mul(invIQ, T, nm);

        // 8. G* = sum_{k=0}^∞ B^k G1  (power series, converges because B*·G1 = 0
        //    after symmetrization; B is stochastic so (I-B)^{-1} doesn't exist)
        G_star_ = G1;
        std::vector<double> Bk_G1 = G1;
        constexpr int    MAX_ITER = 2000;
        constexpr double TOL      = 1e-14;
        for (int iter = 0; iter < MAX_ITER; ++iter) {
            Bk_G1 = mat_vec_mul(B, Bk_G1, nm);
            double max_abs = 0.0;
            for (int x = 0; x < nm; ++x) {
                G_star_[x] += Bk_G1[x];
                max_abs = std::max(max_abs, std::abs(Bk_G1[x]));
            }
            if (max_abs < TOL) break;
        }

        // 9. Set effective half-spread
        effective_half_spread_ = (params_.quote_half_spread > 0.0)
                                     ? params_.quote_half_spread
                                     : tick_;

        calibrated_ = true;

        if (params_.verbose) {
            const auto [gmin, gmax] = std::minmax_element(G_star_.begin(), G_star_.end());
            int visited = 0;
            for (int x = 0; x < nm; ++x) if (row_total[x] > 0.0) ++visited;
            std::printf("[MicroPrice] Calibration complete.\n"
                        "  Tick:          %.3e native\n"
                        "  Half-tick:     %.3e native\n"
                        "  K-set (halfticks): %d entries, range [%d..%d]\n"
                        "  States:        %d/%d visited\n"
                        "  G* range:      [%.3e, %.3e]\n"
                        "  Quote half-spread: %.3e\n",
                        tick_, half_tick_,
                        nK, K_halfticks_.front(), K_halfticks_.back(),
                        visited, nm, *gmin, *gmax,
                        effective_half_spread_);
        }
    }

    void cancel_active_quotes() {
        if (active_bid_id_ != 0) { cancel_(active_bid_id_); active_bid_id_ = 0; }
        if (active_ask_id_ != 0) { cancel_(active_ask_id_); active_ask_id_ = 0; }
    }

    void submit_quote(backtest::Side side, double price, uint64_t ts) {
        const uint64_t price_sc = static_cast<uint64_t>(std::llround(price * 1e9));
        const uint64_t qty_sc   = static_cast<uint64_t>(std::llround(params_.order_qty * 1e9));
        if (price_sc == 0) return;
        const uint64_t id = next_id_++;
        submit_(std::make_unique<backtest::LimitOrder>(id, ts, side, qty_sc, price_sc));
        if (side == backtest::Side::Buy)  active_bid_id_ = id;
        else                              active_ask_id_ = id;
    }
};
