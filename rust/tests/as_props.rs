//! Property tests for Avellaneda–Stoikov (2008) closed-form invariants.
//!
//! For r = s − q·γ·σ²·τ and δ* = ½γσ²τ + (1/γ)ln(1+γ/k):
//!
//!   1. Flat inventory ⇒ reservation == mid.
//!   2. Long ⇒ reservation < mid; short ⇒ reservation > mid.
//!   3. δ* > 0 strictly (γ>0, k>0, σ≥0, τ≥0).
//!   4. δ* is non-decreasing in τ.
//!   5. δ* is non-decreasing in σ².
//!   6. |r − mid| is non-decreasing in γ for fixed q≠0, σ>0, τ>0.
//!   7. |r − mid| is non-decreasing in σ² for fixed q≠0, γ>0, τ>0.
//!   8. δ* at τ=0 equals (1/γ)·ln(1+γ/k) (independent of σ).
//!   9. bid_f ≤ ask_f always (since δ* ≥ 0).

use hft_mm_backtester::strategy::avellaneda_stoikov::{AsParams, AvellanedaStoikov};
use proptest::prelude::*;

fn build(sigma: f64, k: f64, gamma: f64) -> AvellanedaStoikov {
    AvellanedaStoikov::new(AsParams {
        sigma,
        k,
        a: 1.0,
        gamma,
        quote_qty: 1,
        max_abs_inventory: 1_000,
    })
}

proptest! {
    #[test]
    fn flat_inventory_reservation_eq_mid(
        mid in 1.0f64..1e6,
        sigma in 0.0f64..10.0,
        k in 0.01f64..100.0,
        gamma in 0.001f64..5.0,
        tau in 0.0f64..1.0,
    ) {
        let s = build(sigma, k, gamma);
        let (r, _) = s.compute(mid, 0, tau);
        prop_assert!((r - mid).abs() <= 1e-9 * mid.max(1.0));
    }

    #[test]
    fn skew_direction_matches_inventory_sign(
        mid in 1.0f64..1e6,
        sigma in 0.01f64..10.0,
        k in 0.01f64..100.0,
        gamma in 0.001f64..5.0,
        tau in 0.001f64..1.0,
        q in -100i64..=100,
    ) {
        prop_assume!(q != 0);
        let s = build(sigma, k, gamma);
        let (r, _) = s.compute(mid, q, tau);
        if q > 0 {
            prop_assert!(r < mid, "long ⇒ r < mid (got r={r}, mid={mid})");
        } else {
            prop_assert!(r > mid, "short ⇒ r > mid (got r={r}, mid={mid})");
        }
    }

    #[test]
    fn half_spread_strictly_positive(
        sigma in 0.0f64..10.0,
        k in 0.01f64..100.0,
        gamma in 0.001f64..5.0,
        tau in 0.0f64..1.0,
    ) {
        let s = build(sigma, k, gamma);
        let (_, half) = s.compute(100.0, 0, tau);
        prop_assert!(half > 0.0, "δ* should be > 0; got {half}");
    }

    #[test]
    fn half_spread_monotone_in_tau(
        sigma in 0.01f64..10.0,
        k in 0.01f64..100.0,
        gamma in 0.001f64..5.0,
        tau_lo in 0.0f64..0.5,
        tau_hi in 0.5f64..1.0,
    ) {
        prop_assume!(tau_lo < tau_hi);
        let s = build(sigma, k, gamma);
        let (_, h_lo) = s.compute(100.0, 0, tau_lo);
        let (_, h_hi) = s.compute(100.0, 0, tau_hi);
        prop_assert!(h_hi >= h_lo - 1e-12, "δ* should be non-decreasing in τ");
    }

    #[test]
    fn half_spread_monotone_in_sigma(
        sigma_lo in 0.0f64..1.0,
        delta in 0.001f64..5.0,
        k in 0.01f64..100.0,
        gamma in 0.001f64..5.0,
        tau in 0.001f64..1.0,
    ) {
        let sigma_hi = sigma_lo + delta;
        let s_lo = build(sigma_lo, k, gamma);
        let s_hi = build(sigma_hi, k, gamma);
        let (_, h_lo) = s_lo.compute(100.0, 0, tau);
        let (_, h_hi) = s_hi.compute(100.0, 0, tau);
        prop_assert!(h_hi >= h_lo - 1e-12, "δ* should be non-decreasing in σ");
    }

    #[test]
    fn skew_magnitude_monotone_in_gamma(
        sigma in 0.01f64..10.0,
        k in 0.01f64..100.0,
        gamma_lo in 0.001f64..0.5,
        delta in 0.001f64..2.0,
        tau in 0.001f64..1.0,
        q in 1i64..100,
    ) {
        let gamma_hi = gamma_lo + delta;
        let s_lo = build(sigma, k, gamma_lo);
        let s_hi = build(sigma, k, gamma_hi);
        let (r_lo, _) = s_lo.compute(100.0, q, tau);
        let (r_hi, _) = s_hi.compute(100.0, q, tau);
        let skew_lo = (100.0 - r_lo).abs();
        let skew_hi = (100.0 - r_hi).abs();
        prop_assert!(skew_hi >= skew_lo - 1e-12,
            "|r − mid| should be non-decreasing in γ; lo={skew_lo}, hi={skew_hi}");
    }

    #[test]
    fn skew_magnitude_monotone_in_sigma(
        sigma_lo in 0.01f64..1.0,
        delta in 0.001f64..5.0,
        k in 0.01f64..100.0,
        gamma in 0.001f64..5.0,
        tau in 0.001f64..1.0,
        q in 1i64..100,
    ) {
        let sigma_hi = sigma_lo + delta;
        let s_lo = build(sigma_lo, k, gamma);
        let s_hi = build(sigma_hi, k, gamma);
        let (r_lo, _) = s_lo.compute(100.0, q, tau);
        let (r_hi, _) = s_hi.compute(100.0, q, tau);
        let skew_lo = (100.0 - r_lo).abs();
        let skew_hi = (100.0 - r_hi).abs();
        prop_assert!(skew_hi >= skew_lo - 1e-12,
            "|r − mid| should be non-decreasing in σ");
    }

    #[test]
    fn half_spread_at_tau_zero_independent_of_sigma(
        sigma_a in 0.0f64..10.0,
        sigma_b in 0.0f64..10.0,
        k in 0.01f64..100.0,
        gamma in 0.001f64..5.0,
    ) {
        let s_a = build(sigma_a, k, gamma);
        let s_b = build(sigma_b, k, gamma);
        let (_, h_a) = s_a.compute(100.0, 0, 0.0);
        let (_, h_b) = s_b.compute(100.0, 0, 0.0);
        let expected = (1.0 / gamma) * (1.0 + gamma / k).ln();
        prop_assert!((h_a - expected).abs() < 1e-9);
        prop_assert!((h_b - expected).abs() < 1e-9);
    }

    #[test]
    fn bid_le_ask(
        sigma in 0.0f64..10.0,
        k in 0.01f64..100.0,
        gamma in 0.001f64..5.0,
        tau in 0.0f64..1.0,
        q in -50i64..=50,
    ) {
        let s = build(sigma, k, gamma);
        let (r, half) = s.compute(100.0, q, tau);
        let bid = r - half;
        let ask = r + half;
        prop_assert!(bid <= ask);
    }
}
