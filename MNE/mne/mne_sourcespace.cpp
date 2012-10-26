//=============================================================================================================
/**
* @file     mne_sourcespace.cpp
* @author   Christoph Dinh <chdinh@nmr.mgh.harvard.edu>;
*           Matti Hämäläinen <msh@nmr.mgh.harvard.edu>
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
*     * Neither the name of the Massachusetts General Hospital nor the names of its contributors may be used
*       to endorse or promote products derived from this software without specific prior written permission.
* 
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
* PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MASSACHUSETTS GENERAL HOSPITAL BE LIABLE FOR ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*
*
* @brief    ToDo Documentation...
*
*/

//*************************************************************************************************************
//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "mne_sourcespace.h"


//*************************************************************************************************************
//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace MNELIB;


//*************************************************************************************************************
//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

MNESourceSpace::MNESourceSpace()
{
}


//*************************************************************************************************************

MNESourceSpace::MNESourceSpace(MNESourceSpace* p_pMNESourceSpace)
{
    for(int i = 0; i < p_pMNESourceSpace->hemispheres.size(); ++i)
        hemispheres.append(new MNEHemisphere(p_pMNESourceSpace->hemispheres.at(i)));
}


//*************************************************************************************************************

MNESourceSpace::~MNESourceSpace()
{
    QList<MNEHemisphere*>::iterator i;
     for (i = hemispheres.begin(); i != hemispheres.end(); ++i)
         delete *i;
}


//*************************************************************************************************************

bool MNESourceSpace::read_source_spaces(FiffStream*& p_pStream, bool add_geom, FiffDirTree*& p_pTree, MNESourceSpace*& p_pSourceSpace)
{
    if (p_pSourceSpace != NULL)
        delete p_pSourceSpace;
    p_pSourceSpace = new MNESourceSpace();


    //
    //   Open the file, create directory
    //
    bool open_here = false;
    if (!p_pStream->device()->isOpen())
    {
        QList<FiffDirEntry>* t_pDir = NULL;
        QString t_sFileName = p_pStream->streamName();

        if(p_pStream)
            delete p_pStream;

        QFile* t_pFile = new QFile(t_sFileName);//ToDo TCPSocket;
        p_pStream = new FiffStream(t_pFile);
        if(!p_pStream->open(p_pTree, t_pDir))
            return false;
        open_here = true;
        if(t_pDir)
            delete t_pDir;
    }
    //
    //   Find all source spaces
    //
    QList<FiffDirTree*> spaces = p_pTree->dir_tree_find(FIFFB_MNE_SOURCE_SPACE);
    if (spaces.size() == 0)
    {
        if(open_here)
            p_pStream->device()->close();
        std::cout << "No source spaces found";
        return false;
    }

    for(int k = 0; k < spaces.size(); ++k)
    {
        MNEHemisphere* p_pHemisphere = NULL;
        printf("\tReading a source space...");
        MNESourceSpace::read_source_space(p_pStream, spaces.at(k), p_pHemisphere);
        printf("\t[done]\n" );
        if (add_geom)
            complete_source_space_info(p_pHemisphere);

        p_pSourceSpace->hemispheres.append(p_pHemisphere);

//           src(k) = this;
    }

    printf("\t%d source spaces read\n", spaces.size());

    if(open_here)
        p_pStream->device()->close();

    return true;
}


//*************************************************************************************************************

qint32 MNESourceSpace::find_source_space_hemi(MNEHemisphere* p_pHemisphere)
{
    double xave = p_pHemisphere->rr.col(0).sum();

    qint32 hemi;
    if (xave < 0)
        hemi = FIFFV_MNE_SURF_LEFT_HEMI;
    else
        hemi = FIFFV_MNE_SURF_RIGHT_HEMI;

    return hemi;
}


//*************************************************************************************************************

bool MNESourceSpace::transform_source_space_to(fiff_int_t dest, FiffCoordTrans* trans)
{
    for(int k = 0; k < this->hemispheres.size(); ++k)
    {
        if(!this->hemispheres.at(k)->transform_hemisphere_to(dest,trans))
        {
            printf("Could not transform source space.\n");
            return false;
        }
    }
    return true;
}


//*************************************************************************************************************

