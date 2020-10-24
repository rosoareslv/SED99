from __future__ import with_statement

import numpy as np

from numpy.testing import assert_almost_equal
from sklearn.datasets import make_regression
from sklearn.linear_model import LinearRegression

from nistats.first_level_model import run_glm
from nistats.contrasts import (_fixed_effect_contrast,
                               compute_contrast,
                               )



def test_Tcontrast():
    # new API
    n, p, q = 100, 80, 10
    X, Y = np.random.randn(p, q), np.random.randn(p, n)
    labels, results = run_glm(Y, X, 'ar1')
    con_val = np.eye(q)[0]
    z_vals = compute_contrast(labels, results, con_val).z_score()
    assert_almost_equal(z_vals.mean(), 0, 0)
    assert_almost_equal(z_vals.std(), 1, 0)


def test_Fcontrast():
    # new API
    n, p, q = 100, 80, 10
    X, Y = np.random.randn(p, q), np.random.randn(p, n)
    for model in ['ols', 'ar1']:
        labels, results = run_glm(Y, X, model)
        for con_val in [np.eye(q)[0], np.eye(q)[:3]]:
            z_vals = compute_contrast(
                labels, results, con_val, contrast_type='F').z_score()
            assert_almost_equal(z_vals.mean(), 0, 0)
            assert_almost_equal(z_vals.std(), 1, 0)


def test_t_contrast_add():
    # new API
    n, p, q = 100, 80, 10
    X, Y = np.random.randn(p, q), np.random.randn(p, n)
    lab, res = run_glm(Y, X, 'ols')
    c1, c2 = np.eye(q)[0], np.eye(q)[1]
    con = compute_contrast(lab, res, c1) + compute_contrast(lab, res, c2)
    z_vals = con.z_score()
    assert_almost_equal(z_vals.mean(), 0, 0)
    assert_almost_equal(z_vals.std(), 1, 0)


def test_fixed_effect_contrast():
    n, p, q = 100, 80, 10
    X, Y = np.random.randn(p, q), np.random.randn(p, n)
    lab, res = run_glm(Y, X, 'ols')
    c1, c2 = np.eye(q)[0], np.eye(q)[1]
    con = _fixed_effect_contrast([lab, lab], [res, res], [c1, c2])
    z_vals = con.z_score()
    assert_almost_equal(z_vals.mean(), 0, 0)
    assert_almost_equal(z_vals.std(), 1, 0)


def test_fixed_effect_contrast_nonzero_effect():
    X, y = make_regression(n_features=5, n_samples=20, random_state=0)
    y = y[:, None]
    labels, results = run_glm(y, X, 'ols')
    coef = LinearRegression(fit_intercept=False).fit(X, y).coef_
    for i in range(X.shape[1]):
        contrast = np.zeros(X.shape[1])
        contrast[i] = 1.
        fixed_effect = _fixed_effect_contrast([labels], [results], [contrast])
        assert_almost_equal(fixed_effect.effect_size(), coef.ravel()[i])
        fixed_effect = _fixed_effect_contrast(
            [labels] * 3, [results] * 3, [contrast] * 3)
        assert_almost_equal(fixed_effect.effect_size(), coef.ravel()[i])


def test_F_contrast_add():
    # new API
    n, p, q = 100, 80, 10
    X, Y = np.random.randn(p, q), np.random.randn(p, n)
    lab, res = run_glm(Y, X, 'ar1')
    c1, c2 = np.eye(q)[:2], np.eye(q)[2:4]
    con = compute_contrast(lab, res, c1) + compute_contrast(lab, res, c2)
    z_vals = con.z_score()
    assert_almost_equal(z_vals.mean(), 0, 0)
    assert_almost_equal(z_vals.std(), 1, 0)

    # first test with dependent contrast
    con1 = compute_contrast(lab, res, c1)
    con2 = compute_contrast(lab, res, c1) + compute_contrast(lab, res, c1)
    assert_almost_equal(con1.effect * 2, con2.effect)
    assert_almost_equal(con1.variance * 2, con2.variance)
    assert_almost_equal(con1.stat() * 2, con2.stat())


def test_contrast_mul():
    # new API
    n, p, q = 100, 80, 10
    X, Y = np.random.randn(p, q), np.random.randn(p, n)
    lab, res = run_glm(Y, X, 'ar1')
    for c1 in [np.eye(q)[0], np.eye(q)[:3]]:
        con1 = compute_contrast(lab, res, c1)
        con2 = con1 * 2
        assert_almost_equal(con1.effect * 2, con2.effect)
        # assert_almost_equal(con1.variance * 2, con2.variance) FIXME
        # assert_almost_equal(con1.stat() * 2, con2.stat()) FIXME
        assert_almost_equal(con1.z_score(), con2.z_score())


def test_contrast_values():
    # new API
    # but this test is circular and should be removed
    n, p, q = 100, 80, 10
    X, Y = np.random.randn(p, q), np.random.randn(p, n)
    lab, res = run_glm(Y, X, 'ar1', bins=1)
    # t test
    cval = np.eye(q)[0]
    con = compute_contrast(lab, res, cval)
    t_ref = list(res.values())[0].Tcontrast(cval).t
    assert_almost_equal(np.ravel(con.stat()), t_ref)
    # F test
    cval = np.eye(q)[:3]
    con = compute_contrast(lab, res, cval)
    F_ref = list(res.values())[0].Fcontrast(cval).F
    # Note that the values are not strictly equal,
    # this seems to be related to a bug in Mahalanobis
    assert_almost_equal(np.ravel(con.stat()), F_ref, 3)
