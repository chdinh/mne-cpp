//=============================================================================================================
/**
* @file     fiff_digitizer_data.cpp
* @author   Christoph Dinh <chdinh@nmr.mgh.harvard.edu>;
*           Matti Hamalainen <msh@nmr.mgh.harvard.edu>
* @version  1.0
* @date     July, 2012
*
* @section  LICENSE
*
* Copyright (C) 2012, Christoph Dinh and Matti Hamalainen. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that
* the following conditions are met:
*     * Redistributions of source code must retain the above copyright notice, this list of conditions and the
*       following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
*       the following disclaimer in the documentation and/or other materials provided with the distribution.
*     * Neither the name of MNE-CPP authors nor the names of its contributors may be used
*       to endorse or promote products derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
* PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*
*
* @brief    Implementation of the FiffDigitizerData Class.
*
*/

//*************************************************************************************************************
//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "fiff_digitizer_data.h"
#include "fiff_coord_trans_old.h"
#include "../fiff_dig_point.h"

//*************************************************************************************************************
//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace FIFFLIB;


//*************************************************************************************************************
//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

FiffDigitizerData::FiffDigitizerData()
: filename(Q_NULLPTR)
, head_mri_t(Q_NULLPTR)
, head_mri_t_adj(Q_NULLPTR)
, points(Q_NULLPTR)
, coord_frame(FIFFV_COORD_UNKNOWN)
, active(Q_NULLPTR)
, discard(Q_NULLPTR)
, npoint(0)
, mri_fids(Q_NULLPTR)
, nfids(0)
, show(FALSE)
, show_minimal(FALSE)
, dist(Q_NULLPTR)
, closest(Q_NULLPTR)
, closest_point(Q_NULLPTR)
, dist_valid(FALSE)
{
}


//*************************************************************************************************************

FiffDigitizerData::FiffDigitizerData(const FiffDigitizerData& p_FiffDigitizerData)
:  filename(p_FiffDigitizerData.filename)
, head_mri_t(p_FiffDigitizerData.head_mri_t)
, head_mri_t_adj(p_FiffDigitizerData.head_mri_t_adj)
, points(p_FiffDigitizerData.points)
, coord_frame(p_FiffDigitizerData.coord_frame)
, active(p_FiffDigitizerData.active)
, discard(p_FiffDigitizerData.discard)
, npoint(p_FiffDigitizerData.npoint)
, mri_fids(p_FiffDigitizerData.mri_fids)
, nfids(p_FiffDigitizerData.nfids)
, show(p_FiffDigitizerData.show)
, show_minimal(p_FiffDigitizerData.show_minimal)
, dist(p_FiffDigitizerData.dist)
, closest(p_FiffDigitizerData.closest)
, closest_point(p_FiffDigitizerData.closest_point)
, dist_valid(p_FiffDigitizerData.dist_valid)
{
}


//*************************************************************************************************************

FiffDigitizerData::~FiffDigitizerData()
{
    FREE_43(filename);
    FREE_43(points);
    FREE_43(head_mri_t);
    FREE_43(head_mri_t_adj);
    FREE_43(dist);
    FREE_43(closest);
    FREE_43(active);
    FREE_43(discard);
    FREE_CMATRIX_43(closest_point);
    FREE_43(mri_fids);

}
