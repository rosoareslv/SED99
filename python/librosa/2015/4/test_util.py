#!/usr/bin/env python
# CREATED:2014-01-18 14:09:05 by Brian McFee <brm2132@columbia.edu>
# unit tests for util routines

# Disable cache
import os
try:
    os.environ.pop('LIBROSA_CACHE_DIR')
except:
    pass

import matplotlib
matplotlib.use('Agg')
import numpy as np
np.set_printoptions(precision=3)
from nose.tools import raises
import six
import warnings

import librosa


def test_example_audio_file():

    assert os.path.exists(librosa.util.example_audio_file())


def test_frame():

    # Generate a random time series
    def __test(P):
        frame, hop = P

        y = np.random.randn(8000)
        y_frame = librosa.util.frame(y, frame_length=frame, hop_length=hop)

        for i in range(y_frame.shape[1]):
            assert np.allclose(y_frame[:, i], y[i * hop:(i * hop + frame)])

    for frame in [256, 1024, 2048]:
        for hop_length in [64, 256, 512]:
            yield (__test, [frame, hop_length])


def test_pad_center():

    def __test(y, n, axis, mode):

        y_out = librosa.util.pad_center(y, n, axis=axis, mode=mode)

        n_len = y.shape[axis]
        n_pad = int((n - n_len) / 2)

        eq_slice = [slice(None)] * y.ndim
        eq_slice[axis] = slice(n_pad, n_pad + n_len)

        assert np.allclose(y, y_out[eq_slice])

    @raises(librosa.ParameterError)
    def __test_fail(y, n, axis, mode):
        librosa.util.pad_center(y, n, axis=axis, mode=mode)

    for shape in [(16,), (16, 16)]:
        y = np.ones(shape)

        for axis in [0, -1]:
            for mode in ['constant', 'edge', 'reflect']:
                for n in [0, 10]:
                    yield __test, y, n + y.shape[axis], axis, mode

                for n in [0, 10]:
                    yield __test_fail, y, n, axis, mode


def test_fix_length():

    def __test(y, n, axis):

        y_out = librosa.util.fix_length(y, n, axis=axis)

        eq_slice = [slice(None)] * y.ndim
        eq_slice[axis] = slice(y.shape[axis])

        if n > y.shape[axis]:
            assert np.allclose(y, y_out[eq_slice])
        else:
            assert np.allclose(y[eq_slice], y)

    for shape in [(16,), (16, 16)]:
        y = np.ones(shape)

        for axis in [0, -1]:
            for n in [-5, 0, 5]:
                yield __test, y, n + y.shape[axis], axis


def test_fix_frames():

    @raises(librosa.ParameterError)
    def __test_fail(frames, x_min, x_max, pad):
        librosa.util.fix_frames(frames, x_min, x_max, pad)

    def __test_pass(frames, x_min, x_max, pad):

        f_fix = librosa.util.fix_frames(frames,
                                        x_min=x_min,
                                        x_max=x_max,
                                        pad=pad)

        if x_min is not None:
            if pad:
                assert f_fix[0] == x_min
            assert np.all(f_fix >= x_min)

        if x_max is not None:
            if pad:
                assert f_fix[-1] == x_max
            assert np.all(f_fix <= x_max)

    for low in [-20, 0, 20]:
        for high in [low + 20, low + 50, low + 100]:

            frames = np.random.randint(low, high=high, size=15)

            for x_min in [None, 0, 20]:
                for x_max in [None, 20, 100]:
                    for pad in [False, True]:
                        if np.any(frames < 0):
                            yield __test_fail, frames, x_min, x_max, pad
                        else:
                            yield __test_pass, frames, x_min, x_max, pad


def test_normalize():

    def __test_pass(X, norm, axis):
        X_norm = librosa.util.normalize(X, norm=norm, axis=axis)

        if norm is None:
            assert np.allclose(X, X_norm)
            return

        X_norm = np.abs(X_norm)

        if norm == np.inf:
            values = np.max(X_norm, axis=axis)
        elif norm == -np.inf:
            values = np.min(X_norm, axis=axis)
        elif norm == 0:
            # XXX: normalization here isn't quite right
            values = np.ones(1)

        else:
            values = np.sum(X_norm**norm, axis=axis)**(1./norm)

        assert np.allclose(values, np.ones_like(values))

    @raises(librosa.ParameterError)
    def __test_fail(X, norm, axis):
        librosa.util.normalize(X, norm=norm, axis=axis)

    for ndims in [1, 2, 3]:
        X = np.random.randn(* ([16] * ndims))

        for axis in range(X.ndim):
            for norm in [np.inf, -np.inf, 0, 0.5, 1.0, 2.0, None]:
                yield __test_pass, X, norm, axis

            for norm in ['inf', -0.5, -2]:
                yield __test_fail, X, norm, axis


