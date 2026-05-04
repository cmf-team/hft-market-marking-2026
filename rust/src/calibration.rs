pub fn sigma_from_log_returns(prices: &[f64]) -> f64 {
    if prices.len() < 2 {
        return 0.0;
    }

    let rets: Vec<f64> = prices
        .windows(2)
        .filter(|w| w[0] > 0.0 && w[1] > 0.0)
        .map(|w| (w[1] / w[0]).ln())
        .collect();

    if rets.is_empty() {
        return 0.0;
    }

    let mean = rets.iter().sum::<f64>() / rets.len() as f64;
    let var = rets.iter().map(|r| (r - mean).powi(2)).sum::<f64>() / rets.len() as f64;
    var.sqrt()
}

#[derive(Debug, Clone, PartialEq)]
pub struct AkFit {
    pub a: f64,
    pub k: f64,
    pub r_squared: f64,
    pub n: usize,
}

/// Fit λ(δ) = A·exp(−k·δ) by OLS on (δ, ln λ).
/// Drops samples with non-positive λ.  Returns None if <2 valid points or zero slope variance.
pub fn fit_a_k(deltas: &[f64], lambdas: &[f64]) -> Option<AkFit> {
    assert_eq!(deltas.len(), lambdas.len());

    let pairs: Vec<(f64, f64)> = deltas
        .iter()
        .zip(lambdas.iter())
        .filter(|(_, l)| **l > 0.0)
        .map(|(d, l)| (*d, l.ln()))
        .collect();

    if pairs.len() < 2 {
        return None;
    }

    let n = pairs.len() as f64;
    let mx = pairs.iter().map(|(x, _)| *x).sum::<f64>() / n;
    let my = pairs.iter().map(|(_, y)| *y).sum::<f64>() / n;
    let sxx = pairs.iter().map(|(x, _)| (*x - mx).powi(2)).sum::<f64>();
    let sxy = pairs
        .iter()
        .map(|(x, y)| (*x - mx) * (*y - my))
        .sum::<f64>();

    if sxx == 0.0 {
        return None;
    }

    let slope = sxy / sxx;
    let intercept = my - slope * mx;
    let syy = pairs.iter().map(|(_, y)| (*y - my).powi(2)).sum::<f64>();

    let r2 = if syy == 0.0 {
        1.0
    } else {
        1.0 - {
            let resid: f64 = pairs
                .iter()
                .map(|(x, y)| {
                    let p = intercept + slope * x;
                    (y - p).powi(2)
                })
                .sum();
            resid / syy
        }
    };

    Some(AkFit {
        a: intercept.exp(),
        k: -slope,
        r_squared: r2,
        n: pairs.len(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sigma_zero_when_constant() {
        assert_eq!(sigma_from_log_returns(&[100.0; 10]), 0.0);
    }
    #[test]
    fn sigma_positive_when_varying() {
        let p = vec![100.0, 101.0, 100.0, 102.0, 99.0];
        assert!(sigma_from_log_returns(&p) > 0.0);
    }
    #[test]
    fn sigma_handles_tiny_input() {
        assert_eq!(sigma_from_log_returns(&[]), 0.0);
        assert_eq!(sigma_from_log_returns(&[100.0]), 0.0);
    }

    #[test]
    fn fit_recovers_known_a_k() {
        // λ(δ) = 5 · exp(-2δ)
        let a = 5.0;
        let k = 2.0;
        let deltas: Vec<f64> = (1..=20).map(|i| i as f64 * 0.1).collect();
        let lambdas: Vec<f64> = deltas.iter().map(|d| a * (-k * d).exp()).collect();
        let fit = fit_a_k(&deltas, &lambdas).unwrap();
        assert!((fit.a - 5.0).abs() < 1e-9);
        assert!((fit.k - 2.0).abs() < 1e-9);
        assert!(fit.r_squared > 0.999);
    }

    #[test]
    fn fit_drops_nonpositive_lambdas() {
        let deltas = vec![0.1, 0.2, 0.3];
        let lambdas = vec![0.0, 1.0, 2.0]; // first dropped
        let fit = fit_a_k(&deltas, &lambdas).unwrap();
        assert_eq!(fit.n, 2);
    }

    #[test]
    fn fit_none_when_insufficient_data() {
        assert!(fit_a_k(&[0.1], &[1.0]).is_none());
        assert!(fit_a_k(&[0.1, 0.2], &[0.0, 0.0]).is_none());
    }
}
