import os
import numpy as np

from nose.tools import assert_equal

from nibabel import Nifti1Image
from sklearn.externals import joblib

from nilearn.image import new_img_like
from nilearn._utils import niimg
from nilearn._utils.testing import assert_raises_regex


currdir = os.path.dirname(os.path.abspath(__file__))


def test_copy_img():
    assert_raises_regex(ValueError, "Input value is not an image",
                        niimg.copy_img, 3)


def test_copy_img_side_effect():
    img1 = Nifti1Image(np.ones((2, 2, 2, 2)), affine=np.eye(4))
    hash1 = joblib.hash(img1)
    niimg.copy_img(img1)
    hash2 = joblib.hash(img1)
    assert_equal(hash1, hash2)


def test_new_img_like_side_effect():
    img1 = Nifti1Image(np.ones((2, 2, 2, 2)), affine=np.eye(4))
    hash1 = joblib.hash(img1)
    new_img_like(img1, np.ones((2, 2, 2, 2)), img1.get_affine().copy(),
                 copy_header=True)
    hash2 = joblib.hash(img1)
    assert_equal(hash1, hash2)
