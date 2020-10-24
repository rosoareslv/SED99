"""
Placing text boxes
==================

When decorating axes with text boxes, two useful tricks are to place
the text in axes coordinates (see :ref:`sphx_glr_tutorials_advanced_transforms_tutorial.py`), so the
text doesn't move around with changes in x or y limits.  You can also
use the ``bbox`` property of text to surround the text with a
:class:`~matplotlib.patches.Patch` instance -- the ``bbox`` keyword
argument takes a dictionary with keys that are Patch properties.
"""

import numpy as np
import matplotlib.pyplot as plt

np.random.seed(1234)

fig, ax = plt.subplots()
x = 30*np.random.randn(10000)
mu = x.mean()
median = np.median(x)
sigma = x.std()
textstr = '$\mu=%.2f$\n$\mathrm{median}=%.2f$\n$\sigma=%.2f$' % (mu, median, sigma)

ax.hist(x, 50)
# these are matplotlib.patch.Patch properties
props = dict(boxstyle='round', facecolor='wheat', alpha=0.5)

# place a text box in upper left in axes coords
ax.text(0.05, 0.95, textstr, transform=ax.transAxes, fontsize=14,
        verticalalignment='top', bbox=props)