bool MNESourceSpace::read_source_space(FiffStream* p_pStream, FiffDirTree* p_pTree, MNEHemisphere*& p_pHemisphere)
{
    if (p_pHemisphere != NULL)
        delete p_pHemisphere;
    p_pHemisphere = new MNEHemisphere();

    FIFFLIB::FiffTag* t_pTag = NULL;

    //=====================================================================
    if(!p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_ID, t_pTag))
        p_pHemisphere->id = FIFFV_MNE_SURF_UNKNOWN;
    else
        p_pHemisphere->id = *t_pTag->toInt();

//        qDebug() << "Read SourceSpace ID; type:" << t_pTag->getType() << "value:" << *t_pTag->toInt();

    //=====================================================================
    if(!p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_NPOINTS, t_pTag))
    {
        p_pStream->device()->close();
        std::cout << "error: Number of vertices not found."; //ToDo: throw error.
        return false;
    }
//        qDebug() << "Number of vertice; type:" << t_pTag->getType() << "value:" << *t_pTag->toInt();
    p_pHemisphere->np = *t_pTag->toInt();


    //=====================================================================
    if(!p_pTree->find_tag(p_pStream, FIFF_BEM_SURF_NTRI, t_pTag))
    {
        if(!p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_NTRI, t_pTag))
            p_pHemisphere->ntri = 0;
        else
            p_pHemisphere->ntri = *t_pTag->toInt();
    }
    else
    {
        p_pHemisphere->ntri = *t_pTag->toInt();
    }
//        qDebug() << "Number of Tris; type:" << t_pTag->getType() << "value:" << *t_pTag->toInt();


    //=====================================================================
    if(!p_pTree->find_tag(p_pStream, FIFF_MNE_COORD_FRAME, t_pTag))
    {
        p_pStream->device()->close();
        std::cout << "Coordinate frame information not found."; //ToDo: throw error.
        return false;
    }
    p_pHemisphere->coord_frame = *t_pTag->toInt();
//        qDebug() << "Coord Frame; type:" << t_pTag->getType() << "value:" << *t_pTag->toInt();

    //=====================================================================
    //
    //   Vertices, normals, and triangles
    //
    if(!p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_POINTS, t_pTag))
    {
        p_pStream->device()->close();
        std::cout << "Vertex data not found."; //ToDo: throw error.
        return false;
    }

    p_pHemisphere->rr = t_pTag->toFloatMatrix()->transpose();
    qint32 rows_rr = p_pHemisphere->rr.rows();
//        qDebug() << "last element rr: " << p_pHemisphere->rr(rows_rr-1, 0) << p_pHemisphere->rr(rows_rr-1, 1) << p_pHemisphere->rr(rows_rr-1, 2);

    if (rows_rr != p_pHemisphere->np)
    {
        p_pStream->device()->close();
        std::cout << "Vertex information is incorrect."; //ToDo: throw error.
        return false;
    }
//        qDebug() << "Source Space Points; type:" << t_pTag->getType();


    //=====================================================================
    if(!p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_NORMALS, t_pTag))
    {
        p_pStream->device()->close();
        std::cout << "Vertex normals not found."; //ToDo: throw error.
        return false;
    }

    p_pHemisphere->nn = t_pTag->toFloatMatrix()->transpose();
    qint32 rows_nn = p_pHemisphere->nn.rows();

    if (rows_nn != p_pHemisphere->np)
    {
        p_pStream->device()->close();
        std::cout << "Vertex normal information is incorrect."; //ToDo: throw error.
        return false;
    }
//        qDebug() << "Source Space Normals; type:" << t_pTag->getType();


    //=====================================================================
    if (p_pHemisphere->ntri > 0)
    {
        if(!p_pTree->find_tag(p_pStream, FIFF_BEM_SURF_TRIANGLES, t_pTag))
        {
            if(!p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_TRIANGLES, t_pTag))
            {
                p_pStream->device()->close();
                std::cout << "Triangulation not found."; //ToDo: throw error.
                return false;
            }
            else
            {
                p_pHemisphere->tris = t_pTag->toIntMatrix().transpose();
                p_pHemisphere->tris -= MatrixXi::Constant(p_pHemisphere->tris.rows(),3,1);//0 based indizes
            }
        }
        else
        {
            p_pHemisphere->tris = t_pTag->toIntMatrix().transpose();
            p_pHemisphere->tris -= MatrixXi::Constant(p_pHemisphere->tris.rows(),3,1);//0 based indizes
        }
        if (p_pHemisphere->tris.rows() != p_pHemisphere->ntri)
        {
            p_pStream->device()->close();
            std::cout << "Triangulation information is incorrect."; //ToDo: throw error.
            return false;
        }
    }
    else
    {
        MatrixXi p_defaultMatrix(0, 0);
        p_pHemisphere->tris = p_defaultMatrix;
    }
