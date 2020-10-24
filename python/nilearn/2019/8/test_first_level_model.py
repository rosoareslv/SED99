# emacs: -*- mode: python; py-indent-offset: 4; indent-tabs-mode: nil -*-
# vi: set ft=python sts=4 ts=4 sw=4 et:
"""
Test the first level model.
"""
from __future__ import with_statement

import os
import shutil
import warnings

import numpy as np
import pandas as pd

from nibabel import (load,
                     Nifti1Image,
                     )
from nose.tools import (assert_equal,
                        assert_raises,
                        assert_true,
                        )
from numpy.testing import (assert_almost_equal,
                           assert_array_equal,
                           )
from nibabel.tmpdirs import InTemporaryDirectory

from nistats.design_matrix import (check_design_matrix,
                                   make_first_level_design_matrix,
                                   )
from nistats.first_level_model import (first_level_models_from_bids,
                                       FirstLevelModel,
                                       mean_scaling,
                                       run_glm,
                                       )
from nistats.utils import get_bids_files
from nistats._utils.testing import (_create_fake_bids_dataset,
                                    _generate_fake_fmri_data,
                                    _write_fake_fmri_data,
                                    )


BASEDIR = os.path.dirname(os.path.abspath(__file__))
FUNCFILE = os.path.join(BASEDIR, 'functional.nii.gz')


def test_high_level_glm_one_session():
    # New API
    shapes, rk = [(7, 8, 9, 15)], 3
    mask, fmri_data, design_matrices = _generate_fake_fmri_data(shapes, rk)

    single_session_model = FirstLevelModel(mask_img=None).fit(
        fmri_data[0], design_matrices=design_matrices[0])
    assert_true(isinstance(single_session_model.masker_.mask_img_,
                           Nifti1Image))

    single_session_model = FirstLevelModel(mask_img=mask).fit(
        fmri_data[0], design_matrices=design_matrices[0])
    z1 = single_session_model.compute_contrast(np.eye(rk)[:1])
    assert_true(isinstance(z1, Nifti1Image))


def test_high_level_glm_with_data():
    # New API
    with InTemporaryDirectory():
        shapes, rk = ((7, 8, 7, 15), (7, 8, 7, 16)), 3
        mask, fmri_data, design_matrices = _write_fake_fmri_data(shapes, rk)
        multi_session_model = FirstLevelModel(mask_img=None).fit(
            fmri_data, design_matrices=design_matrices)
        n_voxels = multi_session_model.masker_.mask_img_.get_data().sum()
        z_image = multi_session_model.compute_contrast(np.eye(rk)[1])
        assert_equal(np.sum(z_image.get_data() != 0), n_voxels)
        assert_true(z_image.get_data().std() < 3.)
        
        # with mask
        multi_session_model = FirstLevelModel(mask_img=mask).fit(
            fmri_data, design_matrices=design_matrices)
        z_image = multi_session_model.compute_contrast(
            np.eye(rk)[:2], output_type='z_score')
        p_value = multi_session_model.compute_contrast(
            np.eye(rk)[:2], output_type='p_value')
        stat_image = multi_session_model.compute_contrast(
            np.eye(rk)[:2], output_type='stat')
        effect_image = multi_session_model.compute_contrast(
            np.eye(rk)[:2], output_type='effect_size')
        variance_image = multi_session_model.compute_contrast(
            np.eye(rk)[:2], output_type='effect_variance')
        assert_array_equal(z_image.get_data() == 0., load(mask).get_data() == 0.)
        assert_true(
                (variance_image.get_data()[load(mask).get_data() > 0] > .001).all())
        
        all_images = multi_session_model.compute_contrast(
                np.eye(rk)[:2], output_type='all')
        
        assert_array_equal(all_images['z_score'].get_data(), z_image.get_data())
        assert_array_equal(all_images['p_value'].get_data(), p_value.get_data())
        assert_array_equal(all_images['stat'].get_data(), stat_image.get_data())
        assert_array_equal(all_images['effect_size'].get_data(), effect_image.get_data())
        assert_array_equal(all_images['effect_variance'].get_data(), variance_image.get_data())
        # Delete objects attached to files to avoid WindowsError when deleting
        # temporary directory (in Windows)
        del (all_images,
             design_matrices,
             effect_image,
             fmri_data,
             mask,
             multi_session_model,
             n_voxels,
             p_value,
             rk,
             shapes,
             stat_image,
             variance_image,
             z_image,
         )