def test_axis_sort():

    def __test_pass(data, axis, index, value):

        if index:
            Xsorted, idx = librosa.util.axis_sort(data,
                                                  axis=axis,
                                                  index=index,
                                                  value=value)

            cmp_slice = [slice(None)] * X.ndim
            cmp_slice[axis] = idx

            assert np.allclose(X[cmp_slice], Xsorted)

        else:
            Xsorted = librosa.util.axis_sort(data,
                                             axis=axis,
                                             index=index,
                                             value=value)

        compare_axis = np.mod(1 - axis, 2)

        if value is None:
            value = np.argmax

        sort_values = value(Xsorted, axis=compare_axis)

        assert np.allclose(sort_values, np.sort(sort_values))

    @raises(librosa.ParameterError)
    def __test_fail(data, axis, index, value):
        librosa.util.axis_sort(data, axis=axis, index=index, value=value)

    for ndim in [1, 2, 3]:
        X = np.random.randn(*([10] * ndim))

        for axis in [0, 1, -1]:
            for index in [False, True]:
                for value in [None, np.min, np.mean, np.max]:

                    if ndim == 2:
                        yield __test_pass, X, axis, index, value
                    else:
                        yield __test_fail, X, axis, index, value


def test_match_intervals():

    def __make_intervals(n):
        return np.cumsum(np.abs(np.random.randn(n, 2)), axis=1)

    def __compare(i1, i2):

        return np.maximum(0, np.minimum(i1[-1], i2[-1])
                          - np.maximum(i1[0], i2[0]))

    def __is_best(y, ints1, ints2):

        for i in range(len(y)):
            values = np.asarray([__compare(ints1[i], i2) for i2 in ints2])
            if np.any(values > values[y[i]]):
                return False

        return True

    def __test(n, m):
        ints1 = __make_intervals(n)
        ints2 = __make_intervals(m)

        y_pred = librosa.util.match_intervals(ints1, ints2)

        assert __is_best(y_pred, ints1, ints2)

    @raises(librosa.ParameterError)
    def __test_fail(n, m):
        ints1 = __make_intervals(n)
        ints2 = __make_intervals(m)

        librosa.util.match_intervals(ints1, ints2)

    for n in [0, 1, 5, 20, 100]:
        for m in [0, 1, 5, 20, 100]:
            if n == 0 or m == 0:
                yield __test_fail, n, m
            else:
                yield __test, n, m

    # TODO:   2015-01-20 17:04:55 by Brian McFee <brian.mcfee@nyu.edu>
    # add coverage for shape errors


def test_match_events():

    def __make_events(n):
        return np.abs(np.random.randn(n))

    def __is_best(y, ev1, ev2):
        for i in range(len(y)):
            values = np.asarray([np.abs(ev1[i] - e2) for e2 in ev2])
            if np.any(values < values[y[i]]):
                return False

        return True

    def __test(n, m):
        ev1 = __make_events(n)
        ev2 = __make_events(m)

        y_pred = librosa.util.match_events(ev1, ev2)

        assert __is_best(y_pred, ev1, ev2)

    @raises(librosa.ParameterError)
    def __test_fail(n, m):
        ev1 = __make_events(n)
        ev2 = __make_events(m)
        librosa.util.match_events(ev1, ev2)

    for n in [0, 1, 5, 20, 100]:
        for m in [0, 1, 5, 20, 100]:
            if n == 0 or m == 0:
                yield __test_fail, n, m
            else:
                yield __test, n, m


def test_localmax():

    def __test(ndim, axis):

        data = np.random.randn(*([20] * ndim))

        lm = librosa.util.localmax(data, axis=axis)

        for hits in np.argwhere(lm):
            for offset in [-1, 1]:
                compare_idx = hits.copy()
                compare_idx[axis] += offset

                if compare_idx[axis] < 0:
                    continue

                if compare_idx[axis] >= data.shape[axis]:
                    continue

                if offset < 0:
                    assert data[tuple(hits)] > data[tuple(compare_idx)]
                else:
                    assert data[tuple(hits)] >= data[tuple(compare_idx)]

    for ndim in range(1, 5):
        for axis in range(ndim):
            yield __test, ndim, axis