//        qDebug() << "Triangles; type:" << t_pTag->getType() << "rows:" << p_pHemisphere->tris.rows() << "cols:" << p_pHemisphere->tris.cols();


    //
    //   Which vertices are active
    //
    if(!p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_NUSE, t_pTag))
    {
        p_pHemisphere->nuse   = 0;
        p_pHemisphere->inuse  = VectorXi::Zero(p_pHemisphere->nuse);
        VectorXi p_defaultVector;
        p_pHemisphere->vertno = p_defaultVector;
    }
    else
    {
        p_pHemisphere->nuse = *t_pTag->toInt();
        if(!p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_SELECTION, t_pTag))
        {
            p_pStream->device()->close();
            std::cout << "Source selection information missing."; //ToDo: throw error.
            return false;
        }
        p_pHemisphere->inuse = VectorXi(Map<VectorXi>(t_pTag->toInt(), t_pTag->size()/4, 1));//use copy constructor, for the sake of easy memory management

        p_pHemisphere->vertno = VectorXi::Zero(p_pHemisphere->nuse);
        if (p_pHemisphere->inuse.rows() != p_pHemisphere->np)
        {
            p_pStream->device()->close();
            std::cout << "Incorrect number of entries in source space selection."; //ToDo: throw error.
            return false;
        }
        int pp = 0;
        for (int p = 0; p < p_pHemisphere->np; ++p)
        {
            if(p_pHemisphere->inuse(p) == 1)
            {
                p_pHemisphere->vertno(pp) = p;
                ++pp;
            }
        }
    }
//        qDebug() << "Vertices; type:" << t_pTag->getType() << "nuse:" << p_pHemisphere->nuse;

    //
    //   Use triangulation
    //
    FIFFLIB::FiffTag* t_pTag1 = NULL;
    FIFFLIB::FiffTag* t_pTag2 = NULL;
    if(!p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_NUSE_TRI, t_pTag1) || !p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_USE_TRIANGLES, t_pTag2))
    {
        MatrixX3i p_defaultMatrix(0, 0);
        p_pHemisphere->nuse_tri = 0;
        p_pHemisphere->use_tris = p_defaultMatrix;
    }
    else
    {
        p_pHemisphere->nuse_tri = *t_pTag1->toInt();
        p_pHemisphere->use_tris = t_pTag2->toIntMatrix().transpose();
        p_pHemisphere->use_tris -= MatrixXi::Constant(p_pHemisphere->use_tris.rows(),3,1); //0 based indizes
    }
//        qDebug() << "triangulation; type:" << t_pTag2->getType() << "use_tris:" << p_pHemisphere->use_tris.rows()<< "x" << p_pHemisphere->use_tris.cols();

    //
    //   Patch-related information
    //
    if(!p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_NEAREST, t_pTag1) || !p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_NEAREST_DIST, t_pTag2))
    {
        VectorXi p_defaultVector;
        p_pHemisphere->nearest = p_defaultVector;
        VectorXd p_defaultFloatVector;
        p_pHemisphere->nearest_dist = p_defaultFloatVector;
    }
    else
    {
       //res.nearest = tag1.data + 1;
       p_pHemisphere->nearest = VectorXi(Map<VectorXi>(t_pTag1->toInt(), t_pTag1->size()/4, 1));//use copy constructor, for the sake of easy memory management
       p_pHemisphere->nearest_dist = VectorXd((Map<VectorXf>(t_pTag2->toFloat(), t_pTag1->size()/4, 1)).cast<double>());//use copy constructor, for the sake of easy memory management
    }

    patch_info(p_pHemisphere->nearest, p_pHemisphere->pinfo);
    if (p_pHemisphere->pinfo.length() > 0)
    {
       printf("\tPatch information added...");
    }

    //
    // Distances
    //
    if(p_pHemisphere->dist)
        delete p_pHemisphere->dist;
    if(!p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_DIST, t_pTag1) || !p_pTree->find_tag(p_pStream, FIFF_MNE_SOURCE_SPACE_DIST_LIMIT, t_pTag2))
    {
       p_pHemisphere->dist = NULL;
       p_pHemisphere->dist_limit = 0;
    }
    else
    {
        qDebug() << "Attention this haven't been debugged! <- Errors are likely";
        p_pHemisphere->dist       = t_pTag1->toFloatMatrix();
        p_pHemisphere->dist_limit = *t_pTag1->toFloat();
        //
        //  Add the upper triangle
        //
        *p_pHemisphere->dist += p_pHemisphere->dist->transpose();
    }

    delete t_pTag2;
    delete t_pTag1;
    delete t_pTag;

    return true;
}