def test_high_level_glm_with_paths():
    # New API
    shapes, rk = ((7, 8, 7, 15), (7, 8, 7, 14)), 3
    with InTemporaryDirectory():
        mask_file, fmri_files, design_files = _write_fake_fmri_data(shapes, rk)
        multi_session_model = FirstLevelModel(mask_img=None).fit(
            fmri_files, design_matrices=design_files)
        z_image = multi_session_model.compute_contrast(np.eye(rk)[1])
        assert_array_equal(z_image.affine, load(mask_file).affine)
        assert_true(z_image.get_data().std() < 3.)
        # Delete objects attached to files to avoid WindowsError when deleting
        # temporary directory (in Windows)
        del z_image, fmri_files, multi_session_model


def test_high_level_glm_null_contrasts():
    # test that contrast computation is resilient to 0 values.
    # new API
    shapes, rk = ((7, 8, 7, 15), (7, 8, 7, 19)), 3
    mask, fmri_data, design_matrices = _generate_fake_fmri_data(shapes, rk)

    multi_session_model = FirstLevelModel(mask_img=None).fit(
        fmri_data, design_matrices=design_matrices)
    single_session_model = FirstLevelModel(mask_img=None).fit(
        fmri_data[0], design_matrices=design_matrices[0])
    z1 = multi_session_model.compute_contrast([np.eye(rk)[:1],
                                               np.zeros((1, rk))],
                                              output_type='stat')
    z2 = single_session_model.compute_contrast(np.eye(rk)[:1],
                                               output_type='stat')
    np.testing.assert_almost_equal(z1.get_data(), z2.get_data())


def test_run_glm():
    # New API
    n, p, q = 100, 80, 10
    X, Y = np.random.randn(p, q), np.random.randn(p, n)

    # Ordinary Least Squares case
    labels, results = run_glm(Y, X, 'ols')
    assert_array_equal(labels, np.zeros(n))
    assert_equal(list(results.keys()), [0.0])
    assert_equal(results[0.0].theta.shape, (q, n))
    assert_almost_equal(results[0.0].theta.mean(), 0, 1)
    assert_almost_equal(results[0.0].theta.var(), 1. / p, 1)

    # ar(1) case
    labels, results = run_glm(Y, X, 'ar1')
    assert_equal(len(labels), n)
    assert_true(len(results.keys()) > 1)
    tmp = sum([val.theta.shape[1] for val in results.values()])
    assert_equal(tmp, n)

    # non-existant case
    assert_raises(ValueError, run_glm, Y, X, 'ar2')
    assert_raises(ValueError, run_glm, Y, X.T)


def test_scaling():
    """Test the scaling function"""
    shape = (400, 10)
    u = np.random.randn(*shape)
    mean = 100 * np.random.rand(shape[1]) + 1
    Y = u + mean
    Y_, mean_ = mean_scaling(Y)
    assert_almost_equal(Y_.mean(0), 0, 5)
    assert_almost_equal(mean_, mean, 0)
    assert_true(Y.std() > 1)


def test_fmri_inputs():
    # Test processing of FMRI inputs
    with InTemporaryDirectory():
        shapes = ((7, 8, 9, 10),)
        mask, FUNCFILE, _ = _write_fake_fmri_data(shapes)
        FUNCFILE = FUNCFILE[0]
        func_img = load(FUNCFILE)
        T = func_img.shape[-1]
        conf = pd.DataFrame([0, 0])
        des = pd.DataFrame(np.ones((T, 1)), columns=[''])
        des_fname = 'design.csv'
        des.to_csv(des_fname)
        for fi in func_img, FUNCFILE:
            for d in des, des_fname:
                FirstLevelModel().fit(fi, design_matrices=d)
                FirstLevelModel(mask_img=None).fit([fi], design_matrices=d)
                FirstLevelModel(mask_img=mask).fit(fi, design_matrices=[d])
                FirstLevelModel(mask_img=mask).fit([fi], design_matrices=[d])
                FirstLevelModel(mask_img=mask).fit([fi, fi], design_matrices=[d, d])
                FirstLevelModel(mask_img=None).fit((fi, fi), design_matrices=(d, d))
                assert_raises(
                    ValueError, FirstLevelModel(mask_img=None).fit, [fi, fi], d)
                assert_raises(
                    ValueError, FirstLevelModel(mask_img=None).fit, fi, [d, d])
                # At least paradigms or design have to be given
                assert_raises(
                    ValueError, FirstLevelModel(mask_img=None).fit, fi)
                # If paradigms are given then both tr and slice time ref were
                # required
                assert_raises(
                    ValueError, FirstLevelModel(mask_img=None).fit, fi, d)
                assert_raises(
                    ValueError, FirstLevelModel(mask_img=None, t_r=1.0).fit, fi, d)
                assert_raises(
                    ValueError, FirstLevelModel(mask_img=None, slice_time_ref=0.).fit, fi, d)
            # confounds rows do not match n_scans
            assert_raises(
                ValueError, FirstLevelModel(mask_img=None).fit, fi, d, conf)
        # Delete objects attached to files to avoid WindowsError when deleting
        # temporary directory (in Windows)
        del fi, func_img, mask, d, des, FUNCFILE, _