def test_feature_extractor():

    y, sr = librosa.load('data/test1_22050.wav')

    def __test_positional_iterate(myfunc, args):

        output_raw = myfunc(y, **args)

        FP = librosa.util.FeatureExtractor(myfunc, **args)
        output = FP.transform([y])

        assert np.allclose(output, output_raw)

        # Ensure that fitting does nothing
        FP.fit()
        output = FP.transform([y])
        assert np.allclose(output, output_raw)

    def __test_positional(myfunc, args):

        output_raw = myfunc(y, **args)

        FP = librosa.util.FeatureExtractor(myfunc, iterate=False, **args)
        output = FP.transform(y)

        assert np.allclose(output, output_raw)

        # Ensure that fitting does nothing
        FP.fit()
        output = FP.transform(y)
        assert np.allclose(output, output_raw)

    def __test_keyword_iterate(myfunc, args):

        output_raw = myfunc(y=y, **args)

        FP = librosa.util.FeatureExtractor(myfunc, target='y', **args)
        output = FP.transform([y])

        assert np.allclose(output, output_raw)

        # Ensure that fitting does nothing
        FP.fit()
        output = FP.transform([y])
        assert np.allclose(output, output_raw)

    def __test_keyword(myfunc, args):

        output_raw = myfunc(y=y, **args)

        FP = librosa.util.FeatureExtractor(myfunc, target='y',
                                           iterate=False, **args)
        output = FP.transform(y)

        assert np.allclose(output, output_raw)

        # Ensure that fitting does nothing
        FP.fit()
        output = FP.transform(y)
        assert np.allclose(output, output_raw)

    func = librosa.feature.melspectrogram
    args = {'sr': sr}

    for n_fft in [1024, 2048]:
        for n_mels in [32, 64, 128]:
            args['n_fft'] = n_fft
            args['n_mels'] = n_mels

            yield __test_positional_iterate, func, args
            yield __test_keyword_iterate, func, args
            yield __test_positional, func, args
            yield __test_keyword, func, args


def test_peak_pick():

    def __test(n, pre_max, post_max, pre_avg, post_avg, delta, wait):

        # Generate a test signal
        x = np.random.randn(n)**2

        peaks = librosa.util.peak_pick(x,
                                       pre_max, post_max,
                                       pre_avg, post_avg,
                                       delta, wait)

        for i in peaks:
            # Test 1: is it a peak in this window?
            s = i - pre_max
            if s < 0:
                s = 0
            t = i + post_max

            diff = x[i] - np.max(x[s:t])
            assert diff > 0 or np.isclose(diff, 0, rtol=1e-3, atol=1e-4)

            # Test 2: is it a big enough peak to count?
            s = i - pre_avg
            if s < 0:
                s = 0
            t = i + post_avg

            diff = x[i] - (delta + np.mean(x[s:t]))
            assert diff > 0 or np.isclose(diff, 0, rtol=1e-3, atol=1e-4)

        # Test 3: peak separation
        assert not np.any(np.diff(peaks) <= wait)

    @raises(librosa.ParameterError)
    def __test_shape_fail():
        x = np.eye(10)
        librosa.util.peak_pick(x, 1, 1, 1, 1, 0.5, 1)

    yield __test_shape_fail

    win_range = [-1, 0, 1, 10]

    for n in [1, 5, 10, 100]:
        for pre_max in win_range:
            for post_max in win_range:
                for pre_avg in win_range:
                    for post_avg in win_range:
                        for wait in win_range:
                            for delta in [-1, 0.05, 100.0]:
                                tf = __test
                                if pre_max < 0:
                                    tf = raises(librosa.ParameterError)(__test)
                                if pre_avg < 0:
                                    tf = raises(librosa.ParameterError)(__test)
                                if delta < 0:
                                    tf = raises(librosa.ParameterError)(__test)
                                if wait < 0:
                                    tf = raises(librosa.ParameterError)(__test)
                                if post_max <= 0:
                                    tf = raises(librosa.ParameterError)(__test)
                                if post_avg <= 0:
                                    tf = raises(librosa.ParameterError)(__test)
                                yield (tf, n, pre_max, post_max,
                                       pre_avg, post_avg, delta, wait)