//*************************************************************************************************************

bool MNESourceSpace::patch_info(VectorXi& nearest, QList<VectorXi>& pinfo)
{
    if (nearest.rows() == 0)
    {
       pinfo.clear();
       return false;
    }

    std::vector<intpair> t_vIndn;

    for(qint32 i = 0; i < nearest.rows(); ++i)
    {
        intpair t_pair(nearest(i),i);
        t_vIndn.push_back(t_pair);
    }
    std::sort(t_vIndn.begin(),t_vIndn.end(), MNESourceSpace::intPairComparator );

    qint32 current = 0;
    std::vector<qint32> t_vfirsti;
    t_vfirsti.push_back(current);
    std::vector<qint32> t_vlasti;

    for(quint32 i = 0; i < t_vIndn.size(); ++i)
    {
        if (t_vIndn[current].first != t_vIndn[i].first)
        {
            current = i;
            t_vlasti.push_back(i-1);
            t_vfirsti.push_back(current);
        }
    }
    t_vlasti.push_back(t_vIndn.size()-1);


    for(quint32 k = 0; k < t_vfirsti.size(); ++k)
    {
        std::vector<int> t_v;

        for(int l = t_vfirsti[k]; l <= t_vlasti[k]; ++l)
            t_v.push_back(t_vIndn[l].second);

        std::sort(t_v.begin(),t_v.end());

        int* t_pV = &t_v[0];
        Eigen::Map<Eigen::VectorXi> t_vEigen(t_pV, t_v.size());

        pinfo.append(t_vEigen);
    }

    return true;
}


//*************************************************************************************************************

bool MNESourceSpace::intPairComparator ( const intpair& l, const intpair& r)
{
    return l.first < r.first;
}


//*************************************************************************************************************