def basic_paradigm():
    conditions = ['c0', 'c0', 'c0', 'c1', 'c1', 'c1', 'c2', 'c2', 'c2']
    onsets = [30, 70, 100, 10, 30, 90, 30, 40, 60]
    durations = 1 * np.ones(9)
    events = pd.DataFrame({'trial_type': conditions,
                             'onset': onsets,
                             'duration': durations})
    return events


def test_first_level_model_design_creation():
        # Test processing of FMRI inputs
    with InTemporaryDirectory():
        shapes = ((7, 8, 9, 10),)
        mask, FUNCFILE, _ = _write_fake_fmri_data(shapes)
        FUNCFILE = FUNCFILE[0]
        func_img = load(FUNCFILE)
        # basic test based on basic_paradigm and glover hrf
        t_r = 10.0
        slice_time_ref = 0.
        events = basic_paradigm()
        model = FirstLevelModel(t_r, slice_time_ref, mask_img=mask,
                                drift_model='polynomial', drift_order=3)
        model = model.fit(func_img, events)
        frame1, X1, names1 = check_design_matrix(model.design_matrices_[0])
        # check design computation is identical
        n_scans = func_img.get_data().shape[3]
        start_time = slice_time_ref * t_r
        end_time = (n_scans - 1 + slice_time_ref) * t_r
        frame_times = np.linspace(start_time, end_time, n_scans)
        design = make_first_level_design_matrix(frame_times, events,
                                                drift_model='polynomial', drift_order=3)
        frame2, X2, names2 = check_design_matrix(design)
        assert_array_equal(frame1, frame2)
        assert_array_equal(X1, X2)
        assert_array_equal(names1, names2)
        # Delete objects attached to files to avoid WindowsError when deleting
        # temporary directory (in Windows)
        del FUNCFILE, mask, model, func_img


def test_first_level_model_glm_computation():
    with InTemporaryDirectory():
        shapes = ((7, 8, 9, 10),)
        mask, FUNCFILE, _ = _write_fake_fmri_data(shapes)
        FUNCFILE = FUNCFILE[0]
        func_img = load(FUNCFILE)
        # basic test based on basic_paradigm and glover hrf
        t_r = 10.0
        slice_time_ref = 0.
        events = basic_paradigm()
        # Ordinary Least Squares case
        model = FirstLevelModel(t_r, slice_time_ref, mask_img=mask,
                                drift_model='polynomial', drift_order=3,
                                minimize_memory=False)
        model = model.fit(func_img, events)

        # Delete objects attached to files to avoid WindowsError when deleting
        # temporary directory (in Windows)
        del mask, FUNCFILE, func_img, model


def test_first_level_glm_computation_with_memory_caching():
    with InTemporaryDirectory():
        shapes = ((7, 8, 9, 10),)
        mask, FUNCFILE, _ = _write_fake_fmri_data(shapes)
        FUNCFILE = FUNCFILE[0]
        func_img = load(FUNCFILE)
        # initialize FirstLevelModel with memory option enabled
        t_r = 10.0
        slice_time_ref = 0.
        events = basic_paradigm()
        # Ordinary Least Squares case
        model = FirstLevelModel(t_r, slice_time_ref, mask_img=mask,
                                drift_model='polynomial', drift_order=3,
                                memory='nilearn_cache', memory_level=1,
                                minimize_memory=False)
        model.fit(func_img, events)
        # Delete objects attached to files to avoid WindowsError when deleting
        # temporary directory (in Windows)
        del mask, func_img, FUNCFILE, model