def test_sparsify_rows():

    def __test(n, d, q):

        X = np.random.randn(*([d] * n))**4

        X = np.asarray(X)

        xs = librosa.util.sparsify_rows(X, quantile=q)

        if ndim == 1:
            X = X.reshape((1, -1))

        assert np.allclose(xs.shape, X.shape)

        # And make sure that xs matches X on nonzeros
        xsd = np.asarray(xs.todense())

        for i in range(xs.shape[0]):
            assert np.allclose(xsd[i, xs[i].indices], X[i, xs[i].indices])

        # Compute row-wise magnitude marginals
        v_in = np.sum(np.abs(X), axis=-1)
        v_out = np.sum(np.abs(xsd), axis=-1)

        # Ensure that v_out retains 1-q fraction of v_in
        assert np.all(v_out >= (1.0 - q) * v_in)

    for ndim in range(1, 4):
        for d in [1, 5, 10, 100]:
            for q in [-1, 0.0, 0.01, 0.25, 0.5, 0.99, 1.0, 2.0]:
                tf = __test
                if ndim not in [1, 2]:
                    tf = raises(librosa.ParameterError)(__test)

                if not 0.0 <= q < 1:
                    tf = raises(librosa.ParameterError)(__test)

                yield tf, ndim, d, q


def test_files():

    # Expected output
    output = [os.path.join(os.path.abspath(os.path.curdir), 'data', s)
              for s in ['test1_22050.wav',
                        'test1_44100.wav',
                        'test2_8000.wav']]

    def __test(searchdir, ext, recurse, case_sensitive, limit, offset):
        files = librosa.util.find_files(searchdir,
                                        ext=ext,
                                        recurse=recurse,
                                        case_sensitive=case_sensitive,
                                        limit=limit,
                                        offset=offset)

        s1 = slice(offset, None)
        s2 = slice(limit)

        assert set(files) == set(output[s1][s2])

    for searchdir in [os.path.curdir, os.path.join(os.path.curdir, 'data')]:
        for ext in [None, 'wav', 'WAV', ['wav'], ['WAV']]:
            for recurse in [False, True]:
                for case_sensitive in [False, True]:
                    for limit in [None, 1, 2]:
                        for offset in [0, 1, -1]:
                            tf = __test

                            if searchdir == os.path.curdir and not recurse:
                                tf = raises(AssertionError)(__test)

                            if (ext is not None and
                                case_sensitive and
                                (ext == 'WAV' or set(ext) == set(['WAV']))):

                                tf = raises(AssertionError)(__test)

                            yield (tf, searchdir, ext, recurse,
                                   case_sensitive, limit, offset)


def test_valid_int():

    def __test(x_in, cast):
        z = librosa.util.valid_int(x_in, cast)

        assert isinstance(z, int)
        if cast is None:
            assert z == int(np.floor(x_in))
        else:
            assert z == int(cast(x_in))

    __test_fail = raises(librosa.ParameterError)(__test)

    for x in np.linspace(-2, 2, num=6):
        for cast in [None, np.floor, np.ceil, 7]:
            if cast is None or six.callable(cast):
                yield __test, x, cast
            else:
                yield __test_fail, x, cast


def test_valid_intervals():

    def __test(intval):
        librosa.util.valid_intervals(intval)

    for d in range(1, 4):
        for n in range(1, 4):
            ivals = np.ones(d * [n])
            for m in range(1, 3):
                slices = [slice(m)] * d
                if m == 2 and d == 2 and n > 1:
                    yield __test, ivals[slices]
                else:
                    yield raises(librosa.ParameterError)(__test), ivals[slices]


def test_warning_deprecated():

    @librosa.util.decorators.deprecated('old_version', 'new_version')
    def __dummy():
        return True

    warnings.resetwarnings()
    with warnings.catch_warnings(record=True) as out:
        x = __dummy()

        # Make sure we still get the right value
        assert x is True

        # And that the warning triggered
        assert len(out) > 0

        # And that the category is correct
        assert out[0].category is DeprecationWarning

        # And that it says the right thing (roughly)
        assert 'deprecated' in str(out[0].message).lower()


def test_warning_moved():

    @librosa.util.decorators.moved('from', 'old_version', 'new_version')
    def __dummy():
        return True

    warnings.resetwarnings()
    with warnings.catch_warnings(record=True) as out:
        x = __dummy()

        # Make sure we still get the right value
        assert x is True

        # And that the warning triggered
        assert len(out) > 0

        # And that the category is correct
        assert out[0].category is DeprecationWarning

        # And that it says the right thing (roughly)
        assert 'moved' in str(out[0].message).lower()
