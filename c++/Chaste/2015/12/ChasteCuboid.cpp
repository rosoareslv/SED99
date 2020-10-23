/*

Copyright (c) 2005-2015, University of Oxford.
All rights reserved.

University of Oxford means the Chancellor, Masters and Scholars of the
University of Oxford, having an administrative office at Wellington
Square, Oxford OX1 2JD, UK.

This file is part of Chaste.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
 * Neither the name of the University of Oxford nor the names of its
   contributors may be used to endorse or promote products derived from this
   software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "ChasteCuboid.hpp"
#include "Exception.hpp"

template <unsigned SPACE_DIM>
    ChasteCuboid<SPACE_DIM>::ChasteCuboid(ChastePoint<SPACE_DIM>& rLowerPoint, ChastePoint<SPACE_DIM>& rUpperPoint)
        : mLowerCorner(rLowerPoint),
          mUpperCorner(rUpperPoint)
    {
        for (unsigned dim=0; dim<SPACE_DIM; dim++)
        {
            if (mLowerCorner[dim] > mUpperCorner[dim])
            {
                EXCEPTION("Attempt to create a cuboid with MinCorner greater than MaxCorner in some dimension");
            }
        }
    }

template <unsigned SPACE_DIM>
    bool ChasteCuboid<SPACE_DIM>::DoesContain(const ChastePoint<SPACE_DIM>& rPointToCheck) const
    {
        for (unsigned dim=0; dim<SPACE_DIM; dim++)
        {
            if (rPointToCheck[dim] < mLowerCorner[dim] - 100*DBL_EPSILON
                || mUpperCorner[dim] + 100* DBL_EPSILON < rPointToCheck[dim])
            {
                return false;
            }
        }
        return true;
    }

template <unsigned SPACE_DIM>
    const ChastePoint<SPACE_DIM>& ChasteCuboid<SPACE_DIM>::rGetUpperCorner() const
    {
        return mUpperCorner;
    }

template <unsigned SPACE_DIM>
    const ChastePoint<SPACE_DIM>& ChasteCuboid<SPACE_DIM>::rGetLowerCorner() const
    {
        return mLowerCorner;
    }

template <unsigned SPACE_DIM>
    double ChasteCuboid<SPACE_DIM>::GetWidth(unsigned rDimension) const
    {
        assert(rDimension<SPACE_DIM);
        return mUpperCorner[rDimension] - mLowerCorner[rDimension];
    }

template <unsigned SPACE_DIM>
     unsigned ChasteCuboid<SPACE_DIM>::GetLongestAxis() const
     {
        unsigned axis=0;
        double max_dimension = 0.0;
        for (unsigned i=0; i<SPACE_DIM; i++)
        {
            double dimension =  mUpperCorner[i] - mLowerCorner[i];
            if ( dimension > max_dimension)
            {
                axis=i;
                max_dimension = dimension;
            }
        }
        return axis;
     }

/////////////////////////////////////////////////////////////////////
// Explicit instantiation
/////////////////////////////////////////////////////////////////////

template class ChasteCuboid<1>;
template class ChasteCuboid<2>;
template class ChasteCuboid<3>;

// Serialization for Boost >= 1.36
#include "SerializationExportWrapperForCpp.hpp"
EXPORT_TEMPLATE_CLASS_SAME_DIMS(ChasteCuboid)