def test_first_level_model_contrast_computation():
    with InTemporaryDirectory():
        shapes = ((7, 8, 9, 10),)
        mask, FUNCFILE, _ = _write_fake_fmri_data(shapes)
        FUNCFILE = FUNCFILE[0]
        func_img = load(FUNCFILE)
        # basic test based on basic_paradigm and glover hrf
        t_r = 10.0
        slice_time_ref = 0.
        events = basic_paradigm()
        # Ordinary Least Squares case
        model = FirstLevelModel(t_r, slice_time_ref, mask_img=mask,
                                drift_model='polynomial', drift_order=3,
                                minimize_memory=False)
        c1, c2, cnull = np.eye(7)[0], np.eye(7)[1], np.zeros(7)
        # asking for contrast before model fit gives error
        assert_raises(ValueError, model.compute_contrast, c1)
        # fit model
        model = model.fit([func_img, func_img], [events, events])
        # smoke test for different contrasts in fixed effects
        model.compute_contrast([c1, c2])
        # smoke test for same contrast in fixed effects
        model.compute_contrast([c2, c2])
        # smoke test for contrast that will be repeated
        model.compute_contrast(c2)
        model.compute_contrast(c2, 'F')
        model.compute_contrast(c2, 't', 'z_score')
        model.compute_contrast(c2, 't', 'stat')
        model.compute_contrast(c2, 't', 'p_value')
        model.compute_contrast(c2, None, 'effect_size')
        model.compute_contrast(c2, None, 'effect_variance')
        # formula should work (passing varible name directly)
        model.compute_contrast('c0')
        model.compute_contrast('c1')
        model.compute_contrast('c2')
        # smoke test for one null contrast in group
        model.compute_contrast([c2, cnull])
        # only passing null contrasts should give back a value error
        assert_raises(ValueError, model.compute_contrast, cnull)
        assert_raises(ValueError, model.compute_contrast, [cnull, cnull])
        # passing wrong parameters
        assert_raises(ValueError, model.compute_contrast, [])
        assert_raises(ValueError, model.compute_contrast, [c1, []])
        assert_raises(ValueError, model.compute_contrast, c1, '', '')
        assert_raises(ValueError, model.compute_contrast, c1, '', [])
        # Delete objects attached to files to avoid WindowsError when deleting
        # temporary directory (in Windows)
        del func_img, FUNCFILE, model


def test_first_level_models_from_bids():
    with InTemporaryDirectory():
        bids_path = _create_fake_bids_dataset(n_sub=10, n_ses=2,
                                             tasks=['localizer', 'main'],
                                             n_runs=[1, 3])
        # test arguments are provided correctly
        assert_raises(TypeError, first_level_models_from_bids, 2, 'main', 'MNI')
        assert_raises(ValueError, first_level_models_from_bids, 'lolo', 'main', 'MNI')
        assert_raises(TypeError, first_level_models_from_bids, bids_path, 2, 'MNI')
        assert_raises(TypeError, first_level_models_from_bids,
                      bids_path, 'main', 'MNI', model_init=[])
        # test output is as expected
        models, m_imgs, m_events, m_confounds = first_level_models_from_bids(
            bids_path, 'main', 'MNI', [('variant', 'some')])
        assert_true(len(models) == len(m_imgs))
        assert_true(len(models) == len(m_events))
        assert_true(len(models) == len(m_confounds))
        # test repeated run tag error when run tag is in filenames
        # can arise when variant or space is present and not specified
        assert_raises(ValueError, first_level_models_from_bids,
                      bids_path, 'main', 'T1w')  # variant not specified
        # test more than one ses file error when run tag is not in filenames
        # can arise when variant or space is present and not specified
        assert_raises(ValueError, first_level_models_from_bids,
                      bids_path, 'localizer', 'T1w')  # variant not specified
        # test issues with confound files. There should be only one confound
        # file per img. An one per image or None. Case when one is missing
        confound_files = get_bids_files(os.path.join(bids_path, 'derivatives'),
                                        file_tag='confounds')
        os.remove(confound_files[-1])
        assert_raises(ValueError, first_level_models_from_bids,
                      bids_path, 'main', 'MNI')
        # test issues with event files
        events_files = get_bids_files(bids_path, file_tag='events')
        os.remove(events_files[0])
        # one file missing
        assert_raises(ValueError, first_level_models_from_bids,
                      bids_path, 'main', 'MNI')
        for f in events_files[1:]:
            os.remove(f)
        # all files missing
        assert_raises(ValueError, first_level_models_from_bids,
                      bids_path, 'main', 'MNI')

        # In case different variant and spaces exist and are not selected we
        # fail and ask for more specific information
        shutil.rmtree(os.path.join(bids_path, 'derivatives'))
        # issue if no derivatives folder is present
        assert_raises(ValueError, first_level_models_from_bids,
                      bids_path, 'main', 'MNI')

        # check runs are not repeated when ses field is not used
        shutil.rmtree(bids_path)
        bids_path = _create_fake_bids_dataset(n_sub=10, n_ses=1,
                                             tasks=['localizer', 'main'],
                                             n_runs=[1, 3], no_session=True)
        # test repeated run tag error when run tag is in filenames and not ses
        # can arise when variant or space is present and not specified
        assert_raises(ValueError, first_level_models_from_bids,
                      bids_path, 'main', 'T1w')  # variant not specified