bool MNESourceSpace::complete_source_space_info(MNEHemisphere* p_pHemisphere)
{
    //
    //   Main triangulation
    //
    printf("\tCompleting triangulation info...");
    p_pHemisphere->tri_cent = MatrixX3d::Zero(p_pHemisphere->ntri,3);
    p_pHemisphere->tri_nn = MatrixX3d::Zero(p_pHemisphere->ntri,3);
    p_pHemisphere->tri_area = VectorXd::Zero(p_pHemisphere->ntri);

    Matrix3d r;
    Vector3d a, b;
    int k = 0;
    float size = 0;
    for (qint32 i = 0; i < p_pHemisphere->ntri; ++i)
    {
        for ( qint32 j = 0; j < 3; ++j)
        {
            k = p_pHemisphere->tris(i, j);

            r(j,0) = p_pHemisphere->rr(k, 0);
            r(j,1) = p_pHemisphere->rr(k, 1);
            r(j,2) = p_pHemisphere->rr(k, 2);

            p_pHemisphere->tri_cent(i, 0) += p_pHemisphere->rr(k, 0);
            p_pHemisphere->tri_cent(i, 1) += p_pHemisphere->rr(k, 1);
            p_pHemisphere->tri_cent(i, 2) += p_pHemisphere->rr(k, 2);
        }
        p_pHemisphere->tri_cent.row(i) /= 3.0f;

        //cross product {cross((r2-r1),(r3-r1))}
        a = r.row(1) - r.row(0 );
        b = r.row(2) - r.row(0);
        p_pHemisphere->tri_nn(i,0) = a(1)*b(2)-a(2)*b(1);
        p_pHemisphere->tri_nn(i,1) = a(2)*b(0)-a(0)*b(2);
        p_pHemisphere->tri_nn(i,2) = a(0)*b(1)-a(1)*b(0);

        //area
        size = p_pHemisphere->tri_nn.row(i)*p_pHemisphere->tri_nn.row(i).transpose();
        size = std::pow(size, 0.5f );

        p_pHemisphere->tri_area(i) = size/2.0f;
        p_pHemisphere->tri_nn.row(i) /= size;


    }
    printf("[done]\n");


//        qDebug() << "p_pHemisphere->tri_cent:" << p_pHemisphere->tri_cent(0,0) << p_pHemisphere->tri_cent(0,1) << p_pHemisphere->tri_cent(0,2);
//        qDebug() << "p_pHemisphere->tri_cent:" << p_pHemisphere->tri_cent(2,0) << p_pHemisphere->tri_cent(2,1) << p_pHemisphere->tri_cent(2,2);

//        qDebug() << "p_pHemisphere->tri_nn:" << p_pHemisphere->tri_nn(0,0) << p_pHemisphere->tri_nn(0,1) << p_pHemisphere->tri_nn(0,2);
//        qDebug() << "p_pHemisphere->tri_nn:" << p_pHemisphere->tri_nn(2,0) << p_pHemisphere->tri_nn(2,1) << p_pHemisphere->tri_nn(2,2);

    //
    //   Selected triangles
    //
    printf("\tCompleting selection triangulation info...");
    if (p_pHemisphere->nuse_tri > 0)
    {
        p_pHemisphere->use_tri_cent = MatrixX3d::Zero(p_pHemisphere->nuse_tri,3);
        p_pHemisphere->use_tri_nn = MatrixX3d::Zero(p_pHemisphere->nuse_tri,3);
        p_pHemisphere->use_tri_area = VectorXd::Zero(p_pHemisphere->nuse_tri);


        for (qint32 i = 0; i < p_pHemisphere->nuse_tri; ++i)
        {
            for ( qint32 j = 0; j < 3; ++j)
            {
                k = p_pHemisphere->use_tris(i, j);

                r(j,0) = p_pHemisphere->rr(k, 0);
                r(j,1) = p_pHemisphere->rr(k, 1);
                r(j,2) = p_pHemisphere->rr(k, 2);

                p_pHemisphere->use_tri_cent(i, 0) += p_pHemisphere->rr(k, 0);
                p_pHemisphere->use_tri_cent(i, 1) += p_pHemisphere->rr(k, 1);
                p_pHemisphere->use_tri_cent(i, 2) += p_pHemisphere->rr(k, 2);
            }
            p_pHemisphere->use_tri_cent.row(i) /= 3.0f;

            //cross product {cross((r2-r1),(r3-r1))}
            a = r.row(1) - r.row(0 );
            b = r.row(2) - r.row(0);
            p_pHemisphere->use_tri_nn(i,0) = a(1)*b(2)-a(2)*b(1);
            p_pHemisphere->use_tri_nn(i,1) = a(2)*b(0)-a(0)*b(2);
            p_pHemisphere->use_tri_nn(i,2) = a(0)*b(1)-a(1)*b(0);

            //area
            size = p_pHemisphere->use_tri_nn.row(i)*p_pHemisphere->use_tri_nn.row(i).transpose();
            size = std::pow(size, 0.5f );

            p_pHemisphere->use_tri_area(i) = size/2.0f;
        }

    }
    printf("[done]\n");

//        qDebug() << "p_pHemisphere->use_tri_cent:" << p_pHemisphere->use_tri_cent(0,0) << p_pHemisphere->use_tri_cent(0,1) << p_pHemisphere->use_tri_cent(0,2);
//        qDebug() << "p_pHemisphere->use_tri_cent:" << p_pHemisphere->use_tri_cent(2,0) << p_pHemisphere->use_tri_cent(2,1) << p_pHemisphere->use_tri_cent(2,2);

//        qDebug() << "p_pHemisphere->use_tri_nn:" << p_pHemisphere->use_tri_nn(0,0) << p_pHemisphere->use_tri_nn(0,1) << p_pHemisphere->use_tri_nn(0,2);
//        qDebug() << "p_pHemisphere->use_tri_nn:" << p_pHemisphere->use_tri_nn(2,0) << p_pHemisphere->use_tri_nn(2,1) << p_pHemisphere->use_tri_nn(2,2);

    return true;
}