def test_first_level_models_with_no_signal_scaling():
    """
    test to ensure that the FirstLevelModel works correctly with a
    signal_scaling==False. In particular, that derived theta are correct for a
    constant design matrix with a single valued fmri image
    """
    shapes, rk = [(3, 1, 1, 2)], 1
    fmri_data = list()
    design_matrices = list()
    design_matrices.append(pd.DataFrame(np.ones((shapes[0][-1], rk)),
                                        columns=list('abcdefghijklmnopqrstuvwxyz')[:rk]))
    first_level_model = FirstLevelModel(mask_img=False, noise_model='ols', signal_scaling=False)
    fmri_data.append(Nifti1Image(np.zeros((1, 1, 1, 2)) + 6, np.eye(4)))

    first_level_model.fit(fmri_data, design_matrices=design_matrices)
    # trivial test of signal_scaling value
    assert_true(first_level_model.signal_scaling is False)
    # assert that our design matrix has one constant
    assert_true(first_level_model.design_matrices_[0].equals(
        pd.DataFrame([1.0, 1.0], columns=['a'])))
    # assert that we only have one theta as there is only on voxel in our image
    assert_true(first_level_model.results_[0][0].theta.shape == (1, 1))
    # assert that the theta is equal to the one voxel value
    assert_almost_equal(first_level_model.results_[0][0].theta[0, 0], 6.0, 2)


def test_param_mask_deprecation_FirstLevelModel():
    """ Tests whether use of deprecated keyword parameter `mask`
    raises the correct warning & transfers its value to
    replacement parameter `mask_img` correctly.
    """
    deprecation_msg = (
        'The parameter "mask" will be removed in next release of Nistats. '
        'Please use the parameter "mask_img" instead.'
    )
    mask_filepath = '~/masks/mask_01.nii.gz'
    with warnings.catch_warnings(record=True) as raised_warnings:
        flm1 = FirstLevelModel(2.5,
                               1,
                               mask=mask_filepath,
                               target_shape=(2, 4, 4),
                               )
        
        flm2 = FirstLevelModel(t_r=2.5,
                               slice_time_ref=1,
                               mask=mask_filepath,
                               target_shape=(2, 4, 4),
                               )
        
        flm3 = FirstLevelModel(2.5, 0., 'glover', 'cosine', 128, 1, [0], -24,
                               mask_filepath, None, (2, 4, 4),
                               )
    assert flm1.mask_img == mask_filepath
    assert flm2.mask_img == mask_filepath
    assert flm3.mask_img == mask_filepath
    
    with assert_raises(AttributeError):
        flm1.mask == mask_filepath
    with assert_raises(AttributeError):
        flm2.mask == mask_filepath
    with assert_raises(AttributeError):
        flm3.mask == mask_filepath
    
    assert len(raised_warnings) == 2
    
    raised_param_deprecation_warnings = [
        raised_warning_ for raised_warning_
        in raised_warnings if
        str(raised_warning_.message).startswith('The parameter')
        ]
    
    for param_warning_ in raised_param_deprecation_warnings:
        assert str(param_warning_.message) == deprecation_msg
        assert param_warning_.category is DeprecationWarning


def test_param_mask_deprecation_first_level_models_from_bids():
    deprecation_msg = (
        'The parameter "mask" will be removed in next release of Nistats. '
        'Please use the parameter "mask_img" instead.'
    )
    mask_filepath = '~/masks/mask_01.nii.gz'

    with InTemporaryDirectory():
        bids_path = _create_fake_bids_dataset(n_sub=10, n_ses=2,
                                              tasks=['localizer', 'main'],
                                              n_runs=[1, 3])
        with warnings.catch_warnings(record=True) as raised_warnings:
            first_level_models_from_bids(
                    bids_path, 'main', 'MNI', [('variant', 'some')],
                    mask=mask_filepath)
            first_level_models_from_bids(
                    bids_path, 'main', 'MNI', [('variant', 'some')],
                    mask_img=mask_filepath)

    raised_param_deprecation_warnings = [
        raised_warning_ for raised_warning_
        in raised_warnings if
        str(raised_warning_.message).startswith('The parameter')
        ]

    assert len(raised_param_deprecation_warnings) == 1
    for param_warning_ in raised_param_deprecation_warnings:
        assert str(param_warning_.message) == deprecation_msg
        assert param_warning_.category is DeprecationWarning
